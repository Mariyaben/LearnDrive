"""Detector recall check without labels: full frame vs full frame + a zoomed pass ahead.

The bundled detector sees the whole frame letterboxed to 320 px, so a person or rider
100 m ahead is a few pixels tall. A second pass over a square crop centred on the road
ahead (half the frame width) doubles their size for the same model. This tool runs both
on a drive video and reports, per road-user group:

  * detections from the full frame, and confirmed zoom-only detections (same object seen
    by the zoom in >= 3 of 5 consecutive analysed frames, not by the full frame);
  * how much earlier objects are first detected with the zoom (chain back from the
    first full-frame detection through overlapping zoom detections);
  * inference time per pass, and annotated frames for visual review.

There are no ground-truth labels: "zoom-only" is not proof of a true detection, so review
the saved frames. It reuses the app's decoding rules (score >= 0.4, road-user classes,
class-wise NMS 0.45) and nearest-neighbour letterboxing like the native preprocessing.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {0: "person", 1: "two_wheeler", 3: "two_wheeler", 2: "vehicle", 5: "vehicle", 7: "vehicle"}


def probe(video: Path) -> tuple[int, int, float]:
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height:format=duration", "-of", "json", str(video)],
                         capture_output=True, text=True, check=True)
    data = json.loads(out.stdout)
    return int(data["streams"][0]["width"]), int(data["streams"][0]["height"]), float(data["format"]["duration"])


def frames(video: Path, width: int, height: int, fps: float):
    process = subprocess.Popen(["ffmpeg", "-v", "error", "-noautorotate", "-i", str(video), "-an", "-vf", f"fps={fps}",
                                "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"], stdout=subprocess.PIPE)
    size = width * height * 3
    try:
        while True:
            data = process.stdout.read(size)
            if len(data) < size:
                break
            yield np.frombuffer(data, np.uint8).reshape(height, width, 3)
    finally:
        process.stdout.close()
        process.wait()


def letterbox(image: np.ndarray, size: int = 320):
    h, w = image.shape[:2]
    scale = min(size / w, size / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    pad_x, pad_y = (size - w * scale) / 2, (size - h * scale) / 2
    canvas = np.full((size, size, 3), 114, np.uint8)
    x0, y0 = int(round(pad_x)), int(round(pad_y))
    canvas[y0:y0 + nh, x0:x0 + nw] = cv2.resize(image, (nw, nh), interpolation=cv2.INTER_NEAREST)[:size - y0, :size - x0]
    tensor = np.ascontiguousarray(canvas.transpose(2, 0, 1)[None].astype(np.float32) / 255)
    return tensor, scale, pad_x, pad_y


def decode(raw: np.ndarray, scale: float, pad_x: float, pad_y: float, offset=(0.0, 0.0)) -> list[tuple]:
    """Boxes in source-frame pixels: (x1, y1, x2, y2, score, class)."""
    scores = raw[4:84]
    cls = scores.argmax(0)
    best = scores[cls, np.arange(raw.shape[1])]
    keep = (best >= 0.4) & np.isin(cls, list(GROUPS))
    boxes = []
    for i in np.flatnonzero(keep):
        cx, cy, w, h = raw[0:4, i]
        x1, y1 = (cx - w / 2 - pad_x) / scale + offset[0], (cy - h / 2 - pad_y) / scale + offset[1]
        x2, y2 = (cx + w / 2 - pad_x) / scale + offset[0], (cy + h / 2 - pad_y) / scale + offset[1]
        boxes.append((float(x1), float(y1), float(x2), float(y2), float(best[i]), int(cls[i])))
    return boxes


def iou(a, b) -> float:
    w = max(0.0, min(a[2], b[2]) - max(a[0], b[0]))
    h = max(0.0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = w * h
    return inter / max(1e-9, (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter)


def nms(boxes: list[tuple]) -> list[tuple]:
    kept = []
    for box in sorted(boxes, key=lambda b: -b[4]):
        if all(not (k[5] == box[5] and iou(k, box) > 0.45) for k in kept):
            kept.append(box)
    return kept[:64]


def grown(box, factor: float):
    cx, cy, w, h = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, (box[2] - box[0]) * factor, (box[3] - box[1]) * factor
    return (cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)


def crop_rect(width: int, height: int, horizon: float) -> tuple[int, int, int]:
    """Square crop: half the long side (at most the short side), centred horizontally and on the horizon."""
    side = int(min(max(width, height) * 0.5, min(width, height)))
    x0 = (width - side) // 2
    y0 = int(np.clip(horizon * height - side / 2, 0, height - side))
    return x0, y0, side


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("video", type=Path)
    parser.add_argument("--fps", type=float, default=5)
    parser.add_argument("--horizon", type=float, default=0.5, help="horizon height as a fraction of the frame (crop centre)")
    parser.add_argument("--review-times", type=float, nargs="*", default=[], help="seconds to save annotated frames for")
    parser.add_argument("--output", type=Path, default=ROOT / "build/detector-eval")
    parser.add_argument("--detector", type=Path, default=ROOT / "android/app/src/main/assets/detector.onnx")
    args = parser.parse_args(argv)
    width, height, duration = probe(args.video)
    x0, y0, side = crop_rect(width, height, args.horizon)
    options = ort.SessionOptions()
    options.intra_op_num_threads, options.inter_op_num_threads = 2, 1
    session = ort.InferenceSession(str(args.detector), options, providers=["CPUExecutionProvider"])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "frames").mkdir(exist_ok=True)
    timeline, timing = [], {"full_ms": [], "zoom_ms": []}
    review = {round(t * args.fps) for t in args.review_times}
    for index, image in enumerate(frames(args.video, width, height, args.fps)):
        t = index / args.fps
        tensor, s, px, py = letterbox(image)
        started = time.perf_counter()
        full = nms(decode(session.run(None, {"images": tensor})[0][0], s, px, py))
        timing["full_ms"].append((time.perf_counter() - started) * 1000)
        ztensor, zs, zpx, zpy = letterbox(image[y0:y0 + side, x0:x0 + side])
        started = time.perf_counter()
        zoom = decode(session.run(None, {"images": ztensor})[0][0], zs, zpx, zpy, (x0, y0))
        timing["zoom_ms"].append((time.perf_counter() - started) * 1000)
        # Zoom boxes clipped by the crop edge are partial objects; the full frame covers them.
        zoom = [b for b in zoom if b[0] > x0 + 2 and b[2] < x0 + side - 2 and b[1] > y0 + 2 and b[3] < y0 + side - 2]
        merged = nms(full + zoom)
        zoom_only = [b for b in merged if all(not (GROUPS[f[5]] == GROUPS[b[5]] and iou(f, b) > 0.3) for f in full)]
        timeline.append({"t": t, "full": full, "merged": merged, "zoom_only": zoom_only})
        if index in review:
            canvas = cv2.cvtColor(image, cv2.COLOR_RGB2BGR).copy()
            cv2.rectangle(canvas, (x0, y0), (x0 + side, y0 + side), (255, 200, 0), 1)
            for b in full:
                cv2.rectangle(canvas, (int(b[0]), int(b[1])), (int(b[2]), int(b[3])), (0, 255, 0), 2)
            for b in zoom_only:
                cv2.rectangle(canvas, (int(b[0]), int(b[1])), (int(b[2]), int(b[3])), (0, 0, 255), 2)
            cv2.imwrite(str(args.output / "frames" / f"t{t:06.1f}.jpg"), canvas)
        if index % int(args.fps * 30) == 0:
            print(f"{t:.0f}/{duration:.0f} s", flush=True)

    # Confirmation: a zoom-only box counts when the same group overlaps (grown x1.5) in >= 3 of 5 frames around it.
    def present(i, box, key):
        return any(GROUPS[o[5]] == GROUPS[box[5]] and iou(grown(o, 1.5), grown(box, 1.5)) > 0 for o in timeline[i][key])
    stats = {g: {"full_detections": 0, "zoom_only": 0, "zoom_only_confirmed": 0, "zoom_only_confirmed_in_path": 0}
             for g in sorted(set(GROUPS.values()))}
    confirmed_examples, all_confirmed = [], []
    for i, row in enumerate(timeline):
        for b in row["full"]:
            stats[GROUPS[b[5]]]["full_detections"] += 1
        for b in row["zoom_only"]:
            g = GROUPS[b[5]]
            stats[g]["zoom_only"] += 1
            window = [j for j in range(max(0, i - 2), min(len(timeline), i + 3))]
            if sum(present(j, b, "merged") for j in window) >= 3:
                stats[g]["zoom_only_confirmed"] += 1
                all_confirmed.append({"t": row["t"], "box": [round(v, 1) for v in b[:4]], "score": round(b[4], 2), "class": b[5], "group": g})
                cx = (b[0] + b[2]) / 2 / width
                if 0.35 < cx < 0.65:
                    stats[g]["zoom_only_confirmed_in_path"] += 1
                    confirmed_examples.append((row["t"], b))
    # Earlier first detection: for each object newly seen by the full frame (no same-group
    # overlap in the previous second), chain back through overlapping merged detections.
    leads = []
    lookback = int(args.fps)
    for i, row in enumerate(timeline):
        for b in row["full"]:
            if any(present(j, b, "full") for j in range(max(0, i - lookback), i)):
                continue
            chain, j, misses = b, i - 1, 0
            earliest = i
            while j >= 0 and i - j <= args.fps * 4 and misses < 2:
                hits = [o for o in timeline[j]["merged"] if GROUPS[o[5]] == GROUPS[b[5]] and iou(grown(o, 1.6), grown(chain, 1.6)) > 0]
                if hits:
                    chain, earliest, misses = max(hits, key=lambda o: iou(grown(o, 1.6), grown(chain, 1.6))), j, 0
                else:
                    misses += 1
                j -= 1
            leads.append((i - earliest) / args.fps)
    leads.sort()
    summary = {
        "video": str(args.video), "frames": len(timeline), "analysis_fps": args.fps,
        "crop": {"x": x0, "y": y0, "side": side, "zoom": round(max(width, height) / side, 2)},
        "per_group": stats,
        "new_objects_seen_by_full_frame": len(leads),
        "seen_earlier_with_zoom": sum(lead > 0 for lead in leads),
        "median_lead_seconds_when_earlier": float(np.median([l for l in leads if l > 0])) if any(l > 0 for l in leads) else 0.0,
        "lead_ge_1s": sum(lead >= 1 for lead in leads),
        "full_ms_median": float(np.median(timing["full_ms"])), "zoom_ms_median": float(np.median(timing["zoom_ms"])),
        "meaning": "Unlabelled comparison: zoom-only detections still need visual review; host CPU timing, not phone timing",
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    examples = sorted(confirmed_examples, key=lambda e: e[1][3] - e[1][1])[:12]
    (args.output / "zoom_only_in_path.json").write_text(json.dumps(
        [{"t": t, "box": [round(v, 1) for v in b[:4]], "score": round(b[4], 2), "class": b[5]} for t, b in examples], indent=2), encoding="utf-8")
    (args.output / "zoom_only_confirmed.json").write_text(json.dumps(all_confirmed, indent=1), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
