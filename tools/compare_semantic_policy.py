"""Compare historical/current policy with identical verified semantic inputs.

This deliberately separate classic-ABI driver never relaxes review_video's v2
bridge requirement. It reuses current-run raw detector tensors and normalized
YUV, with no spatial inference, geometry, original motion or navigation. The
historical bridge is allowed only at the pinned baseline SHA-256 below.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import time
import wave

import numpy as np

from replay_contract import file_sha256, inference_key, load_cache
from review_video import frames, ptr

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "android/app/src/main/assets"
BASELINE_SHA256 = "53831be980cf341bdce5015ec35a5f40d5ec32ab262c6e0624ea76f3ceb33b5b"
P = C.c_void_p


class ClassicBridge:
    """Only the common, explicit pre-v2 ABI; no cached spatial entry point."""
    def __init__(self, path, dependency_directory, current=False):
        self.directories = []
        if os.name == "nt":
            self.directories = [os.add_dll_directory(str(p)) for p in {path.parent, dependency_directory}]
        self.lib = C.CDLL(str(path))
        if current:
            version = self.lib.replay_contract_version
            version.argtypes, version.restype = [], C.c_int
            if version() != 2:
                raise RuntimeError("Current comparison DLL must implement replay contract v2")
        signatures = {
            "create": ([C.c_char_p, C.c_int, C.c_int, P], P), "destroy": ([P], None),
            "error": ([P], C.c_char_p), "prepare": ([P, P, P], C.c_int),
            "labels": ([P, C.c_double, P, C.c_size_t, C.c_int], C.c_int),
            "tick": ([P, C.c_double, C.c_double, C.c_int, C.c_int, P], None),
            "trace": ([P, P], None), "accepted": ([P, C.c_int, C.c_double, C.c_int], None),
            "emergencies": ([P, C.c_int], None), "guidance_reason": ([P], C.c_char_p),
            "prompt": ([C.c_int], C.c_char_p), "prompt_count": ([], C.c_int),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, "replay_" + name)
            function.argtypes, function.restype = arguments, result
            setattr(self, name, function)

    def require(self, handle, result):
        if not result:
            raise RuntimeError(self.error(handle).decode("utf-8", "replace"))


def checked_report(path, current_library):
    report = json.loads(path.read_text(encoding="utf-8"))
    assumptions = report["assumptions"]
    if report.get("schema_version") != 2 or report.get("replay_bridge_contract") != 2:
        raise ValueError("Comparison requires a verified v2 current-run report")
    expected = {"mode": "semantic", "manual_test_speed_kmh": 30, "analysis_fps": 30,
                "semantic_max_hz": 8, "simulated_inference_latency_ms": 224,
                "startup_before_video_seconds": 6, "simulated_spatial_extra_latency_ms": 0}
    if any(assumptions.get(k) != value for k, value in expected.items()) or assumptions["speed_samples"]:
        raise ValueError(f"Report does not use the pinned matched semantic protocol: {path}")
    if report["research_emergencies"]:
        raise ValueError("Historical comparison excludes experimental emergency commands")
    cache = report["inference_cache"]
    provenance = cache["verified_origin"]["provenance"]
    if provenance["input"]["spatial"] or cache["key"] != inference_key(provenance):
        raise ValueError("Not a semantic-only inference cache")
    arrays, origin = load_cache(Path(cache["path"]), provenance)
    if origin != cache["verified_origin"] or len(arrays["outputs"]) != report["frames"]:
        raise ValueError("Cache origin/frame count changed after the reviewed run")
    video = Path(report["video"])
    if file_sha256(video) != report["sha256"] or report["sha256"] != provenance["video_sha256"]:
        raise ValueError("Video differs from verified detector inputs")
    if file_sha256(current_library) != report["native_library_sha256"] or report["native_library_sha256"] != provenance["native_library_sha256"]:
        raise ValueError("Current DLL differs from reviewed/cache provenance")
    for relative, expected_hash in provenance["source_sha256"].items():
        if file_sha256(ROOT / relative) != expected_hash:
            raise ValueError(f"Current reviewed source changed: {relative}")
    if file_sha256(ASSETS / "detector.onnx") != provenance["models"]["detector.onnx"]:
        raise ValueError("Detector asset changed after inference")
    if file_sha256(shutil.which("ffmpeg")) != provenance["decoder"]["executable_sha256"]:
        raise ValueError("FFmpeg changed after inference")
    expected_decoder = "noautorotate; fps filter; limited-range BT.601 packed yuv420p"
    if provenance["decoder"]["pipeline"] != expected_decoder:
        raise ValueError("Unrecognized decoder pipeline; no implicit historical upgrade")
    for name, expected_hash in provenance["native_runtime_sha256"].items():
        if file_sha256(current_library.parent / name) != expected_hash:
            raise ValueError(f"Native runtime dependency changed: {name}")
    return report, arrays, origin


def replay(bridge, report, arrays, durations):
    stream = next(s for s in report["metadata"]["streams"] if s["codec_type"] == "video")
    width, height = stream["width"], stream["height"]
    # Null geometry + empty model: the historical label bridge must never get a
    # spatial buffer. Every decision is checked for unexpected spatial evidence.
    handle = bridge.create(b"", width, height, None)
    if not handle:
        raise RuntimeError("Cannot initialize semantic-only comparison source")
    bridge.emergencies(handle, 0)
    tensor, decision, trace = np.empty((1, 3, 320, 320), np.float32), np.empty(8), np.empty(22)
    events, startup_events = [], []
    tick, playing, playing_until = 0, 0, 0.
    fps, latency, pre_roll = 30, .224, 6.
    semantic = {int(i): n for n, i in enumerate(arrays["semantic_indices"])}
    decoded = iter(frames(Path(report["video"]), width, height, fps))
    decoded_index = -1
    semantic_yuv = hashlib.sha256()
    decision_count = 0
    def evaluate_until(deadline):
        nonlocal tick, playing, playing_until, decision_count
        while tick / 100 < deadline - 1e-8:
            now = tick / 100
            if now >= playing_until:
                playing = 0
            known_speed = now >= pre_roll
            bridge.tick(handle, now, 30. if known_speed else math.nan, int(known_speed), playing, ptr(decision))
            bridge.trace(handle, ptr(trace))
            decision_count += 1
            if trace[14]:
                raise RuntimeError("Historical/current semantic-only driver published spatial evidence; comparison invalid")
            selected = int(decision[0])
            if selected and decision[6] and decision[7]:
                actor = {"id": int(trace[15]), "category": int(trace[16]), "evidence_kind": int(trace[17]),
                         "box": trace[18:22].tolist()} if trace[15] else None
                event = {"time": round(now - pre_roll, 4), "prompt": selected,
                    "text": bridge.prompt(selected).decode(), "duration": durations[selected],
                    "speed_kmh": 30. if known_speed else None,
                    "source": {1: "dual", 2: "tactical", 3: "terrain", 4: "camera"}.get(int(trace[3]), "none"),
                    "reason": bridge.guidance_reason(handle).decode(), "camera_actor": actor,
                    "interrupted_prompt": playing}
                (events if known_speed else startup_events).append(event)
                bridge.accepted(handle, selected, now, int(trace[3] == 1))
                playing, playing_until = selected, now + durations[selected]
            tick += 1
    try:
        for index, detection_index in semantic.items():
            capture = index / fps
            evaluate_until(capture + latency + pre_roll)
            while decoded_index < index:
                try:
                    frame = next(decoded)
                except StopIteration as error:
                    raise RuntimeError("Decoded video ended before cached semantic inputs") from error
                decoded_index += 1
            bridge.require(handle, bridge.prepare(handle, ptr(frame), ptr(tensor)))
            semantic_yuv.update(index.to_bytes(8, "little"))
            semantic_yuv.update(frame)
            values = np.ascontiguousarray(arrays["detections"][detection_index])
            bridge.require(handle, bridge.labels(handle, capture + pre_roll, ptr(values), values.size, values.shape[1]))
        # Semantic sampling can stop a few frames before the clip ends. Drain
        # the decoder so successful comparisons also confirm a complete decode.
        for _ in decoded:
            pass
        evaluate_until(len(arrays["outputs"]) / fps + latency + pre_roll)
    finally:
        decoded.close()
        bridge.destroy(handle)
    return {"events": events, "startup_events": startup_events, "decision_tick_count": decision_count,
            "spatial_available_observed": False, "video_spoken_events": len(events),
            "semantic_yuv_sha256": semantic_yuv.hexdigest(), "semantic_input_frames": len(semantic)}


def comparable_events(events):
    keys = ("time", "prompt", "text", "duration", "speed_kmh", "source", "reason", "camera_actor", "interrupted_prompt")
    return [{key: event[key] for key in keys} for event in events]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path, help="Current primary semantic report.json files")
    parser.add_argument("--baseline-library", type=Path,
        default=ROOT / "build/trust-increment-0.4.6/baseline/liblearndrive_replay.dll")
    parser.add_argument("--current-library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_replay.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "build/trust-increment-0.4.6/matched-semantic-comparison")
    args = parser.parse_args()
    baseline, current = args.baseline_library.resolve(), args.current_library.resolve()
    if file_sha256(baseline) != BASELINE_SHA256:
        raise ValueError("Historical baseline DLL does not match the pinned original SHA-256")
    current_hash = file_sha256(current)
    old_bridge = ClassicBridge(baseline, current.parent)
    new_bridge = ClassicBridge(current, current.parent, current=True)
    if old_bridge.prompt_count() != new_bridge.prompt_count():
        raise ValueError("Prompt catalogs have different sizes; explicit comparison upgrade needed")
    durations, audio = {}, {}
    for prompt in range(1, new_bridge.prompt_count() + 1):
        path = ASSETS / f"audio/prompt-{prompt}.wav"
        with wave.open(str(path), "rb") as clip:
            durations[prompt] = clip.getnframes() / clip.getframerate()
        audio[str(prompt)] = {"sha256": file_sha256(path), "duration_seconds": durations[prompt]}
    args.output.mkdir(parents=True, exist_ok=True)
    summary = []
    for path in args.reports:
        started = time.monotonic()
        report, arrays, origin = checked_report(path.resolve(), current)
        old = replay(old_bridge, report, arrays, durations)
        new = replay(new_bridge, report, arrays, durations)
        if old["semantic_yuv_sha256"] != new["semantic_yuv_sha256"]:
            raise RuntimeError("Historical/current drivers did not receive identical semantic YUV frames")
        if comparable_events(new["events"]) != comparable_events(report["events"]) or comparable_events(new["startup_events"]) != comparable_events(report["startup_events"]):
            raise RuntimeError(f"Common classic-ABI driver disagrees with current v2 report: {path}")
        if file_sha256(current) != current_hash or file_sha256(baseline) != BASELINE_SHA256:
            raise RuntimeError("A policy binary changed during comparison")
        for relative, expected_hash in origin["provenance"]["source_sha256"].items():
            if file_sha256(ROOT / relative) != expected_hash:
                raise RuntimeError(f"Reviewed source changed during comparison: {relative}")
        result = {"schema_version": 1, "current_report": str(path.resolve()),
            "current_report_sha256": file_sha256(path), "video_sha256": report["sha256"],
            "baseline_library_sha256": BASELINE_SHA256, "current_library_sha256": current_hash,
            "comparison_driver_sha256": file_sha256(Path(__file__)), "verified_input_cache": origin,
            "common_audio": audio, "baseline": old, "current": new,
            "current_driver_matches_v2_review_events": True,
            "wall_seconds": time.monotonic() - started,
            "limitations": ["Matched normalized limited-range BT.601 YUV and raw current-run detector tensors isolate shared-native behavior; not APK/device validation",
                "Historical bridge label update could republish old/zero spatial outputs when geometry was enabled; this driver supplies no geometry/model and fails on any observed spatial availability",
                "Manual 30 km/h, 224 ms semantic delay, six-second pre-roll and common current PCM durations are explicit assumptions",
                "No original optics/IMU/GPS, calibrated spatial inference, route/navigation, device scheduling or physical speech timing"]}
        destination = args.output / (path.parent.name + ".json")
        destination.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        summary.append({"run_id": path.parent.name, "baseline_events": len(old["events"]),
                        "current_events": len(new["events"]), "output": str(destination.resolve())})
        print(path.parent.name, "baseline", len(old["events"]), "current", len(new["events"]), flush=True)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
