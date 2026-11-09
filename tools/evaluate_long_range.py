"""How far ahead does the app's detector see road users? Old versus new camera pipeline on a drive.

Runs the app's own native detector mosaic (through the replay bridge) and the bundled detector on a
drive video, once per configuration, and reports for every detected road user its distance from
its apparent height (person or rider 1.6 m, car 1.5 m, bus or truck 3.0 m tall) through the filmed
image's optics. No ground truth: distances are estimates, and boxes should be reviewed on the saved
frames. Configurations are VIDEO:MODE:HORIZON with an optional fourth field "plain" (no mosaic):

  --config 0.8.1=build/drive-tests/nov8/camera_640.mp4:1:0.81
  --config 0.8.3=build/drive-tests/nov8/camera.mp4:2:0.70

MODE 1 is the 0.8.0 mosaic (one centred 2x view), 2 the 0.8.3 long-range mosaic (2x and 4x views
aimed along the road by the video's own turn rate). Reported per configuration:
  * road users boxed beyond 30 / 50 / 70 m over the whole drive (fps sampled);
  * for each --event NAME=START-END[:CLASSES[:X0-X1]] window, the first time a matching box appears,
    with its estimated distance, and annotated frames of that window.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import math
import sys
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

sys.path.insert(0, str(Path(__file__).resolve().parent))
from review_video import frames, video_yaw  # noqa: E402  (the replay's own decoder and turn-rate estimate)

ROOT = Path(__file__).resolve().parents[1]
P = C.c_void_p
TALL = {0: 1.6, 1: 1.6, 3: 1.6, 2: 1.5, 5: 3.0, 7: 3.0}
GROUP = {0: "person", 1: "two-wheeler", 3: "two-wheeler", 2: "vehicle", 5: "vehicle", 7: "vehicle"}
NAMES = {0: "person", 1: "bicycle", 2: "car", 3: "motorcycle", 5: "bus", 7: "truck"}


def bridge(path: Path):
    lib = C.CDLL(str(path))
    sig = {"replay_create": ([C.c_char_p, C.c_int, C.c_int, P], P), "replay_destroy": ([P], None),
           "replay_error": ([P], C.c_char_p), "replay_camera_horizon": ([P, C.c_double], None),
           "replay_camera_yaw": ([P, C.c_double, C.c_double], None), "replay_mosaic_mode": ([P, C.c_int], None),
           "replay_camera_geometry": ([P, C.c_double, C.c_double, C.c_double, C.c_double], None),
           "replay_mosaic_plan": ([P, C.c_double, P], C.c_int), "replay_prepare_mosaic_plan": ([P, P, P, P], C.c_int),
           "replay_prepare": ([P, P, P], C.c_int),
           "replay_labels": ([P, C.c_double, P, C.c_size_t, C.c_int], C.c_int), "replay_boxes": ([P, P, C.c_size_t], C.c_size_t)}
    for name, (args, res) in sig.items():
        f = getattr(lib, name); f.argtypes, f.restype = args, res
    return lib


def ptr(a):
    return a.ctypes.data_as(P)


def probe(video: Path):
    cap = cv2.VideoCapture(str(video))
    w, h, n, fps = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)), int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)), cap.get(cv2.CAP_PROP_FRAME_COUNT), cap.get(cv2.CAP_PROP_FPS)
    cap.release()
    return w, h, (n / fps if fps else 0)


def run(name, spec, args, detector, lib, events):
    parts = spec.split(":")
    video, mode = Path(parts[0]), int(parts[1])
    try:
        track = (np.array([0.0, 1e9]), np.array([float(parts[2])] * 2))
    except ValueError:  # a CSV time,horizon track: the mount tilts or sags during a drive
        rows = [line.split(",") for line in Path(parts[2]).read_text(encoding="utf-8").splitlines()[1:] if line.strip()]
        track = (np.array([float(r[0]) for r in rows]), np.array([float(r[1]) for r in rows]))
    horizon = parts[2]
    plain = len(parts) > 3 and parts[3] == "plain"
    width, height, duration = probe(video)
    handle = lib.replay_create(b"", width, height, None)
    if not handle:
        raise RuntimeError("replay bridge refused the video size")
    lib.replay_camera_horizon(handle, float(np.interp(0, *track)))
    lib.replay_mosaic_mode(handle, mode)
    if args.camera_geometry:
        lib.replay_camera_geometry(handle, *args.camera_geometry)
    yaw_t, yaw_r = video_yaw(video) if mode == 2 else (np.zeros(0), np.zeros(0))
    tensor = np.empty((1, 3, 320, 320), np.float32); plan = np.empty(8, np.float32); out = np.empty(64 * 6, np.float32)
    fy = args.camera_geometry[1] if args.camera_geometry else .96
    detections, saved = [], {}
    wanted = {e["name"]: e for e in events}
    outdir = args.output / name; (outdir / "frames").mkdir(parents=True, exist_ok=True)
    try:
        for index, frame in enumerate(frames(video, width, height, args.fps)):
            t = index / args.fps
            in_event = [e for e in events if e["start"] - 1 <= t <= e["end"]]
            if index % args.whole_drive_stride and not in_event:
                continue
            lib.replay_camera_horizon(handle, float(np.interp(t, *track)))
            if plain:
                ok = lib.replay_prepare(handle, ptr(frame), ptr(tensor))
            else:
                if len(yaw_t):
                    lib.replay_camera_yaw(handle, t, float(np.interp(t, yaw_t, yaw_r)))
                ok = lib.replay_mosaic_plan(handle, t, ptr(plan)) and lib.replay_prepare_mosaic_plan(handle, ptr(frame), ptr(tensor), ptr(plan))
            if not ok:
                raise RuntimeError(lib.replay_error(handle).decode())
            raw = np.ascontiguousarray(detector.run(None, {"images": tensor})[0][0], np.float32)
            if not lib.replay_labels(handle, t, ptr(raw), raw.size, raw.shape[1]):
                raise RuntimeError(lib.replay_error(handle).decode())
            n = lib.replay_boxes(handle, ptr(out), out.size)
            boxes = []
            for k in range(n):
                x, y, w, h, score, cls = out[k * 6:k * 6 + 6]; cls = int(cls)
                if cls not in TALL or h <= 0:
                    continue
                clipped = y < .01 or y + h > .99
                boxes.append({"x": float(x), "y": float(y), "w": float(w), "h": float(h), "score": round(float(score), 3), "class": cls,
                              "distance_m": None if clipped else round(fy * TALL[cls] / float(h), 1)})
            detections.append({"t": round(t, 3), "boxes": boxes, "event": [e["name"] for e in in_event],
                               "grid": index % args.whole_drive_stride == 0,
                               "plan": [round(float(v), 3) for v in plan] if not plain else None})
            for e in in_event:
                if args.save_frames:
                    yuv = frame.reshape(height * 3 // 2, width)
                    img = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_I420)
                    for b in boxes:
                        p1 = (int(b["x"] * width), int(b["y"] * height)); p2 = (int((b["x"] + b["w"]) * width), int((b["y"] + b["h"]) * height))
                        cv2.rectangle(img, p1, p2, (0, 255, 255), 2)
                        label = f'{NAMES[b["class"]]} {b["score"]:.2f}' + (f' {b["distance_m"]:.0f}m' if b["distance_m"] else "")
                        cv2.putText(img, label, (p1[0], max(12, p1[1] - 4)), cv2.FONT_HERSHEY_SIMPLEX, .45, (0, 255, 255), 1, cv2.LINE_AA)
                    cv2.putText(img, f"{name}  {int(t // 60):02d}:{t % 60:05.2f}", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, .7, (0, 0, 255), 2, cv2.LINE_AA)
                    cv2.imwrite(str(outdir / "frames" / f"{e['name']}_{t:07.2f}.jpg"), img)
            if index % int(args.fps * 60) == 0:
                print(f"  {name}: {t:.0f}/{duration:.0f} s", flush=True)
    finally:
        lib.replay_destroy(handle)
    whole = [b for d in detections if d["grid"] for b in d["boxes"]]  # the whole drive, evenly sampled
    far = {g: {m: sum(1 for b in whole if GROUP[b["class"]] == g and b["distance_m"] and b["distance_m"] >= m) for m in (30, 50, 70)}
           for g in ("person", "two-wheeler", "vehicle")}
    firsts = {}
    for e in events:
        hits = [(d["t"], b) for d in detections if e["start"] <= d["t"] <= e["end"] for b in d["boxes"]
                if b["class"] in e["classes"] and e["x0"] <= b["x"] + b["w"] / 2 <= e["x1"]]
        firsts[e["name"]] = ({"t": hits[0][0], "class": NAMES[hits[0][1]["class"]], "distance_m": hits[0][1]["distance_m"],
                              "box_px_height": round(hits[0][1]["h"] * height, 1), "score": hits[0][1]["score"]} if hits else None)
    summary = {"config": name, "video": str(video), "frame": [width, height], "mosaic_mode": None if plain else mode,
               "horizon": horizon, "boxed_beyond_m": far, "first_detection": firsts}
    (outdir / "detections.json").write_text(json.dumps(detections), encoding="utf-8")
    (outdir / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", action="append", required=True, help="NAME=VIDEO:MODE:HORIZON[:plain]")
    ap.add_argument("--event", action="append", default=[], help="NAME=START-END[:CLASSES[:X0-X1]] in seconds; classes like 0,1,3")
    ap.add_argument("--camera-geometry", type=lambda s: [float(v) for v in s.split(",")],
                    help="filmed image optics as fractions: fx,fy,cx,cy (0.8.1 S24 Ultra preview: 0.665,1.228,0.5,0.306)")
    ap.add_argument("--fps", type=float, default=8)
    ap.add_argument("--whole-drive-stride", type=int, default=2, help="outside events, analyse every Nth frame")
    ap.add_argument("--save-frames", action="store_true")
    ap.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_replay.dll")
    ap.add_argument("--detector", type=Path, default=ROOT / "android/app/src/main/assets/detector.onnx")
    ap.add_argument("--output", type=Path, default=ROOT / "build/long-range")
    args = ap.parse_args(argv)
    events = []
    for spec in args.event:
        name, rest = spec.split("=", 1); parts = rest.split(":")
        a, b = (float(v) for v in parts[0].split("-"))
        classes = [int(c) for c in parts[1].split(",")] if len(parts) > 1 and parts[1] else list(TALL)
        x0, x1 = (float(v) for v in parts[2].split("-")) if len(parts) > 2 else (0.0, 1.0)
        events.append({"name": name, "start": a, "end": b, "classes": classes, "x0": x0, "x1": x1})
    options = ort.SessionOptions(); options.intra_op_num_threads = 4
    detector = ort.InferenceSession(str(args.detector), options, providers=["CPUExecutionProvider"])
    lib = bridge(args.library.resolve())
    summaries = [run(*c.split("=", 1), args, detector, lib, events) for c in args.config]
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "comparison.json").write_text(json.dumps(summaries, indent=1), encoding="utf-8")
    for s in summaries:
        print(f"\n{s['config']} ({s['frame'][0]}x{s['frame'][1]}, mosaic {s['mosaic_mode']}): boxed beyond 30/50/70 m: "
              + "; ".join(f"{g} {v[30]}/{v[50]}/{v[70]}" for g, v in s["boxed_beyond_m"].items()))
        for e, f in s["first_detection"].items():
            print(f"  {e:14s} " + (f"first at {f['t']:.2f} s ({int(f['t'] // 60):02d}:{f['t'] % 60:05.2f}) {f['class']} ~{f['distance_m']} m, {f['box_px_height']} px" if f else "not detected"))


if __name__ == "__main__":
    main()
