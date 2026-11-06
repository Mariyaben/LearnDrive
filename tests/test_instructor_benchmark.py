import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import instructor_benchmark as bench  # noqa: E402


def journal(rows):
    return "\n".join(json.dumps(r) for r in rows) + "\n"


class ClassificationTests(unittest.TestCase):
    def test_instructor_phrasing_and_app_phrasing_share_intents(self):
        intents, attrs = bench.classify("Junction ahead. Brake gently and change down to second gear")
        self.assertIn("brake", intents)
        self.assertIn("gear", intents)
        self.assertIn("reduce_speed", intents)
        self.assertEqual(attrs["gear"], 2)
        intents, attrs = bench.classify("slow down, drop down to second")
        self.assertIn("reduce_speed", intents)
        self.assertEqual(attrs["gear"], 2)

    def test_waiting_is_not_permission_and_clutch_slowly_is_not_slowing(self):
        self.assertEqual(bench.classify("Let them go")[0], ["wait"])
        self.assertIn("proceed", bench.classify("Okay, now go")[0])
        intents, _ = bench.classify("first gear, check mirrors and signal, then release the clutch slowly")
        self.assertNotIn("slow", intents)
        self.assertEqual(bench.classify("Put the left indicator on")[1]["signal_side"], "left")

    def test_lexicon_extension_for_other_languages(self):
        lexicon = bench.compile_lexicon({"clutch": [r"\bklachu\b"]})
        self.assertIn("clutch", bench.classify("klachu chavittu", lexicon)[0])


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_journal_alignment_echo_removal_and_agreement(self):
        app = self.write("session.jsonl", journal([
            {"event": "session_start", "session_id": "s", "metadata": {}},
            {"event": "instructor_audio_start", "time": 100.0, "start_uncertainty_seconds": 0.01},
            {"event": "coaching_accepted", "time": 110.0, "prompt": 1101, "voice_submitted": True,
             "text": "Junction ahead. Brake gently and change down to second gear"},
            {"event": "coaching_accepted", "time": 130.0, "prompt": 71, "voice_submitted": True, "text": "Change up to third gear"},
            {"event": "coaching_accepted", "time": 150.0, "prompt": 88, "voice_submitted": False, "text": "Keep left"},
        ]))
        transcript = self.write("lesson.srt", "\n".join([
            "1", "00:00:09,000 --> 00:00:10,500", "Slow down, go down to second", "",
            "2", "00:00:10,200 --> 00:00:13,000", "Junction ahead brake gently and change down to second gear", "",
            "3", "00:00:40,000 --> 00:00:41,000", "Indicate left", "",
            "4", "00:00:45,000 --> 00:00:46,000", "Okay now go", "",
            "5", "00:00:50,000 --> 00:00:51,000", "Nice weather today", ""]))
        events, start, warnings = bench.read_app(app, include_visual=False)
        self.assertEqual(start, 100.0)
        self.assertEqual(len(events), 2, "visual-only instruction is not counted as spoken")
        result = bench.benchmark(bench.read_transcript(transcript), events, start, 3.0)
        self.assertEqual(result["app_echo_removed"], 1, "the app's own voice on the microphone is not the instructor")
        self.assertEqual(result["instructor_actions_covered"], {"covered": 1, "of": 2, "share": 0.5})
        self.assertEqual(result["app_actions_agreed"], {"agreed": 1, "of": 2, "share": 0.5})
        self.assertEqual(result["per_intent"]["signal"]["instructor_only"], 1)
        self.assertEqual(result["per_intent"]["proceed"]["kind"], "not_given_by_design")
        self.assertEqual(result["attribute_agreement"]["gear"], {"same": 1, "of": 1})
        self.assertEqual(result["unclassified_instructor_segments"], 1)
        self.assertLess(result["median_lag_seconds"], 2)
        self.assertIn("not a validation of teaching quality", result["meaning"])

    def test_mixed_segment_does_not_credit_the_app_with_its_echo(self):
        app = [{"time": 20.0, "text": "Change up to third gear", "duration": 2.0}]
        segments = [{"start": 20.5, "end": 23.0, "text": "change up to third, okay now press the clutch fully"}]
        result = bench.benchmark(segments, app, 0.0, 3.0)
        self.assertEqual(result["mixed_segments_stripped"], 1)
        row = result["instructor"][0]
        self.assertEqual(row["intents"], ["clutch"])
        self.assertEqual(result["per_intent"]["gear"]["matched"], 0)

    def test_report_csv_whisper_and_vtt_inputs_and_cli(self):
        report = self.write("report.json", json.dumps({"events": [
            {"time": 37.1, "prompt": 69, "text": "Change up to second gear", "duration": 1.7},
            {"time": 60.0, "prompt": 1201, "text": "Vehicle crossing. Stop and wait. Let them pass.", "duration": 3.0}]}))
        whisper = self.write("t.json", json.dumps({"segments": [
            {"start": 36.0, "end": 37.0, "text": "Second gear now"},
            {"start": 59.0, "end": 60.0, "text": "Wait, wait, let the bike go"}]}))
        vtt = self.write("t.vtt", "WEBVTT\n\n00:36.000 --> 00:37.000\nSecond gear now\n\n01:59.000 --> 02:00.000\nHorn!\n")
        csv_path = self.write("t.csv", "start,text\n36,Second gear now\n0:59,stop here\n")
        self.assertEqual(len(bench.read_transcript(whisper)), 2)
        self.assertEqual(bench.read_transcript(vtt)[1]["start"], 119.0)
        self.assertEqual(bench.read_transcript(csv_path)[1]["start"], 59.0)
        out, md = self.root / "out.json", self.root / "out.md"
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(bench.main([str(report), "--transcript", str(whisper), "--output", str(out), "--markdown", str(md)]), 0)
        result = json.loads(out.read_text(encoding="utf-8"))
        self.assertEqual(result["instructor_actions_covered"]["covered"], 2)
        self.assertEqual(result["per_intent"]["wait"]["matched"], 1)
        self.assertIn("| Intent |", md.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
