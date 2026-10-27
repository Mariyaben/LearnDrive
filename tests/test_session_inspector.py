import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_session import inspect


class SessionInspectorTest(unittest.TestCase):
    def capture(self, rows, tail=""):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        path = Path(folder.name) / "session.jsonl"
        path.write_text("".join(json.dumps(r)+"\n" for r in rows)+tail, encoding="utf-8")
        return path

    def header(self):
        return {"schema_version": 1, "event": "session_start", "session_id": "test", "metadata": {}}

    def end(self, count=0, complete=True, dropped=0):
        return {"schema_version": 1, "event": "session_end", "session_id": "test", "records_written": count,
                "complete": complete, "records_dropped": dropped, "limit_reached": False}

    def test_clean(self):
        self.assertEqual(inspect(self.capture([self.header(), self.end()]))["journal_status"], "complete")

    def test_interrupted(self):
        self.assertEqual(inspect(self.capture([self.header()], '{"event":'))["journal_status"], "partial")

    def test_loss_is_partial(self):
        self.assertEqual(inspect(self.capture([self.header(), self.end(complete=False, dropped=1)]))["journal_status"], "partial")

    def test_false_complete(self):
        with self.assertRaises(ValueError):
            inspect(self.capture([self.header(), self.end(dropped=1)]))

    def test_wrong_count(self):
        with self.assertRaises(ValueError):
            inspect(self.capture([self.header(), self.end(count=3)]))

    def test_nonfinite(self):
        with self.assertRaises(ValueError):
            inspect(self.capture([self.header()], '{"schema_version":1,"event":"decision","time":NaN}\n'))

    def test_nonfinite_eof_is_not_a_truncation(self):
        with self.assertRaises(ValueError):
            inspect(self.capture([self.header()], '{"schema_version":1,"event":"decision","time":NaN}'))

    def test_unmatched_accepted(self):
        row = {"schema_version": 1, "event": "accepted", "source_id": 1, "sequence": 4, "prompt": 8}
        with self.assertRaises(ValueError):
            inspect(self.capture([self.header(), row, self.end(count=1)]))

    def test_matched_accepted(self):
        decision = {"schema_version": 1, "event": "decision", "source_id": 1, "sequence": 4, "prompt": 8, "eligible": True}
        accepted = {**decision, "event": "accepted"}
        result = inspect(self.capture([self.header(), decision, accepted, self.end(count=2)]))
        self.assertEqual(result["records"]["accepted"], 1)

    def logical(self, voice=False):
        decision = {"schema_version":1,"event":"decision","source_id":1,"sequence":4,"prompt":44,
                    "eligible":True,"settings_revision":2}
        accepted = {"schema_version":1,"event":"coaching_accepted","source_id":1,"sequence":4,"prompt":44,
                    "settings_revision":2,"voice_submitted":voice,"audible_playback_confirmed":False}
        return decision, accepted

    def test_muted_logical_acceptance_is_not_pcm_or_heard(self):
        decision, accepted = self.logical()
        result = inspect(self.capture([self.header(), decision, accepted, self.end(count=2)]))
        self.assertEqual(result["logical_accepted"], 1)
        self.assertEqual(result["voice_submissions"], 0)
        self.assertIn("unverified", result["audio_evidence"])

    def test_voice_submission_does_not_double_logical_total(self):
        decision, logical = self.logical(True)
        pcm = {**logical,"event":"accepted"}
        result = inspect(self.capture([self.header(), decision, logical, pcm, self.end(count=3)]))
        self.assertEqual(result["logical_accepted"], 1)
        self.assertEqual(result["voice_submissions"], 1)

    def test_logical_settings_revision_and_audibility_claim_fail_closed(self):
        for field,value in (("settings_revision",1),("audible_playback_confirmed",True)):
            decision, logical = self.logical()
            logical[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                inspect(self.capture([self.header(), decision, logical, self.end(count=2)]))

    def test_duplicate_logical_acknowledgment_fails(self):
        decision, logical = self.logical()
        with self.assertRaisesRegex(ValueError,"Duplicate"):
            inspect(self.capture([self.header(), decision, logical, logical, self.end(count=3)]))


if __name__ == "__main__":
    unittest.main()
