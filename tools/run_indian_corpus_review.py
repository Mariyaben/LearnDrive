"""Evaluate current production perception/policy on prepared Indian excerpts.

Artifacts explicitly distinguish software contracts, measured replay quantities,
unavailable telemetry and visual/instructor judgements. No accuracy is invented.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import re
from pathlib import Path
import subprocess
import sys
import time
import zipfile
from replay_contract import expected_semantic_indices

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "android/app/src/main/assets"


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for data in iter(lambda: source.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def verified_new_encounter(row, previous):
    """Verify coordinator exception provenance, not physical driving accuracy."""
    fields = {k: int(v) for k, v in re.findall(r"(?:^|;\s*)(encounter|reference|source|new_entry)=(\d+)(?=;|$)", row["reason"])}
    actor, reference = row["camera_actor"], previous["camera_actor"]
    return bool(row["prompt"] in (18, 44, 45, 46)
        and row["origin"] == 4 and previous["origin"] == 4 and actor and reference
        and actor["category"] == reference["category"]
        and row["scene_age"] is not None and 0 <= row["scene_age"] < .35
        and row["source_id"] == previous["source_id"]
        and fields.get("source") == row["source_id"]
        and fields.get("new_entry") == 1 and fields.get("encounter") == actor["id"]
        and fields.get("reference") == reference["id"] and actor["id"] != reference["id"])


def audit(folder):
    report = json.loads((folder / "report.json").read_text())
    observations = json.loads((folder / "observations.json").read_text())
    decisions, accepted = [], []
    latest = None
    errors = []
    for line in (folder / "decisions.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row["event"] == "decision":
            if latest and row["sequence"] <= latest["sequence"]:
                errors.append("Non-increasing decision sequence")
            if row["eligible"] != (row["prompt"] != 0 and row["blocked"] == 0):
                errors.append("Eligibility/blocked mismatch")
            latest = row
            decisions.append(row)
        elif row["event"] == "accepted":
            if not latest or not latest["eligible"] or any(row[k] != latest[k] for k in ("sequence", "prompt", "source_id")):
                errors.append("Acceptance does not match eligible decision")
            accepted.append({**row, "reason": latest.get("reason", "") if latest else "",
                             "camera_actor": latest.get("camera_actor") if latest else None,
                             "scene_age": latest.get("scene_age") if latest else None,
                             "origin": latest.get("origin") if latest else None})
        else:
            errors.append("Unknown decision trace event")
    if len(accepted) != len(report["events"]) + len(report["startup_events"]):
        errors.append("Report/acknowledgement count mismatch")
    if len(observations) != report["frames"]:
        errors.append("Frame count mismatch")
    mode = report["assumptions"]["mode"]
    if mode == "semantic" and any(r["spatial_available"] for r in decisions):
        errors.append("Spatial output leaked into uncalibrated semantic run")
    unknown_speed = report["assumptions"]["manual_test_speed_kmh"] is None and not report["assumptions"]["speed_samples"]
    if unknown_speed and any(r["speed_kmh"] is not None for r in observations):
        errors.append("Unknown speed was fabricated")
    for first, second in zip(accepted, accepted[1:]):
        if second["time"] - first["time"] < 3.5 - 1e-8:
            errors.append("Ordinary prompt global spacing below 3.5 seconds")
    last = {}
    for row in accepted:
        previous = last.get(row["prompt"])
        if previous and row["time"] - previous["time"] < 10 - 1e-8:
            if not verified_new_encounter(row, previous):
                errors.append("Duplicate prompt lock below 10 seconds without verified new encounter")
        last[row["prompt"]] = row
    if report.get("schema_version") == 2:
        cache = report.get("inference_cache", {})
        origin = cache.get("verified_origin", {})
        if origin.get("provenance_sha256") != cache.get("key"):
            errors.append("Inference cache provenance is missing/inconsistent")
        if Path(origin.get("created_for_output", "")).parent.resolve() != folder.parent.resolve():
            errors.append("Inference cache origin is outside the current evidence run")
        previous_publication = -math.inf
        semantic_captures = set()
        expected_semantics = set(int(i) for i in expected_semantic_indices(report["frames"],
            report["assumptions"]["analysis_fps"], report["assumptions"]["semantic_max_hz"]))
        for line in (folder / "publications.jsonl").read_text().splitlines():
            publication = json.loads(line)
            if publication["publication_time"] < previous_publication:
                errors.append("Non-monotonic publication clock")
            if publication["capture_time"] > publication["completion_time"] or publication["completion_time"] > publication["publication_time"]:
                errors.append("Publication precedes capture/completion")
            if publication["stage"] == "semantic":
                semantic_captures.add(publication["capture_time"])
            elif mode == "semantic":
                errors.append("Spatial publication in semantic-only run")
            elif publication["frame_index"] in expected_semantics and publication["capture_time"] not in semantic_captures:
                errors.append("Spatial output published before semantic evidence from its capture")
            previous_publication = publication["publication_time"]
    duration = float(report["metadata"]["format"]["duration"])
    invideo = [r for r in decisions if .35 <= r["recorded_time"] < duration]
    spoken = 0.0
    interrupted = 0
    for i, event in enumerate(report["events"]):
        next_time = report["events"][i + 1]["time"] if i + 1 < len(report["events"]) else duration
        spoken += max(0, min(duration, next_time, event["time"] + event["duration"]) - max(0, event["time"]))
        interrupted += bool(event["interrupted_prompt"])
    return {"contract_errors": sorted(set(errors)), "trace_contract": "pass" if not errors else "fail",
            "guidance_quality": "requires event-linked visual/independent instructor review",
            "duration_seconds": duration, "frame_steps": report["frames"],
            "spoken_events": len(report["events"]), "events_per_minute": len(report["events"]) / duration * 60,
            "scheduled_speech_seconds": spoken, "speech_fraction": spoken / duration,
            "interruptions": interrupted,
            "fresh_semantic_tick_fraction": sum(r["semantic_available"] for r in invideo) / len(invideo) if invideo else None,
            "fresh_spatial_tick_fraction": sum(r["spatial_available"] for r in invideo) / len(invideo) if invideo else None,
            "road_valid_frames": sum(bool(r["road"][1]) for r in observations),
            "frames_with_detections": sum(bool(r["boxes"]) for r in observations),
            "actor_linked_video_events": sum(e["camera_actor"] is not None for e in report["events"]),
            "host_inference_ms": report["host_inference_ms"], "events": report["events"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/traffic-review-2026-10-22")
    parser.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_replay.dll")
    parser.add_argument("--apk", type=Path, help="Optional APK reference for model-asset comparison; this runner does not execute APKs")
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--watch-count", type=int, default=0)
    parser.add_argument("--max-wait-seconds", type=int, default=1100)
    parser.add_argument("--extra", action="store_true", help="Additional speed/delay/optics controls on selected excerpts")
    parser.add_argument("--control-clips", nargs="*", help="Only unknown/stopped controls for these prepared IDs")
    args = parser.parse_args()
    output = args.output.resolve()
    runs = output / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    native = args.library.resolve()
    source_files = [p for base in (ROOT / "core/src", ROOT / "core/include") for p in sorted(base.rglob("*")) if p.is_file()]
    source_files += [ROOT / "desktop/replay_bridge.cpp", ROOT / "tools/review_video.py", ROOT / "tools/replay_contract.py",
                     Path(__file__), ROOT / "android/app/src/main/java/ai/learndrive/mobile/Vision.kt"]
    fingerprints = {str(p.relative_to(ROOT)): sha(p) for p in source_files}
    fingerprints.update({"native_dll": sha(native), "detector_asset": sha(ASSETS / "detector.onnx"),
                         "supercombo_asset": sha(ASSETS / "supercombo.onnx")})
    gradle = (ROOT / "android/app/build.gradle.kts").read_text()
    version_name = re.search(r'versionName\s*=\s*"([^"]+)"', gradle).group(1)
    version_code = int(re.search(r"versionCode\s*=\s*(\d+)", gradle).group(1))
    apk = args.apk.resolve() if args.apk else ROOT / f"build/LearnDrive-{version_name}-debug.apk"
    if args.apk and not apk.is_file():
        raise ValueError(f"Explicit APK reference is missing: {apk}")
    apk_match = {}
    if apk.exists():
        with zipfile.ZipFile(apk) as source:
            for name in ("detector.onnx", "supercombo.onnx"):
                with source.open("assets/" + name) as stream:
                    h = hashlib.sha256()
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        h.update(chunk)
                apk_match[name] = {"apk_sha256": h.hexdigest(), "source_sha256": sha(ASSETS / name),
                                   "matches": h.hexdigest() == sha(ASSETS / name)}
    protocol = {"schema_version": 2, "app_version": f"{version_name} / {version_code}",
                "app_version_source": "android/app/build.gradle.kts; APK manifest not inspected",
                "apk_path": str(apk), "apk_sha256": sha(apk) if apk.is_file() else None,
                "apk_reference_available": apk.is_file(),
                "apk_asset_comparison": apk_match, "fingerprints": fingerprints,
                "primary": {"mode": "semantic", "fps": 30, "semantic_max_hz": 8,
                            "assumed_speed_kmh": 30, "simulated_delay_ms": 224, "pre_roll_seconds": 6},
                "test_scope": "Fresh inference and current shared C++ perception/coaching decisions; not APK/UI or real audio",
                "unknowns": ["original optics/IMU/speed/gear/RPM/navigation feed", "instructor coaching ground truth"],
                "no_tuning": True,
                "cache_policy": "Verified schema-v2 current-provenance inference may be shared only within this evidence directory; original creation run is retained in each report",
                "host_provider": "Python ORT CPU; exact runtime/binary/preprocessing hashes in each report inference_cache. This is not Android XNNPACK execution.",
                "spatial_controls": "Assumed HFOV70/110 and level gravity; sensitivity only, never calibrated bend accuracy"}
    protocol_path = output / "evaluation-protocol.json"
    if protocol_path.exists():
        previous = json.loads(protocol_path.read_text())
        if previous != protocol:
            raise RuntimeError("Existing evidence uses a different protocol/code/models/binary/APK reference. Use a fresh output directory.")
    else:
        save(protocol_path, protocol)
    results_path = output / "aggregate.json"
    results = json.loads(results_path.read_text()) if results_path.exists() else []
    done = {r["run_id"] for r in results}
    regressions = [
        ("jodhpur-clear", ROOT / "build/public-traffic/clips/jodhpur-clear.mp4"),
        ("jodhpur-rain", ROOT / "build/public-traffic/clips/jodhpur-rain.mp4"),
        ("bangalore-cv-raman", ROOT / "build/public-traffic-extension/clips/bangalore-cv-raman.mp4"),
        ("kochi-munnar", ROOT / "output-video-1_w8CLL23y.mp4")]
    prepared = {}
    submitted = set(done)
    checked_videos = {}

    def run(clip, mode="semantic", speed=30, delay=224, hfov=110):
        speed_tag = "unknown" if speed is None else f"manual{speed}"
        run_id = f"{clip['id']}-{mode}-{speed_tag}-delay{delay}" + (f"-hfov{hfov}" if mode == "spatial" else "")
        folder = runs / run_id
        folder.mkdir(exist_ok=True)
        command = [sys.executable, str(ROOT / "tools/review_video.py"), str(clip["path"]),
                   "--mode", mode, "--fps", "20" if mode == "spatial" else "30", "--semantic-hz", "8",
                   "--latency-ms", str(delay), "--pre-roll-seconds", "6", "--hfov", str(hfov),
                   "--output", str(folder), "--library", str(native)]
        if speed is not None:
            command += ["--test-speed-kmh", str(speed)]
        save(folder / "command.json", command)
        print("RUN", run_id, flush=True)
        start = time.monotonic()
        with (folder / "run.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600)
        if process.returncode:
            raise RuntimeError(f"Replay failed: {folder / 'run.log'}")
        result = {"run_id": run_id, "clip": clip["id"], "mode": mode, "manual_test_speed_kmh": speed,
                  "simulated_delay_ms": delay, "hfov": hfov if mode == "spatial" else None,
                  "development_regression": clip.get("development_regression", False),
                  "output": str(folder), "wall_seconds": time.monotonic() - start, **audit(folder)}
        print("DONE", run_id, result["frame_steps"], "frames", result["spoken_events"], "cues",
              f"speech {result['speech_fraction']:.1%}", result["trace_contract"], flush=True)
        return result

    def specs(clip):
        if args.control_clips:
            return [("semantic", None, 224, 110), ("semantic", 0, 224, 110)] if clip["id"] in args.control_clips else []
        specs = [("semantic", 30, 224, 110)]
        if args.extra and clip["id"] in {"jodhpur-clear", "jodhpur-rain", "bangalore-cv-raman", "gp_set_0009_vid_0001", "gp_set_0003_vid_0005"}:
            specs += [("semantic", None, 224, 110), ("semantic", 0, 224, 110),
                      ("semantic", 30, 350, 110), ("semantic", 30, 500, 110),
                      ("spatial", 30, 224, 70), ("spatial", 30, 224, 110)]
        return specs

    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending = {}
        while True:
            path = output / "prepared-iddped.json"
            if path.exists():
                try:
                    prepared = {r["id"]: r for r in json.loads(path.read_text()) if r["status"] == "prepared"}
                except (json.JSONDecodeError, OSError):
                    pass
            available = [dict(id=key, path=str(path), development_regression=True) for key, path in regressions if path.exists()]
            available += list(prepared.values())
            for clip in available:
                if clip["id"] not in checked_videos:
                    checked_videos[clip["id"]] = sha(clip["path"])
                video_hash = checked_videos[clip["id"]]
                for mode, speed, delay, hfov in specs(clip):
                    speed_tag = "unknown" if speed is None else f"manual{speed}"
                    run_id = f"{clip['id']}-{mode}-{speed_tag}-delay{delay}" + (f"-hfov{hfov}" if mode == "spatial" else "")
                    if run_id not in submitted:
                        pending[pool.submit(run, clip, mode, speed, delay, hfov)] = run_id
                        submitted.add(run_id)
                    elif run_id in done:
                        recorded = json.loads((runs / run_id / "report.json").read_text())
                        if recorded["sha256"] != video_hash or recorded["native_library_sha256"] != fingerprints["native_dll"]:
                            raise RuntimeError(f"Stale video/binary result: {run_id}; use a fresh output directory")
            finished = [future for future in pending if future.done()]
            for future in finished:
                run_id = pending.pop(future)
                try:
                    results.append(future.result())
                    save(results_path, results)
                except Exception as error:
                    print("FAILED", run_id, str(error), flush=True)
                    save(output / (run_id + "-error.json"), {"run": run_id, "error": str(error)})
            enough = not args.watch_count or len(prepared) >= args.watch_count
            if not pending and (enough or time.monotonic() - started > args.max_wait_seconds):
                break
            time.sleep(2)
    changed = [str(p.relative_to(ROOT)) for p in source_files if sha(p) != fingerprints[str(p.relative_to(ROOT))]]
    if sha(native) != fingerprints["native_dll"]:
        changed.append("native_dll")
    save(output / "source-stability.json", {"source_files_changed_during_run": changed,
         "matches_latest_apk_model_assets": all(r["matches"] for r in apk_match.values()) if apk_match else None,
         "completed_runs": len(results), "prepared_raw_video_files": len(prepared)})
    if changed:
        raise RuntimeError("Source or native binary changed during evaluation; discard this run as current-build evidence")
    print("COMPLETE", len(results), "replays", len(prepared), "new source videos", "changed files", changed, flush=True)


if __name__ == "__main__":
    main()
