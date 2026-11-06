"""Replay existing recorded-perception snapshots through automatic drive policy.

No downloads, new detections, training export, instructor labels or phone/audio
claims. Input measurements stay unchanged; visual acceptance exercises policy.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path

from replay_coaching import NativeBridge, decode_snapshot


def evaluate(path, library, profile, automatic):
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if payload.get("evidence_kind") != "recorded_native_perception":
        raise ValueError("Expected original recorded-perception snapshots")
    bridge = NativeBridge(library)
    accepted = []
    try:
        assert bridge.configure({"profile": profile, "voice": False, "revision": 1})
        enable = bridge.lib.coaching_replay_automatic
        enable.argtypes, enable.restype = [C.c_void_p, C.c_int], None
        enable(bridge.handle, int(automatic))
        for sequence, row in enumerate(payload["rows"], 1):
            world = decode_snapshot(row["native_world"])
            now = row["published_at"]
            assert bridge.world(world, now), "Original snapshot rejected"
            decision = bridge.decide(sequence)
            if decision["eligible"]:
                assert bridge.accept(decision, now, False), "Eligible visual acceptance failed"
                assert not bridge.accept(decision, now, False), "Repeated acceptance succeeded"
                accepted.append({"time": now, "prompt": decision["prompt"], "mode": decision["mode"]})
        status = bridge.lib.coaching_replay_drive_context
        status.argtypes, status.restype = [C.c_void_p], C.c_char_p
        return {"input": str(path), "input_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "source_provenance": payload.get("provenance"), "profile": profile, "automatic": automatic,
                "snapshots": len(payload["rows"]), "accepted": accepted,
                "final_drive": json.loads(status(bridge.handle))}
    finally:
        bridge.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worlds", nargs="+", type=Path)
    parser.add_argument("--library", type=Path, default=Path("build/native-spatial/liblearndrive_coaching_replay.dll"))
    parser.add_argument("--output", type=Path, default=Path("build/automatic-drive/recorded-policy.json"))
    args = parser.parse_args()
    results = [evaluate(path, args.library, profile, automatic)
               for path in args.worlds for profile in range(3) for automatic in (False, True)]
    report = {"scope": "cached_recorded_perception_policy_only", "visual_acceptance": True,
              "instructor_labels": None, "phone_test": False, "new_pixel_inference": False,
              "commercial_training_export": False, "configurations": len(results), "results": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    print(json.dumps({"configurations": len(results), "snapshot_decisions": sum(r["snapshots"] for r in results),
                      "visual_accepted": sum(len(r["accepted"]) for r in results), "output": str(args.output)}))


if __name__ == "__main__":
    main()
