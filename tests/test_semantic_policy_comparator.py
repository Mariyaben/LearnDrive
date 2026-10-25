"""Check the separate historical driver without loading models or native DLLs."""
import ctypes as C
from pathlib import Path
import sys
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import compare_semantic_policy as comparator


class FakeBridge:
    def __init__(self, spatial=False):
        self.spatial = spatial
        self.captures = []
        self.destroyed = False

    def create(self, model, width, height, geometry):
        assert model == b"" and geometry is None
        return 1

    def emergencies(self, handle, enabled):
        assert enabled == 0

    def tick(self, handle, now, speed, known, playing, out):
        np.ctypeslib.as_array(C.cast(out, C.POINTER(C.c_double)), shape=(8,))[:] = 0

    def trace(self, handle, out):
        trace = np.ctypeslib.as_array(C.cast(out, C.POINTER(C.c_double)), shape=(22,))
        trace[:] = 0
        trace[14] = self.spatial

    def prepare(self, *args):
        return 1

    def labels(self, handle, capture, values, count, candidates):
        self.captures.append(capture)
        return 1

    def require(self, handle, result):
        assert result

    def destroy(self, handle):
        self.destroyed = True


class HistoricalDriverTests(unittest.TestCase):
    def setUp(self):
        self.report = {"video": "unused.mp4", "metadata": {"streams": [{"codec_type": "video", "width": 16, "height": 16}]}}
        self.arrays = {"outputs": np.empty((10, 0), np.float32),
                       "semantic_indices": np.array([0, 4, 8]),
                       "detections": np.zeros((3, 84, 2), np.float32)}

    def pixels(self, *args):
        return (np.full(384, i, np.uint8) for i in range(10))

    def test_matched_inputs_and_capture_clock_without_spatial_api(self):
        old, new = FakeBridge(), FakeBridge()
        with mock.patch.object(comparator, "frames", side_effect=self.pixels):
            baseline = comparator.replay(old, self.report, self.arrays, {})
            current = comparator.replay(new, self.report, self.arrays, {})
        self.assertEqual(baseline, current)
        np.testing.assert_allclose(old.captures, [6., 6. + 4 / 30, 6. + 8 / 30])
        self.assertEqual(baseline["semantic_input_frames"], 3)
        self.assertFalse(baseline["spatial_available_observed"])
        self.assertTrue(old.destroyed and new.destroyed)

    def test_any_spatial_availability_fails_and_cleans_source(self):
        bridge = FakeBridge(spatial=True)
        with mock.patch.object(comparator, "frames", side_effect=self.pixels), \
             self.assertRaisesRegex(RuntimeError, "published spatial evidence"):
            comparator.replay(bridge, self.report, self.arrays, {})
        self.assertTrue(bridge.destroyed)

    def test_old_or_wrong_protocol_report_is_rejected(self):
        with mock.patch.object(Path, "read_text", return_value='{"assumptions": {}, "schema_version": 1}'), \
             self.assertRaisesRegex(ValueError, "verified v2"):
            comparator.checked_report(Path("report.json"), Path("current.dll"))


if __name__ == "__main__":
    unittest.main()
