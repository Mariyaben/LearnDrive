"""Batch real-video coaching evaluation: 0.7.1 policy vs task-aware scheduling.

Runs tools/review_video.py (real detector ONNX + shared native core, semantic mode,
same as Android MP4 mode) for every clip and both policies, then aggregates
full-interval delivery metrics. Assumptions, stated in every row:
- speed is an assumed constant manual test input, NOT recorded vehicle speed;
- navigation overlays are SYNTHETIC (turn times chosen by the evaluator);
- playback is simulated from recorded prompt durations; nothing is audible;
- no instructor labels; local research clips are evaluated only, never exported.
"""
import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CLIPS = sorted((ROOT / "build/public-traffic/clips").glob("*.mp4")) + \
    sorted((ROOT / "build/public-traffic-extension/clips").glob("*.mp4")) + \
    sorted((ROOT / "build/traffic-review-2026-10-22/clips").glob("*.mp4"))


def run(clip, out, profile, scheduling, speed, navigation=None, render=False):
    command = [sys.executable, str(ROOT / "tools/review_video.py"), str(clip), "--mode", "semantic",
               "--coaching-profile", profile, "--automatic", "--scheduling", scheduling,
               "--test-speed-kmh", str(speed), "--output", str(out)]
    if navigation:
        command += ["--navigation", str(navigation)]
    if render:
        command += ["--render"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{clip.name}: {result.stderr[-2000:]}")
    return json.loads((out / "report.json").read_text(encoding="utf-8"))


def metrics(report):
    events, cut = report["events"], report.get("interruptions", [])
    duration = float(report["metadata"]["format"]["duration"])
    texts = [e["text"] for e in events]
    repeats = sum(1 for i, e in enumerate(events) for f in events[:i]
                  if f["text"] == e["text"] and e["time"] - f["time"] <= 10)
    drive = report.get("final_drive") or {}
    junction = drive.get("junction", {})
    return {"duration_s": duration, "spoken": len(events), "per_minute": round(len(events) * 60 / duration, 2),
            "interrupted": len(cut), "interrupted_reasons": dict(Counter(c["reason"] for c in cut)),
            "mean_fraction_spoken_when_interrupted": round(sum(c["spoken_fraction"] for c in cut) / len(cut), 3) if cut else None,
            "same_phrase_within_10s": repeats, "phrases": dict(Counter(texts)),
            "junction_planning_delivered": junction.get("planning_deliveries"),
            "junction_approach_delivered": junction.get("approach_deliveries"),
            "execution_quiet_holds": junction.get("execution_quiet_holds"),
            "timeline": [[e["time"], e["text"]] for e in events] + [[c["time"], "INTERRUPTED: " + c["text"]] for c in cut]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clips", nargs="*", type=Path, default=DEFAULT_CLIPS)
    parser.add_argument("--profiles", nargs="*", default=["balanced"])
    parser.add_argument("--speed-kmh", type=float, default=30)
    parser.add_argument("--output", type=Path, default=ROOT / "build/video-coaching-eval")
    parser.add_argument("--junction-clips", nargs="*", type=Path, default=[
        ROOT / "build/public-traffic/clips/udacity-curves.mp4",
        ROOT / "build/public-traffic-extension/clips/bangalore-cv-raman.mp4"])
    parser.add_argument("--jobs", type=int, default=3, help="Clips evaluated concurrently")
    parser.add_argument("--render", nargs="*", type=Path, default=[], help="Clips to render as captioned review MP4 (scheduling on)")
    args = parser.parse_args()
    rows = []
    overlays = {}
    for name, turn in {"junction": {"turns": [{"at": 46, "maneuver": "left", "appear_lead_s": 45}]},
                       "junction_gps_dropout": {"turns": [{"at": 46, "maneuver": "left", "appear_lead_s": 45}],
                                                "unavailable": [[39.5, 47]]}}.items():
        path = args.output / "overlays" / f"{name}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(turn), encoding="utf-8")
        overlays[name] = path
    jobs = [(clip, profile, None) for clip in args.clips for profile in args.profiles]
    jobs += [(clip, profile, name) for clip in args.junction_clips for profile in ("minimal", "balanced", "guided")
             for name in overlays]
    render = {p.resolve() for p in args.render}

    def evaluate(job):
        clip, profile, overlay = job
        pair = {}
        for scheduling in ("off", "on"):
            out = args.output / "runs" / f"{clip.stem}-{profile}-{overlay or 'no_route'}-{scheduling}"
            report = run(clip, out, profile, scheduling, args.speed_kmh, overlays.get(overlay),
                         render=scheduling == "on" and clip.resolve() in render)
            pair[scheduling] = metrics(report)
        print(f"{clip.name} {profile} {overlay or 'no_route'}: "
              f"0.7.1 spoken={pair['off']['spoken']} cut={pair['off']['interrupted']} | "
              f"0.7.2 spoken={pair['on']['spoken']} cut={pair['on']['interrupted']}", flush=True)
        return {"clip": clip.name, "profile": profile, "navigation_overlay": overlay or "none",
                "policy_0_7_1": pair["off"], "policy_0_7_2": pair["on"]}

    # Jobs on the same clip share one inference cache: run them in one worker.
    by_clip = {}
    for job in jobs:
        by_clip.setdefault(job[0], []).append(job)
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for group in pool.map(lambda group: [evaluate(job) for job in group], by_clip.values()):
            rows.extend(group)
    def total(key, policy, select=lambda r: True):
        return sum(r[policy][key] or 0 for r in rows if select(r))
    summary = {"scope": "real video, real detector, shared native core; assumed constant speed; synthetic navigation overlays; "
                        "simulated playback; no instructor labels; not a phone or audible test",
               "assumed_speed_kmh": args.speed_kmh, "rows": rows,
               "totals": {policy: {k: total(k, policy) for k in ("spoken", "interrupted", "same_phrase_within_10s",
                                                                  "junction_planning_delivered", "junction_approach_delivered")}
                          for policy in ("policy_0_7_1", "policy_0_7_2")}}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary["totals"], indent=2))


if __name__ == "__main__":
    main()
