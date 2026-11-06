"""Run/audit the learner camera development regressions; not an accuracy benchmark."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from test_public_traffic import ROOT, audit_report


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "build/learner-coaching")
    parser.add_argument("--tag", default="r3")
    parser.add_argument("--run", action="store_true", help="Execute replays first; requires the downloaded development clips")
    args = parser.parse_args()
    matrix = json.loads((ROOT / "scenarios/learner-coaching/matrix.json").read_text())
    results = []
    for item in matrix:
        folder = args.root / "runs" / f"{item['id']}-{args.tag}"
        if args.run:
            folder.mkdir(parents=True, exist_ok=True)
            command = [sys.executable, str(ROOT / "tools/review_video.py"), str(ROOT / item["video"]),
                       "--mode", item["mode"], "--fps", str(item["fps"]), "--semantic-hz", str(item["hz"]),
                       "--latency-ms", str(item["delay"]), "--pre-roll-seconds", "6", "--test-speed-kmh", str(item["speed"]),
                       "--output", str(folder)]
            (folder / "command.json").write_text(json.dumps(command, indent=2))
            subprocess.run(command, cwd=ROOT, check=True)
        report = json.loads((folder / "report.json").read_text())
        assert report["native_library_sha256"] == digest(ROOT / "build/native-spatial/liblearndrive_replay.dll"), "Stale native replay"
        assert report["review_tool_sha256"] == digest(ROOT / "tools/review_video.py"), "Stale replay tool"
        for source, expected in report["policy_source_sha256"].items():
            assert digest(ROOT / source) == expected, f"Stale policy: {source}"
        audit = audit_report(folder, item["mode"], item["speed"])
        duration = float(report["metadata"]["format"]["duration"])
        decisions = [json.loads(line) for line in (folder / "decisions.jsonl").read_text().splitlines()]
        window = [row for row in decisions if row["event"] == "decision" and 6.35 <= row["time"] < 6 + duration]
        for event in report["events"]:
            if event["source"] == "camera":
                actor = event.get("camera_actor")
                assert actor and actor["id"] > 0 and len(actor["box"]) == 4, "Unattributed camera instruction"
                assert event["prompt"] not in {6, 16, 17, 24, 25, 26}, "Box-only obstacle/gear wording returned"
                if event["prompt"] == 44:
                    assert actor["category"] == 0 and event["text"] == "Person ahead. Give space"
                if event["prompt"] == 45:
                    assert actor["category"] in {1, 3}
                if event["prompt"] == 46:
                    assert actor["category"] in {2, 5, 7}
            assert event["interrupted_prompt"] == 0, "Ordinary lesson was interrupted in the regression corpus"
        results.append({"id": item["id"], "folder": str(folder.relative_to(args.root)), "source_duration_seconds": duration,
                        "assumptions": report["assumptions"], "native_library_sha256": report["native_library_sha256"],
                        "semantic_available_fraction": sum(row["semantic_available"] for row in window) / len(window),
                        "decision_window_ticks": len(window), "reason_counts": dict(collections.Counter(row["reason"] for row in window)), **audit})
    by_id = {row["id"]: row for row in results}
    # Negative regressions plus positive preservation: silence alone cannot pass.
    assert any(event["prompt"] == 45 and 0 <= event["time"] <= 3 for event in by_id["jodhpur-clear-semantic-hz8-delay224"]["events"]), "Lost the known early two-wheeler lesson"
    assert any(event["prompt"] == 46 and 3.5 <= event["time"] <= 6.5 for event in by_id["jodhpur-clear-semantic-hz8-delay224"]["events"]), "Lost attention for the reviewed entering vehicle"
    assert any(event["prompt"] == 46 and 0 <= event["time"] <= 3 for event in by_id["bangalore-semantic-hz8-delay224"]["events"]), "Lost sustained vehicle attention"
    assert not by_id["udacity-challenge-semantic-hz8-delay224"]["events"], "Opposing-traffic regression returned"
    assert not by_id["udacity-curves-semantic-hz8-delay224"]["events"], "False-road-box/oncoming regression returned; review actor evidence"
    assert by_id["bangalore-semantic-hz8-delay224"]["semantic_available_fraction"] > .9
    assert by_id["bangalore-semantic-hz8-delay224"]["semantic_available_fraction"] > by_id["bangalore-semantic-hz4-delay224"]["semantic_available_fraction"]
    assert by_id["bangalore-semantic-hz8-delay500"]["semantic_available_fraction"] == 0
    assert not by_id["bangalore-semantic-hz8-delay500"]["events"]
    aggregate = {"schema_version": 1, "date": "2026-10-17", "replay_contracts": "pass", "known_development_regressions": "pass",
                 "learner_acceptance": "pending independent instructor labels and supervised learner/device evaluation",
                 "frame_steps": sum(row["frames"] for row in results), "runs": results,
                 "voice_sha256": {str(i): digest(ROOT / f"android/app/src/main/assets/audio/prompt-{i}.wav") for i in (44, 45, 46)},
                 "limitations": ["Development data, not held-out accuracy", "No instructor-approved usefulness or collision/actor ground truth",
                     "Fixed delay and assumed speed do not qualify Android runtime", "Spatial run uses assumed optics/level pose",
                     "Silence on uncalibrated bends is not bend acceptance", "Rain clip ends immediately after its second cue; its continuation cannot be reviewed",
                     "Generic person labels may be riders; no claim of walking",
                     "The 5.51s Jodhpur vehicle cue box was visually checked against the crossing white car; crossing intent, collision risk and optimal timing are not validated"]}
    args.root.mkdir(parents=True, exist_ok=True)
    (args.root / "aggregate.json").write_text(json.dumps(aggregate, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"{len(results)} replay audits and known development regressions passed; learner acceptance remains pending")
    for row in results:
        print(row["id"], row["spoken_events"], f"speech={100*row['speech_fraction']:.2f}%", f"semantic freshness={100*row['semantic_available_fraction']:.2f}%")


if __name__ == "__main__":
    main()
