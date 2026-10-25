"""Replay real video through the shared C++ perception/policy code and save evidence.

Requires OpenCV, NumPy, Python ONNX Runtime, ffmpeg/ffprobe and the CMake
learndrive_replay target. No IMU, camera calibration or road grade is invented.
--hfov is an explicitly assumed camera field of view for sensitivity testing.
"""
import argparse
from bisect import bisect_right
import csv
import ctypes as C
import json
import math
import os
from pathlib import Path
import subprocess
import shutil
import time
import wave
import platform

import cv2
import numpy as np
import onnxruntime as ort
from replay_contract import (SpeedTimeline, file_sha256, inference_key,
    inference_provenance, load_cache, publication_events, save_cache)

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "android/app/src/main/assets"
P = C.c_void_p


def digest(path):
    return file_sha256(path)


class Native:
    def __init__(self, library):
        self.directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
        self.lib = C.CDLL(str(library))
        try:
            version = self.lib.replay_contract_version
            version.argtypes, version.restype = [], C.c_int
            if version() != 2:
                raise RuntimeError("Unsupported replay bridge contract; rebuild learndrive_replay")
        except AttributeError as error:
            raise RuntimeError("Replay bridge predates semantic-first publication; rebuild learndrive_replay") from error
        signatures = {
            "create": ([C.c_char_p, C.c_int, C.c_int, P], P),
            "destroy": ([P], None), "error": ([P], C.c_char_p),
            "frame": ([P, P, C.c_double, P], C.c_int),
            "cached_frame": ([P, C.c_double, P], C.c_int),
            "prepare": ([P, P, P], C.c_int),
            "labels": ([P, C.c_double, P, C.c_size_t, C.c_int], C.c_int),
            "boxes": ([P, P, C.c_size_t], C.c_size_t), "road": ([P, P], C.c_int),
            "tick": ([P, C.c_double, C.c_double, C.c_int, C.c_int, P], None),
            "accepted": ([P, C.c_int, C.c_double, C.c_int], None),
            "emergencies": ([P, C.c_int], None), "trace": ([P, P], None),
            "guidance_reason": ([P], C.c_char_p),
            "prompt": ([C.c_int], C.c_char_p), "prompt_count": ([], C.c_int), "reason": ([P, C.c_int], C.c_char_p),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, "replay_" + name)
            function.argtypes, function.restype = arguments, result
            setattr(self, name, function)

    def require(self, handle, result):
        if not result:
            raise RuntimeError(self.error(handle).decode("utf-8", "replace"))


def ptr(array):
    return array.ctypes.data_as(P)


def probe(video):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_entries",
        "format=duration:stream=index,codec_type,width,height,avg_frame_rate,color_range:stream_tags=rotate:stream_side_data=rotation",
        "-of", "json", str(video)], text=True))


def frames(video, width, height, fps):
    command = ["ffmpeg", "-v", "error", "-noautorotate", "-i", str(video), "-an", "-vf",
               f"fps={fps},scale=in_range=auto:out_range=tv:out_color_matrix=bt601",
               "-colorspace", "smpte170m", "-color_range", "tv",
               "-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:1"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE)
    size = width * height * 3 // 2
    completed = False
    try:
        while True:
            data = process.stdout.read(size)
            if not data:
                completed = True
                break
            if len(data) != size:
                raise RuntimeError("Truncated ffmpeg YUV frame")
            yield np.frombuffer(data, dtype=np.uint8)
    finally:
        process.stdout.close()
        if not completed and process.poll() is None:
            process.terminate()
        code = process.wait()
    if code:
        raise RuntimeError(f"ffmpeg decoding failed: {code}")


def finite(value):
    return float(value) if math.isfinite(value) else None


def render(video, output, observations, events, fps, latency):
    """Save precisely the replay's accepted cached speech, not editorial advice."""
    source = cv2.VideoCapture(str(video))
    source_fps = source.get(cv2.CAP_PROP_FPS)
    duration = source.get(cv2.CAP_PROP_FRAME_COUNT) / source_fps
    voice = np.zeros(int((duration + 12) * 22050), np.float32)
    for index, event in enumerate(events):
        with wave.open(str(ASSETS / f"audio/prompt-{event['prompt']}.wav"), "rb") as clip:
            pcm = np.frombuffer(clip.readframes(clip.getnframes()), "<i2").astype(np.float32)
        next_time = events[index + 1]["time"] if index + 1 < len(events) else duration + 12
        count = min(len(pcm), max(0, round((next_time - event["time"]) * 22050)))
        start = round(event["time"] * 22050)
        voice[start:start + count] = pcm[:count]
    wav_path = output / "guidance.wav"
    with wave.open(str(wav_path), "wb") as clip:
        clip.setnchannels(1); clip.setsampwidth(2); clip.setframerate(22050)
        clip.writeframes(np.clip(voice, -32768, 32767).astype("<i2").tobytes())
    width, height = int(source.get(3)), int(source.get(4))
    scale = 2
    writer = cv2.VideoWriter(str(output / "annotated-silent.mp4"), cv2.VideoWriter_fourcc(*"mp4v"),
                             source_fps, (width * scale, height * scale + 140))
    if not writer.isOpened():
        raise RuntimeError("Cannot initialize annotated video writer")
    frame_index = 0
    available = [o.get("available_time", o["time"] + latency) for o in observations]
    try:
        while True:
            ok, frame = source.read()
            if not ok:
                break
            timestamp = frame_index / source_fps
            # A capture cannot be displayed as available evidence before its
            # publication deadline. Historical reports retain their old clock.
            observation_index = bisect_right(available, timestamp) - 1
            observation = observations[max(0, observation_index)]
            image = cv2.resize(frame, (width * scale, height * scale))
            for box in observation["boxes"] if observation_index >= 0 else []:
                x, y, w, h, confidence, category = box
                a = (round(x * width * scale), round(y * height * scale))
                b = (round((x + w) * width * scale), round((y + h) * height * scale))
                cv2.rectangle(image, a, b, (40, 230, 70), 2)
                cv2.putText(image, f"class {int(category)} {confidence:.2f}", (a[0], max(20, a[1] - 4)),
                            cv2.FONT_HERSHEY_SIMPLEX, .5, (40, 230, 70), 1)
            road = observation["road"]
            if road[1] and observation_index >= 0:
                points = np.array([(round(road[i] * width * scale), round(road[i + 1] * height * scale))
                                   for i in range(3, 13, 2)], np.int32)
                cv2.polylines(image, [points], False, (0, 220, 255), 3)
            canvas = np.zeros((height * scale + 140, width * scale, 3), np.uint8)
            canvas[140:] = image
            active = [e for j, e in enumerate(events) if e["time"] <= timestamp <
                      min(e["time"] + e["duration"], events[j + 1]["time"] if j + 1 < len(events) else math.inf)]
            text = active[-1]["text"] if active else "No new spoken instruction"
            speed_text = f"{observation['speed_kmh']:.0f} km/h" if observation_index >= 0 and observation['speed_kmh'] is not None else "unknown"
            if observation.get("speed_source", "").startswith("assumed"):
                speed_text += " (assumed test setting)"
            optics = "Assumed optics" if observation.get("spatial_enabled", True) else "Semantic only; original optics unknown"
            evidence_text = (f"Capture {observation['time']:.2f}s; available {available[max(0, observation_index)]:.2f}s."
                             if observation_index >= 0 else "No published frame yet.")
            lines = [f"OFFLINE REPLAY  {timestamp:.2f}s | speed {speed_text}",
                     f"{optics}; no IMU. {evidence_text}", text[:95], text[95:]]
            for i, line in enumerate(lines):
                cv2.putText(canvas, line, (12, 26 + i * 30), cv2.FONT_HERSHEY_SIMPLEX, .55,
                            (245, 245, 245) if i < 2 else (0, 220, 255), 1, cv2.LINE_AA)
            writer.write(canvas); frame_index += 1
    finally:
        source.release(); writer.release()
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(output / "annotated-silent.mp4"),
                    "-i", str(wav_path), "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-threads", "2", "-crf", "21",
                    "-c:a", "aac", "-shortest", str(output / "guidance-review.mp4")], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path)
    parser.add_argument("--speeds", type=Path, help="Reviewed samples: time,speed_kmh; otherwise speed is unknown")
    parser.add_argument("--mode", choices=["spatial", "semantic"], default="spatial",
                        help="Spatial uses explicitly assumed optics; semantic matches uncalibrated Android MP4")
    parser.add_argument("--test-speed-kmh", type=float,
                        help="Explicit assumed manual speed for sensitivity tests; NOT a recorded measurement")
    parser.add_argument("--hfov", type=float, default=110)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--semantic-hz", type=float, default=8,
                        help="Maximum detector cadence; 4 reproduces the previous schedule, 8 matches Android 0.3.6")
    parser.add_argument("--latency-ms", type=float, default=0)
    parser.add_argument("--spatial-extra-latency-ms", type=float, default=0,
                        help="Additional spatial publication delay after semantic completion; idealized timeline, not Android scheduling")
    parser.add_argument("--speed-max-age-seconds", type=float, default=1.5,
                        help="Recorded CSV speed becomes unknown at this age; never backfill future samples")
    parser.add_argument("--fresh-inference", action="store_true",
                        help="Explicitly regenerate current-provenance raw inference instead of reusing a verified cache")
    parser.add_argument("--pre-roll-seconds", type=float, default=0,
                        help="Let startup speech finish before the clip begins")
    parser.add_argument("--output", type=Path, default=ROOT / "build/video-review/replay")
    parser.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_replay.dll")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--research-emergencies", action="store_true",
                        help="Enable unvalidated emergency phrases for explicit lab review")
    args = parser.parse_args()
    spatial = args.mode == "spatial"
    if args.test_speed_kmh is not None:
        if args.speeds or not math.isfinite(args.test_speed_kmh) or not 0 <= args.test_speed_kmh <= 160:
            raise ValueError("Use a finite 0-160 manual test speed OR recorded CSV samples")
    args.output.mkdir(parents=True, exist_ok=True)
    metadata = probe(args.video)
    stream = next(s for s in metadata["streams"] if s["codec_type"] == "video")
    width, height = stream["width"], stream["height"]
    if width % 2 or height % 2 or args.fps <= 0 or not 30 <= args.hfov <= 160 or not math.isfinite(args.semantic_hz) or not 1 <= args.semantic_hz <= 20:
        raise ValueError("Unsupported replay geometry/rate")
    rotation = float(stream.get("tags", {}).get("rotate", 0))
    for side_data in stream.get("side_data_list", []):
        rotation = float(side_data.get("rotation", rotation))
    if not math.isfinite(rotation) or rotation % 360 != 0:
        raise ValueError("Rotated video needs explicit orientation/calibration normalization before replay")
    if any(not math.isfinite(v) or v < 0 for v in (args.latency_ms, args.spatial_extra_latency_ms, args.pre_roll_seconds)):
        raise ValueError("Replay delays/pre-roll must be finite and nonnegative")
    if not spatial and args.spatial_extra_latency_ms:
        raise ValueError("Spatial completion delay requires --mode spatial")
    speeds = []
    if args.speeds:
        with args.speeds.open(newline="") as source:
            speeds = [(float(r["time"]), float(r["speed_kmh"])) for r in csv.DictReader(source)]
    if args.speeds and not speeds:
        raise ValueError("Recorded speed CSV is empty")
    speed_timeline = SpeedTimeline(speeds, args.test_speed_kmh, args.speed_max_age_seconds)
    speed_source = (f"reviewed overlay CSV; hold <{args.speed_max_age_seconds:g}s, otherwise unknown" if speeds else
                    f"assumed manual test input {args.test_speed_kmh:g} km/h; NOT recorded speed"
                    if args.test_speed_kmh is not None else "unknown; no recorded speed")
    calibration_origin = "assumed hfov sensitivity test" if spatial else "unknown; semantic only"
    focal = width / (2 * math.tan(math.radians(args.hfov) / 2))
    geometry = np.array([focal, focal, width / 2, height / 2, width, height, 0, 1, 0], np.float64)
    native = Native(args.library.resolve())
    decoder = {"version": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[:3],
               "executable_sha256": digest(shutil.which("ffmpeg")),
               "pipeline": "noautorotate; fps filter; limited-range BT.601 packed yuv420p"}
    runtime = {"python": platform.python_version(), "numpy": np.__version__, "opencv": cv2.__version__,
               "python_onnxruntime": ort.__version__, "detector_provider": "CPUExecutionProvider",
               "intra_op_num_threads": 2, "inter_op_num_threads": 1, "native_bridge_contract": 2,
               "platform": platform.platform(), "machine": platform.machine(),
               "python_ort_binaries": {p.name: digest(p) for p in sorted((Path(ort.__file__).parent / "capi").glob("*"))
                   if p.suffix.lower() in (".pyd", ".dll", ".so", ".dylib")}}
    provenance = inference_provenance(ROOT, args.video, ASSETS, args.library.resolve(), geometry.tolist(),
        args.fps, args.semantic_hz, spatial, runtime, decoder, Path(__file__))
    cache_key = inference_key(provenance)
    cache = args.output.parent / (("model-cache-v2-" if spatial else "semantic-cache-v2-") + cache_key + ".npz")
    tensor = np.empty((1, 3, 320, 320), np.float32)
    raw = np.empty(6472, np.float32)
    cache_existed = cache.exists()
    cached, cache_origin = load_cache(cache, provenance) if cache_existed and not args.fresh_inference else (None, None)
    fresh_inference_performed = cached is None
    outputs, detections, semantic_indices, inference_ms = [], [], [], []
    if cached is None:
        options = ort.SessionOptions(); options.intra_op_num_threads = 2; options.inter_op_num_threads = 1
        detector = ort.InferenceSession(str(ASSETS / "detector.onnx"), options, providers=["CPUExecutionProvider"])
        handle = native.create(str(ASSETS / "supercombo.onnx").encode() if spatial else b"", width, height,
                               ptr(geometry) if spatial else None)
        if not handle:
            raise RuntimeError("Native Supercombo initialization failed")
        last_semantic = -math.inf
        try:
            for index, frame in enumerate(frames(args.video, width, height, args.fps)):
                timestamp = index / args.fps
                started = time.perf_counter()
                if timestamp - last_semantic >= 1 / args.semantic_hz - 1e-9:
                    native.require(handle, native.prepare(handle, ptr(frame), ptr(tensor)))
                    detections.append(detector.run(None, {"images": tensor})[0][0])
                    semantic_indices.append(index); last_semantic = timestamp
                    if not spatial:
                        inference_ms.append((time.perf_counter() - started) * 1000)
                if spatial:
                    # Android computes semantic labels before current spatial inference.
                    spatial_started = time.perf_counter()
                    native.require(handle, native.frame(handle, ptr(frame), timestamp, ptr(raw)))
                    inference_ms.append((time.perf_counter() - spatial_started) * 1000)
                outputs.append(raw.copy() if spatial else np.empty(0, np.float32))
                if index % (args.fps * 5) == 0:
                    print(f"inferred {timestamp:.1f}s / {float(metadata['format']['duration']):.1f}s", flush=True)
        finally:
            native.destroy(handle)
        if not outputs or not detections:
            raise ValueError("Video has no decodable inference frames")
        save_cache(cache, provenance, args.output, replace_existing=args.fresh_inference, outputs=np.stack(outputs), detections=np.stack(detections),
                   semantic_indices=np.asarray(semantic_indices, dtype=np.int64), inference_ms=np.asarray(inference_ms))
        cached, cache_origin = load_cache(cache, provenance)
    outputs, detections = cached["outputs"], cached["detections"]
    semantic_indices = {int(i): n for n, i in enumerate(cached["semantic_indices"])}
    handle = native.create(b"", width, height, ptr(geometry) if spatial else None)
    if not handle:
        raise RuntimeError("Native policy initialization failed")
    native.emergencies(handle, int(args.research_emergencies))
    box_buffer, road, decision = np.empty(64 * 6, np.float32), np.empty(13, np.float64), np.empty(8, np.float64)
    trace = np.empty(22, np.float64)
    traces = []
    durations = {}
    for prompt in range(1, native.prompt_count() + 1):
        with wave.open(str(ASSETS / f"audio/prompt-{prompt}.wav"), "rb") as clip:
            durations[prompt] = clip.getnframes() / clip.getframerate()
    events, startup_events, observations, candidate_counts, publications = [], [], [], {}, []
    playing, playing_until, tick = 0, 0, 0
    latency = args.latency_ms / 1000
    def speed_at(t):
        return speed_timeline.at(t).speed_kmh
    def evaluate_until(deadline):
        nonlocal tick, playing, playing_until
        while tick / 100 < deadline - 1e-8:
            now = tick / 100
            if now >= playing_until:
                playing = 0
            speed_observation = speed_timeline.at(now - args.pre_roll_seconds)
            speed = speed_observation.speed_kmh
            native.tick(handle, now, speed if speed is not None else math.nan,
                        int(speed is not None and now >= args.pre_roll_seconds), playing, ptr(decision))
            native.trace(handle, ptr(trace))
            reason = native.guidance_reason(handle).decode()
            traces.append({"schema_version": 1, "event": "decision", "time": now, "source": "MP4",
                           "sequence": int(trace[0]), "source_id": int(trace[1]),
                           "prompt": int(trace[2]), "origin": int(trace[3]),
                           "candidates": [int(v) for v in trace[4:8]], "suppress": bool(trace[8]),
                           "eligible": bool(trace[9]), "blocked": int(trace[10]),
                           "capture_time": finite(trace[11]) if trace[11] >= 0 else None,
                           "scene_age": finite(trace[12]) if trace[11] >= 0 else None,
                           "semantic_available": bool(trace[13]), "spatial_available": bool(trace[14]),
                           "camera_actor": {"id": int(trace[15]), "category": int(trace[16]), "evidence_kind": int(trace[17]),
                                            "box": trace[18:22].tolist()} if trace[15] else None,
                           "reason": reason, "speed_source": speed_observation.source,
                           "speed_kmh": speed, "speed_sample_time": speed_observation.sample_time,
                           "speed_sample_age_seconds": speed_observation.age_seconds,
                           "research_emergencies": args.research_emergencies,
                           "calibration_origin": calibration_origin,
                           "recorded_time": now - args.pre_roll_seconds})
            selected = int(decision[0])
            if selected:
                candidate_counts[str(selected)] = candidate_counts.get(str(selected), 0) + 1
            if selected and decision[6] and decision[7]:
                is_dual = int(trace[3]) == 1
                event = {"time": round(now - args.pre_roll_seconds, 4), "prompt": selected, "text": native.prompt(selected).decode(),
                         "duration": durations[selected], "speed_kmh": speed,
                         "speed_source": speed_observation.source,
                         "speed_sample_time": speed_observation.sample_time,
                         "speed_sample_age_seconds": speed_observation.age_seconds,
                         "source": {1: "dual", 2: "tactical", 3: "terrain", 4: "camera"}.get(int(trace[3]), "none"),
                         "reason": reason,
                         "interrupted_prompt": playing}
                event["camera_actor"] = traces[-1]["camera_actor"]
                (events if now >= args.pre_roll_seconds else startup_events).append(event)
                native.accepted(handle, selected, now, int(is_dual))
                traces.append({"schema_version": 1, "event": "accepted", "time": now,
                               "source_id": int(trace[1]), "sequence": int(trace[0]), "prompt": selected})
                playing, playing_until = selected, now + durations[selected]
            tick += 1
    decoded = iter(frames(args.video, width, height, args.fps))
    decoded_index = -1
    semantic_capture_time = None
    schedule = publication_events(len(outputs), args.fps, semantic_indices, latency,
                                  args.spatial_extra_latency_ms / 1000, spatial)
    try:
        for available_time, _, index, stage in schedule:
            timestamp = index / args.fps
            evaluate_until(available_time + args.pre_roll_seconds)
            if stage == "semantic":
                while decoded_index < index:
                    try:
                        frame = next(decoded)
                    except StopIteration as error:
                        raise RuntimeError("Video ended before its verified inference cache") from error
                    decoded_index += 1
                native.require(handle, native.prepare(handle, ptr(frame), ptr(tensor)))
                values = np.ascontiguousarray(detections[semantic_indices[index]])
                native.require(handle, native.labels(handle, timestamp + args.pre_roll_seconds, ptr(values), values.size, values.shape[1]))
                semantic_capture_time = timestamp
            elif stage == "spatial":
                raw = np.ascontiguousarray(outputs[index])
                native.require(handle, native.cached_frame(handle, timestamp + args.pre_roll_seconds, ptr(raw)))
            if stage != "observation":
                publications.append({"schema_version": 1, "stage": stage, "frame_index": index,
                    "capture_time": timestamp, "completion_time": available_time, "publication_time": available_time,
                    "clock": "recording-relative seconds; simulated completion and immediate publication"})
                continue
            raw = np.ascontiguousarray(outputs[index])
            n = native.boxes(handle, ptr(box_buffer), box_buffer.size)
            native.road(handle, ptr(road))
            # Store decoded output fields separately from policy interpretation.
            mixture = int(raw[5755 + 51 + 48] > raw[5755 + 48]) if spatial else 0
            lead = raw[5755 + mixture * 51:] if spatial else None
            speed_observation = speed_timeline.at(available_time)
            observations.append({"time": timestamp, "available_time": available_time,
                "semantic_capture_time": semantic_capture_time,
                "speed_kmh": speed_observation.speed_kmh, "speed_source": speed_observation.source,
                "speed_sample_time": speed_observation.sample_time, "speed_sample_age_seconds": speed_observation.age_seconds,
                "spatial_enabled": spatial,
                "lead_probability": 1 / (1 + math.exp(-float(np.clip(raw[5857], -50, 50)))) if spatial else None,
                "lead_distance_m": finite(lead[0]) if spatial else None, "lead_lateral_m": finite(lead[1]) if spatial else None,
                "lead_absolute_speed_mps": finite(lead[2]) if spatial else None,
                "lead_std_m": math.exp(float(np.clip(lead[24], -20, 20))) if spatial else None,
                "lane_probabilities": [1 / (1 + math.exp(-float(np.clip(raw[5483 + i * 2 + 1], -50, 50)))) for i in range(4)] if spatial else None,
                "boxes": box_buffer[:n * 6].reshape(-1, 6).tolist(), "road": [finite(v) for v in road]})
        evaluate_until(len(outputs) / args.fps + latency + args.spatial_extra_latency_ms / 1000 + args.pre_roll_seconds)
    finally:
        decoded.close()
        native.destroy(handle)
    with (args.output / "decisions.jsonl").open("w", encoding="utf-8") as trace_file:
        for entry in traces:
            trace_file.write(json.dumps(entry, allow_nan=False) + "\n")
    with (args.output / "publications.jsonl").open("w", encoding="utf-8") as publication_file:
        for entry in publications:
            publication_file.write(json.dumps(entry, allow_nan=False) + "\n")
    ms = cached["inference_ms"]
    report = {"schema_version": 2, "video": str(args.video.resolve()), "sha256": digest(args.video), "metadata": metadata,
        "replay_bridge_contract": 2,
        "inference_cache": {"path": str(cache.resolve()), "key": cache_key,
            "present_at_start": cache_existed, "fresh_inference_performed": fresh_inference_performed,
            "verified_origin": cache_origin,
            "reused_from_another_output": cache_origin["created_for_output"] != str(args.output.resolve())},
        "guidance_coordinator": "ld_guidance_decide", "research_emergencies": args.research_emergencies,
        "review_tool_sha256": digest(Path(__file__)),
        "native_library_sha256": digest(args.library),
        "policy_source_sha256": {file: digest(ROOT / file) for file in ["core/src/c_api.cpp",
            "core/src/ReflexEngine.cpp", "core/src/ContextualPolicyEngine.cpp", "core/src/PriorityArbiter.cpp",
            "core/src/decision.cpp", "core/include/learndrive/decision.hpp", "core/include/learndrive/types.hpp"]},
        "assumptions": {"mode": args.mode, "horizontal_fov_degrees": args.hfov if spatial else None, "camera_intrinsics_measured": False,
            "gravity_assumed_level": spatial, "imu_available": False, "slope_guidance_enabled": False,
            "speed_source": speed_source,
            "manual_test_speed_kmh": args.test_speed_kmh,
            "speed_samples": speeds, "analysis_fps": args.fps, "simulated_inference_latency_ms": args.latency_ms,
            "recorded_speed_max_age_seconds": args.speed_max_age_seconds,
            "speed_clock": "recording-relative seconds; no interpolation or future sample backfill",
            "simulated_spatial_extra_latency_ms": args.spatial_extra_latency_ms,
            "publication_timing": "semantic before spatial for each capture; separate idealized completion/publication events",
            "semantic_max_hz": args.semantic_hz,
            "startup_before_video_seconds": args.pre_roll_seconds,
            "audio": "cached clip durations and native interruption policy; assumes granted audio focus"},
        "frames": len(outputs), "timing_scope": "Supercombo per analyzed frame" if spatial else "semantic detector on semantic frames only",
        "host_inference_ms": {"p50": float(np.percentile(ms, 50)),
            "p95": float(np.percentile(ms, 95)), "max": float(max(ms))},
        "events": events, "startup_events": startup_events, "candidate_tick_counts": candidate_counts,
        "limitations": ["No ground-truth depth, collision labels, calibrated optics or recorded IMU",
            "Headless shared-core replay, not a physical Android camera/audio test",
            "Idealized fixed-rate completion timeline; does not reproduce Android serial workload, frame drops, XNNPACK, GPS or navigation",
            "Completion delays are explicit test assumptions, not measured device stage timings",
            "Do not treat monocular emergency events as validated collision detections"]}
    (args.output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (args.output / "observations.json").write_text(json.dumps(observations, allow_nan=False) + "\n", encoding="utf-8")
    with (args.output / "guidance.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=["time", "prompt", "text", "duration", "speed_kmh", "speed_source",
            "speed_sample_time", "speed_sample_age_seconds", "source", "reason", "interrupted_prompt", "camera_actor"])
        writer.writeheader(); writer.writerows(events)
    for event in events:
        print(f"{event['time']:6.2f}s [{event['source']}] {event['text']}")
    print(f"{len(outputs)} frames; {len(events)} spoken events; {args.output / 'report.json'}")
    if args.render:
        render(args.video, args.output, observations, events, args.fps, latency)


if __name__ == "__main__":
    main()
