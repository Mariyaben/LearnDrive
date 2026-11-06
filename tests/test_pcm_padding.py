import array
from pathlib import Path
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from trim_pcm_padding import trim


class PcmPaddingTest(unittest.TestCase):
    def test_keeps_every_nonzero_sample_and_internal_pause_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "speech.wav"
            speech = [1, -2, 800, 0, 0, -1200, 1]
            values = array.array("h", [0] * 1000 + speech + [0] * 2000)
            if sys.byteorder != "little":
                values.byteswap()
            with wave.open(str(path), "wb") as target:
                target.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
                target.writeframes(values.tobytes())
            before, after = trim(path)
            self.assertLess(after, before)
            with wave.open(str(path), "rb") as source:
                samples = array.array("h", source.readframes(source.getnframes()))
            if sys.byteorder != "little":
                samples.byteswap()
            first = next(i for i, value in enumerate(samples) if value)
            self.assertEqual(list(samples[first:first+len(speech)]), speech)
            original = path.read_bytes()
            trim(path)
            self.assertEqual(path.read_bytes(), original)

    def test_silent_input_is_preserved_on_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "silent.wav"
            with wave.open(str(path), "wb") as target:
                target.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
                target.writeframes(bytes(8000))
            original = path.read_bytes()
            with self.assertRaises(ValueError):
                trim(path)
            self.assertEqual(path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
