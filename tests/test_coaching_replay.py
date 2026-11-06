"""Host/native synthetic harness and shadow boundary regression checks."""
import json
import os
import subprocess
import tempfile
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT / "tools"))
from coaching_contract import empty_world, load_fixture
from generate_coaching_fixtures import candidate
from replay_coaching import NativeBridge, encode_world, percentiles, run
from benchmark_coaching_shadow import checked_proposal, execute

LIBRARY = Path(os.environ.get("LEARNDRIVE_COACHING_LIBRARY", ROOT / "build/native-spatial/liblearndrive_coaching_replay.dll"))


class ShadowBoundaryTest(unittest.TestCase):
    def test_omitted_expiry_is_derived_from_capture_never_replay_now(self):
        world=empty_world()
        world["candidates"]=[candidate(1)]
        _,_,packed=encode_world(world,42)
        self.assertAlmostEqual(packed[8],1.35)

    def test_unknowns_remain_unknown_in_native_pack(self):
        header, actors, candidates = encode_world(empty_world(), 0)
        self.assertEqual(len(header), 34)
        self.assertEqual(header[8], 0)
        self.assertEqual(header[9], 0)
        self.assertEqual(header[4], float("inf"))
        self.assertEqual(len(actors), 0)
        self.assertEqual(len(candidates), 0)

    def test_proposal_calls_validator_without_acceptance(self):
        class Gate:
            def validate(self,index,now):
                self.called=(index,now)
                return 3
        gate=Gate()
        world=empty_world()
        world["candidates"]=[candidate(0)]
        proposal,block=checked_proposal('{"schema_version":1,"mode":"COACH","candidate_index":0,"rationale":"diagnostic"}',world,gate,0)
        self.assertEqual(block,3)
        self.assertEqual(gate.called,(0,0))
        self.assertEqual(proposal["candidate_index"],0)

    def test_model_cannot_promote_routine_to_warn(self):
        world=empty_world()
        world["candidates"]=[candidate(0)]
        with self.assertRaisesRegex(ValueError,"promote"):
            checked_proposal('{"schema_version":1,"mode":"WARN","candidate_index":0,"rationale":"x"}',world,None,0)

    def test_local_adapter_timeout_and_bounded_output(self):
        result=execute([sys.executable,"-c","import time; time.sleep(2)"],{},timeout_ms=50)
        self.assertEqual(result["status"],"timeout")
        result=execute([sys.executable,"-c","print('x'*100000)"],{},timeout_ms=5000,max_output_bytes=1024)
        self.assertEqual(result["status"],"output_limit")

    def test_model_cannot_downgrade_urgent_semantics(self):
        world=empty_world();world["candidates"]=[candidate(0)]
        world["candidates"][0]["urgent"]=True
        with self.assertRaisesRegex(ValueError,"downgrade"):
            checked_proposal('{"schema_version":1,"mode":"COACH","candidate_index":0,"rationale":"x"}',world,None,0)

    def test_local_fixture_adapter_is_only_plumbing_evidence(self):
        result=execute([sys.executable,"-c","import sys; sys.stdin.read(); print('{\"schema_version\":1,\"mode\":\"SILENCE\",\"candidate_index\":null,\"rationale\":\"synthetic adapter test\"}')"],{},timeout_ms=5000)
        self.assertEqual(result["status"],"completed")
        self.assertEqual(json.loads(result["output"])["mode"],"SILENCE")

    def test_quantiles_are_observed_samples(self):
        self.assertEqual(percentiles([3,1,2])["p95"],3)
        self.assertIsNone(percentiles([])["p95"])

    def test_missing_model_reports_unavailable_without_loading_native_library(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/"report.json"
            process=subprocess.run([sys.executable,str(ROOT/"tools/benchmark_coaching_shadow.py"),
                                    str(ROOT/"scenarios/balanced-coaching/straight-silence.json"),
                                    "--library",str(Path(folder)/"nonexistent.dll"),"--output",str(output)],
                                   capture_output=True,text=True,timeout=10)
            self.assertEqual(process.returncode,0,process.stderr)
            report=json.loads(output.read_text())
            self.assertEqual(report["status"],"unavailable")
            self.assertEqual(report["samples"],[])
            self.assertTrue(report["shadow_only"])


@unittest.skipUnless(LIBRARY.is_file(), "current-source structured native bridge must be built separately")
class NativeStructuredReplayTest(unittest.TestCase):
    def evaluate(self,name,baseline=False):
        value=load_fixture(ROOT / "scenarios/balanced-coaching" / (name+".json"))
        bridge=NativeBridge(LIBRARY,baseline)
        self.addCleanup(bridge.close)
        return run(value,bridge)[0]

    def test_same_event_is_not_repeated_as_phrase_lock_expires(self):
        old=self.evaluate("same-pedestrian",True)
        current=self.evaluate("same-pedestrian")
        self.assertEqual(current["logical_accepts"],1)
        self.assertGreater(current["duplicate_ticks"],0)
        self.assertGreater(old["logical_accepts"],current["logical_accepts"])
        self.assertEqual(current["trace_sha256"], self.evaluate("same-pedestrian")["trace_sha256"])

    def test_new_actor_and_concurrent_event_remain_distinct(self):
        self.assertEqual(self.evaluate("new-pedestrian")["logical_accepts"],2)
        self.assertEqual(self.evaluate("concurrent-events")["logical_accepts"],2)

    def test_silence_and_unqualified_evidence_do_not_produce_cues(self):
        for case in ("straight-silence","bad-gps","missing-sensors","contradictory-route","unreliable-approach","future-capture"):
            with self.subTest(case=case):
                self.assertEqual(self.evaluate(case)["logical_accepts"],0)

    def test_turn_and_explicit_approach_produce_supported_events(self):
        self.assertEqual(self.evaluate("left-turn")["logical_accepts"],1)
        approach=self.evaluate("reliable-approach")
        self.assertGreater(approach["mode_ticks"].get("WARN",0),0)
        self.assertGreater(approach["logical_accepts"],0)

    def test_original_capture_and_settings_gate_queued_work(self):
        late=self.evaluate("stale-late-queue")
        self.assertEqual(late["logical_accepts"],0)
        self.assertGreater(late["queue_rejections"],0)
        revised=self.evaluate("voice-revision")
        self.assertEqual(revised["simulated_validated_onsets"],0)
        self.assertGreater(revised["playback_rejections"],0)

    def test_retired_source_rejected_and_captures_not_redated(self):
        result=self.evaluate("out-of-order-source")
        self.assertEqual(len(result["input_rejections"]),1)


@unittest.skipUnless((ROOT / "build/native-spatial/liblearndrive_replay.dll").is_file(), "video native bridge unavailable")
class VideoCoachingIntegrationTest(unittest.TestCase):
    def test_video_opt_in_uses_native_acceptance_and_original_capture_onset(self):
        import numpy as np
        from review_video import Native,ptr
        native=Native(ROOT / "build/native-spatial/liblearndrive_replay.dll")
        handle=native.create(b"",64,48,None)
        self.assertTrue(handle)
        try:
            native.configure_coaching(handle,"balanced")
            yuv=np.full(64*48*3//2,128,np.uint8)
            tensor=np.empty(3*320*320,np.float32)
            labels=np.zeros(84,np.float32);labels[:5]=[160,160,25.6,96,.95]
            decision=np.zeros(8,np.float64)
            for capture in (1.,1.125):
                native.require(handle,native.prepare(handle,ptr(yuv),ptr(tensor)))
                native.require(handle,native.labels(handle,capture,ptr(labels),84,1))
                native.tick(handle,capture+.02,0.,0,0,ptr(decision))
            self.assertEqual(int(decision[0]),44)
            self.assertTrue(decision[6])
            self.assertTrue(native.coaching_accepted(handle,44,1.145))
            self.assertTrue(native.coaching_onset(handle,44,1.145))
            self.assertFalse(native.coaching_onset(handle,44,1.5))
            diagnostic=json.loads(native.coaching_diagnostics(handle,1.145))
            self.assertEqual(diagnostic["logical_accepted"],1)
            self.assertEqual(diagnostic["playback_onsets"],1)
        finally:
            native.destroy(handle)

if __name__ == "__main__":
    unittest.main()
