import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_coaching_video import inspect


class CoachingVideoInspectorTest(unittest.TestCase):
    def setUp(self):
        self.report = {"schema_version": 2, "replay_bridge_contract": 2, "research_emergencies": False,
                       "assumptions": {"coaching_profile": "balanced", "mode": "semantic", "imu_available": False},
                       "events": [{}], "startup_events": []}
        self.decision = {"event": "decision", "source_id": 1, "sequence": 1, "time": 1.2,
                         "capture_time": 1., "scene_age": .2, "spatial_available": False, "eligible": True,
                         "settings_revision": 1, "prompt": 44, "coaching": {"speed_trend": 0, "mode": 1}}
        self.ack = {"event": "coaching_accepted", "source_id": 1, "sequence": 1, "time": 1.21,
                    "settings_revision": 1, "prompt": 44, "audible_playback_confirmed": False,
                    "simulated_validated_onset": True}

    def test_valid_simulation_is_not_reported_as_audible(self):
        result = inspect(self.report, [self.decision, self.ack])
        self.assertEqual(result["logical_accepted"], 1)
        self.assertEqual(result["physical_audio"], "not tested")

    def test_stale_revision_duplicate_and_heard_claims_fail(self):
        for changes in ({"time": 1.4}, {"settings_revision": 2}, {"audible_playback_confirmed": True},
                        {"simulated_validated_onset": False}, {"prompt": 14}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                inspect(self.report, [self.decision, {**self.ack, **changes}])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            inspect(self.report, [self.decision, self.ack, self.ack])

    def test_cooldown_does_not_hide_independent_warning(self):
        d, a = copy.deepcopy(self.decision), copy.deepcopy(self.ack)
        d.update(sequence=2, time=2.2, capture_time=2.);a.update(sequence=2,time=2.21)
        report = {**self.report, "events": [{}, {}]}
        with self.assertRaisesRegex(ValueError, "interval"):
            inspect(report, [self.decision, self.ack, d, a])
        d["coaching"]["mode"] = 2
        self.assertEqual(inspect(report, [self.decision, self.ack, d, a])["validated_warnings"], 1)


if __name__ == "__main__":
    unittest.main()
