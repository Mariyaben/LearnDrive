"""Check Balanced video replay delivery contracts, never coaching accuracy."""
import argparse
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(report, rows):
    assumptions = report["assumptions"]
    require(report["schema_version"] == 2 and report["replay_bridge_contract"] == 2, "current video contract required")
    require(assumptions["coaching_profile"] == "balanced", "Balanced report required")
    require(assumptions["mode"] == "semantic" and not assumptions["imu_available"], "uncalibrated semantic review required")
    require(not report["research_emergencies"], "critical commands must remain off")
    decisions, accepted = {}, set()
    routine_at, count, warnings, max_age = -math.inf, 0, 0, 0.
    forbidden = {1, 4, 13, 14, 15, 17, 19, 22, 23, 24, 26, 30, 42, 43}
    for row in rows:
        if row["event"] == "decision":
            key = row["source_id"], row["sequence"]
            require(key not in decisions, "duplicate decision identity")
            decisions[key] = row
            require(not row["spatial_available"], "uncalibrated video claimed spatial evidence")
            require(row["coaching"]["speed_trend"] == 0, "recording setting claimed measured driver response")
            if row["eligible"]:
                age = row["time"] - row["capture_time"]
                require(0 <= age < .35, "eligible cue has stale/future original capture")
                require(abs(age - row["scene_age"]) < 1e-6, "capture-age provenance mismatch")
        elif row["event"] == "coaching_accepted":
            key = row["source_id"], row["sequence"]
            decision = decisions.get(key)
            require(key not in accepted, "duplicate logical acceptance")
            require(decision and decision["eligible"] and row["prompt"] == decision["prompt"], "acceptance lacks exact eligible decision")
            require(row["settings_revision"] == decision["settings_revision"], "settings revision mismatch")
            require(row["prompt"] not in forbidden, "unsupported mechanical/critical instruction")
            age = row["time"] - decision["capture_time"]
            require(0 <= age < .35, "accepted original evidence expired")
            require(not row["audible_playback_confirmed"], "simulation claimed audibility")
            require(row["simulated_validated_onset"], "speech event lacks validated simulated onset")
            mode = decision["coaching"]["mode"]
            require(mode in (1, 2), "accepted silence/deferred decision")
            if mode == 1:
                require(row["time"] - routine_at >= 12 - 1e-8, "ordinary interval bypassed")
                routine_at = row["time"]
            else:
                warnings += 1
            accepted.add(key)
            count += 1
            max_age = max(max_age, age)
    require(count == len(report["events"]) + len(report["startup_events"]), "scheduled speech and logical acknowledgments differ")
    return {"decisions": len(decisions), "logical_accepted": count, "validated_warnings": warnings,
            "max_accepted_capture_age_seconds": max_age if count else None,
            "instructor_quality": "unavailable", "physical_audio": "not tested"}


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--verify-artifacts", action="store_true")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    runs = []
    for path in args.reports:
        report = json.loads(path.read_text(encoding="utf-8"))
        if args.verify_artifacts:
            require(digest(report["video"]) == report["sha256"], "video hash changed")
            require(digest(ROOT / "build/native-spatial/liblearndrive_replay.dll") == report["native_library_sha256"], "native library hash changed")
            require(digest(ROOT / "tools/review_video.py") == report["review_tool_sha256"], "review tool hash changed")
            for name, value in report["policy_source_sha256"].items():
                require(digest(ROOT / name) == value, "source hash changed: " + name)
        rows = [json.loads(line) for line in (path.parent / "decisions.jsonl").read_text(encoding="utf-8").splitlines()]
        result = inspect(report, rows)
        result.update(clip=path.parent.name, report_sha256=digest(path), video_sha256=report["sha256"], frames=report["frames"])
        runs.append(result)
    output = {"schema_version": 1, "status": "software_contracts_passed", "clips": len(runs),
              "logical_accepted": sum(r["logical_accepted"] for r in runs), "runs": runs,
              "limits": "Development corpus; no instructor labels, accuracy score, qualified sensor state or phone audio/thermal evidence"}
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(output["clips"], "clips;", output["logical_accepted"], "logical cues; software contracts passed")


if __name__ == "__main__":
    main()
