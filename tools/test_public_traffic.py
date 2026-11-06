"""Fetch pinned public traffic previews and replay them through native guidance.

No training, no uploading and no inference that test speed is a measurement.
The download bound is 250 MB per clip; source checksums are pinned in the manifest.
"""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def download(clip, target):
    if not target.exists():
        pending = target.with_suffix(".part")
        with urllib.request.urlopen(clip["url"], timeout=40) as source, pending.open("wb") as dest:
            total = 0
            while chunk := source.read(1024 * 1024):
                total += len(chunk)
                if total > 250_000_000:
                    raise ValueError("Preview exceeds per-file download bound")
                dest.write(chunk)
        if sha256(pending) != clip["sha256"]:
            raise ValueError(f"Source checksum mismatch: {clip['id']}")
        pending.replace(target)
    if sha256(target) != clip["sha256"]:
        raise ValueError(f"Existing source checksum mismatch: {target}")


def audit_report(folder, mode, manual_speed):
    report = json.loads((folder / "report.json").read_text())
    decisions = []
    accepted = []
    latest = None
    for line in (folder / "decisions.jsonl").read_text().splitlines():
        row = json.loads(line)
        assert row["schema_version"] == 1
        if row["event"] == "decision":
            assert latest is None or row["sequence"] > latest["sequence"]
            assert row["eligible"] == (row["prompt"] != 0 and row["blocked"] == 0)
            if mode == "semantic":
                assert not row["spatial_available"], "Uncalibrated replay leaked a spatial prediction"
            latest = row
            decisions.append(row)
        else:
            assert row["event"] == "accepted" and latest and latest["eligible"]
            assert row["sequence"] == latest["sequence"] and row["prompt"] == latest["prompt"]
            assert row["source_id"] == latest["source_id"] and row["time"] >= latest["time"]
            accepted.append(row)
    assert len(accepted) == len(report["events"]) + len(report["startup_events"])
    observations = json.loads((folder / "observations.json").read_text())
    assert len(observations) == report["frames"] > 0
    if manual_speed is None:
        assert all(r["speed_kmh"] is None for r in observations), "Missing speed was fabricated"
    if mode == "semantic":
        assert all(r["lead_probability"] is None and not r["road"][1] for r in observations)
    urgent = {4, 19, 42, 43}
    assert all(e["prompt"] not in urgent for e in report["events"])
    for a, b in zip(accepted, accepted[1:]):
        assert b["time"] - a["time"] >= 3.5 - 1e-8
    last = {}
    for event in accepted:
        assert event["time"] - last.get(event["prompt"], -1e30) >= 10 - 1e-8
        last[event["prompt"]] = event["time"]
    duration = float(report["metadata"]["format"]["duration"])
    spoken_seconds = 0.0
    for i, event in enumerate(report["events"]):
        next_time = report["events"][i + 1]["time"] if i + 1 < len(report["events"]) else duration
        spoken_seconds += max(0.0, min(duration, next_time, event["time"] + event["duration"]) - max(0.0, event["time"]))
    return {"frames": report["frames"], "spoken_events": len(report["events"]),
            "emergency_candidate_ticks": sum(any(v in urgent for v in d["candidates"]) for d in decisions),
            "road_valid_frames": sum(bool(r["road"][1]) for r in observations),
            "road_valid_fraction": sum(bool(r["road"][1]) for r in observations) / len(observations),
            "frames_with_detections": sum(bool(r["boxes"]) for r in observations),
            "decision_ticks": len(decisions), "trace_contract": "pass", "guidance_quality": "requires visual review",
            "spoken_seconds": spoken_seconds, "speech_fraction": spoken_seconds / duration,
            "events": report["events"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/public-traffic")
    parser.add_argument("--mode", choices=["both", "spatial", "semantic"], default="both")
    parser.add_argument("--test-speed-kmh", type=float,
                        help="Assumed manual input; omit to test missing speed")
    parser.add_argument("--clips", nargs="*")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "scenarios/public-traffic/sources.json").read_text())
    for name in ("sources", "clips", "runs"):
        (args.output / name).mkdir(parents=True, exist_ok=True)
    modes = ["semantic", "spatial"] if args.mode == "both" else [args.mode]
    results = []
    prepared = []
    for clip in manifest["clips"]:
        if args.clips and clip["id"] not in args.clips:
            continue
        if not re.fullmatch(r"[a-z0-9-]+", clip["id"]):
            raise ValueError("Invalid clip ID")
        source = args.output / "sources" / (clip["id"] + ".mp4")
        download(clip, source)
        video = args.output / "clips" / source.name
        transform = ["ffmpeg", "-v", "error", "-y", "-threads", "2", "-i", str(source),
                     "-vf", "scale=640:360", "-an", "-c:v", "libx264", "-threads", "2", "-crf", "18", str(video)]
        if not video.exists():
            subprocess.run(transform, check=True)
        prepared.append({"id": clip["id"], "source_sha256": sha256(source), "clip_sha256": sha256(video),
                         "transform": transform, "source_page": clip["source_page"]})
        (args.output / "prepared.json").write_text(json.dumps(prepared, indent=2))
        for mode in modes:
            speed_tag = "unknown" if args.test_speed_kmh is None else f"manual-{args.test_speed_kmh:g}"
            folder = args.output / "runs" / f"{clip['id']}-{mode}-{speed_tag}"
            folder.mkdir(exist_ok=True)
            command = [sys.executable, str(ROOT / "tools/review_video.py"), str(video), "--mode", mode,
                       "--fps", str(clip["analysis_fps"]), "--hfov", "110", "--pre-roll-seconds", "6",
                       "--latency-ms", "180", "--output", str(folder)]
            if args.test_speed_kmh is not None:
                command += ["--test-speed-kmh", str(args.test_speed_kmh)]
            if args.render:
                command += ["--render"]
            print("RUN", clip["id"], mode, speed_tag, flush=True)
            with (folder / "run.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
                if process.wait():
                    raise RuntimeError(f"Replay failed: {folder / 'run.log'}")
            result = {"clip": clip["id"], "mode": mode, "manual_test_speed_kmh": args.test_speed_kmh,
                      "output": str(folder), **audit_report(folder, mode, args.test_speed_kmh)}
            results.append(result)
            (args.output / f"summary-{speed_tag}.json").write_text(json.dumps(results, indent=2))
    if not results:
        raise ValueError("No matching clips")
    print("Contract checks passed for", len(results), "replays; guidance quality requires separate visual review", flush=True)


if __name__ == "__main__":
    main()
