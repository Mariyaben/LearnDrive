import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from review_video import decodable_candidates  # noqa: E402


class ZoomCacheTests(unittest.TestCase):
    def test_keeps_exactly_the_columns_the_native_decoder_can_use(self):
        raw = np.zeros((84, 4200), np.float32)
        raw[0:4] = 50
        raw[4 + 0, 10] = 0.9    # person: kept
        raw[4 + 7, 4000] = 0.45  # truck from the zoom half: kept
        raw[4 + 9, 20] = 0.95   # traffic light: argmax is not a road user, dropped
        raw[4 + 2, 30] = 0.39   # car below threshold: dropped
        raw[4 + 2, 21] = 0.5    # car, but a non-road class wins: dropped
        raw[4 + 16, 21] = 0.8
        out = decodable_candidates(raw)
        self.assertEqual(out.shape, (84, 256))
        np.testing.assert_array_equal(out[:, 0], raw[:, 10])
        np.testing.assert_array_equal(out[:, 1], raw[:, 4000])
        self.assertEqual(float(out[4:, 2:].max()), 0.0)

    def test_bounded_to_the_best_candidates_in_original_order(self):
        raw = np.zeros((84, 1000), np.float32)
        raw[4, :] = np.linspace(0.41, 0.99, 1000, dtype=np.float32)
        out = decodable_candidates(raw, keep=256)
        kept = out[4]
        self.assertTrue(np.all(kept >= raw[4, 743]))
        self.assertTrue(np.all(np.diff(kept) > 0), "original column order is preserved")


if __name__ == "__main__":
    unittest.main()
