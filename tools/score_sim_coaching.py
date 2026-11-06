"""Score LearnDrive coaching on a simulated drive against simulator ground truth.

Inputs: truth.json from tools/simulate_drive.py and report.json from
tools/review_video.py run on that drive (with --speeds and --navigation).
Everything here is SIMULATED: the expert driver is not a learner, and the
rendering is synthetic. Scores measure timing and evidence agreement, not
teaching quality.
"""
import argparse
import json
from bisect import bisect_left
from pathlib import Path

ROAD_USER = ("Road user ahead", "Person ahead", "Two-wheeler ahead", "vehicle", "Vehicle")


def score(truth, report):
    steps = truth["steps"]
    times = [s["time"] for s in steps]

    def state(t):
        i = min(max(bisect_left(times, t), 0), len(steps) - 1)
        return steps[i]

    events = report["events"]
    cuts = report.get("interruptions", [])
    turns = [j for j in truth["junctions"] if j["maneuver"] in ("left", "right") and j["kind"] != "roundabout"]
    junction_rows = []
    for j in turns:
        before = [e for e in events if j["enter"] - 45 <= e["time"] <= j["exit"]]
        planning = [e for e in before if "mirrors" in e["text"] and "turn ahead" in e["text"].lower() or "Junction ahead" in e["text"]]
        approach = [e for e in before if "approach speed" in e["text"] or "Change down to second" in e["text"]]
        entry = [e for e in before if "Look both ways" in e["text"]]
        exit_cue = [e for e in events if j["enter"] <= e["time"] <= j["exit"] + 8 and "Straighten the wheel" in e["text"]]
        lesson = set(x["text"] for x in planning + approach + entry + exit_cue)
        quiet = [e for e in before if j["enter"] - 8 <= e["time"] <= j["exit"] and e["text"] not in lesson]
        junction_rows.append({"enter": j["enter"], "maneuver": j["maneuver"], "kind": j["kind"],
                              "planning_lead_s": round(j["enter"] - planning[0]["time"], 1) if planning else None,
                              "approach_lead_s": round(j["enter"] - approach[0]["time"], 1) if approach else None,
                              "approach_text": approach[0]["text"] if approach else None,
                              "entry_lead_s": round(j["enter"] - entry[0]["time"], 1) if entry else None,
                              "entry_repeats": max(0, len(entry) - 1),
                              "exit_after_s": round(exit_cue[0]["time"] - j["enter"], 1) if exit_cue else None,
                              "routine_speech_in_execution": [e["text"] for e in quiet]})
    cue_rows = []
    for e in events:
        if not any(k in e["text"] for k in ROAD_USER):
            continue
        s = state(e["time"])
        ahead = [o for o in s["traffic"] if 0 < o["forward_m"] < 35 and abs(o["lateral_m"]) < 3.5]
        cue_rows.append({"time": e["time"], "text": e["text"], "vehicle_ahead_within_35m": bool(ahead),
                         "nearest_ahead_m": min((o["forward_m"] for o in ahead), default=None)})
    # Opportunities: a vehicle within 20 m ahead in-lane for >= 2 s (presence only, not a judgement of need).
    episodes, start = [], None
    for s in steps:
        near = any(0 < o["forward_m"] < 20 and abs(o["lateral_m"]) < 2.5 for o in s["traffic"])
        if near and start is None:
            start = s["time"]
        elif not near and start is not None:
            if s["time"] - start >= 2:
                episodes.append((start, s["time"]))
            start = None
    covered = sum(1 for a, b in episodes if any(a - 2 <= c["time"] <= b for c in cue_rows))
    duration = steps[-1]["time"] if steps else 0
    return {"scope": "SIMULATED drive (MetaDrive, expert IDM driver, synthetic rendering); not instructor-validated",
            "duration_s": duration, "spoken": len(events), "per_minute": round(len(events) * 60 / duration, 2) if duration else None,
            "interrupted": len(cuts), "junctions": junction_rows,
            "road_user_cues": len(cue_rows), "road_user_cues_with_vehicle_ahead": sum(r["vehicle_ahead_within_35m"] for r in cue_rows),
            "close_following_episodes": len(episodes), "close_following_episodes_with_cue": covered,
            "cue_detail": cue_rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("truth", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(score(json.loads(args.truth.read_text()), json.loads(args.report.read_text())), indent=1))


if __name__ == "__main__":
    main()
