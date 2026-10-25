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


if __name__ == "__main__":
    unittest.main()
