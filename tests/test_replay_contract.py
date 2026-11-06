"""Replay integrity/clock regression tests; no models, downloads or device needed."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import replay_contract as contract


class CacheContractTests(unittest.TestCase):
    def test_concurrent_publish_keeps_one_valid_origin_without_replacing_readers(self):
        import concurrent.futures
        import threading
        path = self.root / "concurrent.npz"
        barrier = threading.Barrier(2)
        original_link = contract.os.link
        def simultaneous_link(source, destination):
            barrier.wait(timeout=5)
            return original_link(source, destination)
        with mock.patch.object(contract.os, "link", side_effect=simultaneous_link):
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(contract.save_cache, path, self.provenance,
                    self.root / name, **self.arrays) for name in ("one", "two")]
                origins = [f.result(timeout=10) for f in futures]
        _, origin = contract.load_cache(path, self.provenance)
        self.assertEqual(origins, [origin, origin])
        with path.open("rb"):
            reused = contract.save_cache(path, self.provenance, self.root / "reader", **self.arrays)
        self.assertEqual(reused, origin)
        self.assertFalse(list(self.root.glob("*.part")))

    def test_explicit_regeneration_can_replace_invalid_cache(self):
        self.path.write_bytes(b"invalid")
        origin = contract.save_cache(self.path, self.provenance, self.root / "regenerated",
            replace_existing=True, **self.arrays)
        self.assertEqual(contract.load_cache(self.path, self.provenance)[1], origin)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.path = self.root / "cache.npz"
        self.provenance = {"schema_version": 2, "native_library_sha256": "a", "runtime": {"provider": "CPU"},
                           "input": {"spatial": False, "analysis_fps": 4, "semantic_max_hz": 2}}
        self.arrays = {"outputs": np.empty((5, 0), np.float32),
                       "detections": np.zeros((3, 84, 2), np.float32),
                       "semantic_indices": np.array([0, 2, 4], np.int64),
                       "inference_ms": np.array([1., 2., 3.])}
        self.origin = contract.save_cache(self.path, self.provenance, self.root / "first", **self.arrays)

    def tearDown(self):
        self.temp.cleanup()

    def rewrite(self, arrays=None, metadata=None):
        np.savez_compressed(self.path, metadata=np.asarray(contract.canonical_json(metadata or self.origin)),
                            **(arrays or self.arrays))

    def test_origin_survives_reuse(self):
        arrays, origin = contract.load_cache(self.path, self.provenance)
        self.assertEqual(origin, self.origin)
        self.assertEqual(origin["created_for_output"], str((self.root / "first").resolve()))
        np.testing.assert_array_equal(arrays["semantic_indices"], [0, 2, 4])

    def test_provenance_change_rejects_existing_cache(self):
        changed = copy.deepcopy(self.provenance)
        changed["native_library_sha256"] = "new binary"
        self.assertNotEqual(contract.inference_key(changed), contract.inference_key(self.provenance))
        with self.assertRaisesRegex(ValueError, "provenance does not match"):
            contract.load_cache(self.path, changed)

    def test_finite_payload_tamper_is_detected(self):
        self.arrays["detections"][0, 0, 0] = 0.7
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "array fingerprints"):
            contract.load_cache(self.path, self.provenance)

    def test_invalid_arrays_are_rejected(self):
        for field, value in [("semantic_indices", np.array([0, 1, 4], np.int64)),
                             ("outputs", np.empty((5, 1), np.float32)),
                             ("detections", np.zeros((3, 83, 2), np.float32)),
                             ("inference_ms", np.array([1., np.nan, 3.])),
                             ("inference_ms", np.array([1., -1., 3.]))]:
            with self.subTest(field=field, shape=value.shape):
                bad = {**self.arrays, field: value}
                self.rewrite(arrays=bad)
                with self.assertRaises(ValueError):
                    contract.load_cache(self.path, self.provenance)

    def test_legacy_and_pickle_metadata_rejected(self):
        np.savez_compressed(self.path, **self.arrays)
        with self.assertRaisesRegex(ValueError, "missing/extra cache fields"):
            contract.load_cache(self.path, self.provenance)
        np.savez_compressed(self.path, metadata=np.asarray({"x": "object"}, dtype=object), **self.arrays)
        with self.assertRaises(ValueError):
            contract.load_cache(self.path, self.provenance)

    def test_bad_origin_rejected(self):
        for field, value in [("creation_id", "yesterday"), ("created_at_utc", "2026-10-22T00:00:00"),
                             ("created_for_output", "relative/path")]:
            with self.subTest(field=field):
                self.rewrite(metadata={**self.origin, field: value})
                with self.assertRaises(ValueError):
                    contract.load_cache(self.path, self.provenance)

    def test_binary_header_runtime_and_decoder_change_key(self):
        for relative in ("core/src/source.cpp", "core/include/api.hpp", "desktop/replay_bridge.cpp",
                         "tools/review_video.py", "tools/replay_contract.py", "assets/detector.onnx",
                         "bin/replay.dll", "bin/onnxruntime.dll", "video.mp4"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(relative.encode())
        runtime = {"provider": "CPU", "version": "1"}
        decoder = {"pipeline": "limited BT601"}
        def provenance():
            with mock.patch.object(contract, "__file__", str(self.root / "tools/replay_contract.py")):
                return contract.inference_provenance(self.root, self.root / "video.mp4", self.root / "assets",
                    self.root / "bin/replay.dll", [], 4, 2, False, copy.deepcopy(runtime), copy.deepcopy(decoder),
                    self.root / "tools/review_video.py")
        old = contract.inference_key(provenance())
        for relative in ("core/include/api.hpp", "bin/replay.dll", "bin/onnxruntime.dll"):
            path = self.root / relative
            original = path.read_bytes()
            path.write_bytes(original + b"changed")
            self.assertNotEqual(old, contract.inference_key(provenance()), relative)
            path.write_bytes(original)
        runtime["provider"] = "different"
        self.assertNotEqual(old, contract.inference_key(provenance()))
        runtime["provider"] = "CPU"
        decoder["pipeline"] = "full range"
        self.assertNotEqual(old, contract.inference_key(provenance()))


class ClockTests(unittest.TestCase):
    def test_speed_never_backfills_or_holds_forever(self):
        speeds = contract.SpeedTimeline([(1., 25.), (5., 30.)], max_age_seconds=1.5)
        self.assertIsNone(speeds.at(.99).speed_kmh)
        self.assertEqual(speeds.at(1.).speed_kmh, 25.)
        self.assertEqual(speeds.at(2.49).speed_kmh, 25.)
        self.assertIsNone(speeds.at(2.5).speed_kmh)
        self.assertEqual(speeds.at(2.5).sample_time, 1.)
        self.assertEqual(speeds.at(5.).speed_kmh, 30.)
        self.assertIsNone(speeds.at(100.).speed_kmh)

    def test_assumed_speed_is_labeled_and_absent_before_recording(self):
        speeds = contract.SpeedTimeline(manual_speed=30.)
        self.assertIsNone(speeds.at(-.1).speed_kmh)
        self.assertIn("NOT recorded", speeds.at(0.).source)

    def test_invalid_speed_clocks_rejected(self):
        for samples in ([(1, 10), (1, 20)], [(2, 10), (1, 20)], [(-1, 10)], [(0, float("nan"))]):
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                contract.SpeedTimeline(samples)
        with self.assertRaises(ValueError):
            contract.SpeedTimeline([(0, 10)], manual_speed=20)

    def test_semantic_first_and_distinct_completion(self):
        events = contract.publication_events(3, 10, [0, 2], .1, 0, True)
        self.assertEqual([e[3] for e in events if e[2] == 0], ["semantic", "spatial", "observation"])
        delayed = contract.publication_events(3, 10, [0, 2], .1, .15, True)
        self.assertEqual(delayed, sorted(delayed))
        initial = {stage: deadline for deadline, _, index, stage in delayed if index == 0}
        self.assertAlmostEqual(initial["semantic"], .1)
        self.assertAlmostEqual(initial["spatial"], .25)
        self.assertTrue(all(deadline >= index / 10 for deadline, _, index, _ in delayed))

    def test_semantic_mode_has_no_spatial_publication(self):
        events = contract.publication_events(3, 10, [0, 2], .1, 0, False)
        self.assertNotIn("spatial", [e[3] for e in events])


class ReviewerIntegrationTests(unittest.TestCase):
    def test_duplicate_exception_requires_full_fresh_encounter_provenance(self):
        from run_indian_corpus_review import verified_new_encounter
        previous = {"prompt": 44, "origin": 4, "source_id": 7, "camera_actor": {"id": 10, "category": 0}}
        row = {"prompt": 44, "origin": 4, "source_id": 7, "scene_age": .2,
               "camera_actor": {"id": 11, "category": 0},
               "reason": "entry; encounter=11; reference=10; source=7; new_entry=1"}
        self.assertTrue(verified_new_encounter(row, previous))
        for changes in ({"reason": "entry; encounter=11; reference=10; source=7; new_entry=0"},
                        {"reason": "entry; encounter=11; reference=8; source=7; new_entry=1"},
                        {"source_id": 8}, {"scene_age": .35}, {"prompt": 37},
                        {"camera_actor": {"id": 10, "category": 0}},
                        {"camera_actor": {"id": 11, "category": 3}}):
            with self.subTest(changes=changes):
                self.assertFalse(verified_new_encounter({**row, **changes}, previous))

    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg unavailable")
    def test_decoder_normalizes_rgb_white_to_limited_bt601(self):
        import cv2
        from review_video import frames
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "white.png"
            cv2.imwrite(str(image), np.full((16, 16, 3), 255, np.uint8))
            video = Path(temp) / "white.mkv"
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-loop", "1", "-i", str(image),
                            "-t", "1", "-r", "1", "-c:v", "ffv1", str(video)], check=True)
            decoded = list(frames(video, 16, 16, 1))
            self.assertEqual(len(decoded), 1)
            self.assertEqual(decoded[0].shape, (384,))
            np.testing.assert_allclose(decoded[0][:256], 235, atol=1)
            np.testing.assert_allclose(decoded[0][256:], 128, atol=1)

    def test_old_native_bridge_has_explicit_upgrade_error(self):
        import review_video
        old = mock.Mock()
        old.replay_contract_version.return_value = 1
        with mock.patch.object(review_video.os, "add_dll_directory", return_value=None, create=True), \
             mock.patch.object(review_video.C, "CDLL", return_value=old), \
             self.assertRaisesRegex(RuntimeError, "rebuild learndrive_replay"):
            review_video.Native(Path("old/replay.dll"))


if __name__ == "__main__":
    unittest.main()
