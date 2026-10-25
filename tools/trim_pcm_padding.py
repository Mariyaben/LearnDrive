"""Remove only excess digital-zero padding, preserving every nonzero PCM sample."""
import argparse
from array import array
from pathlib import Path
import sys
import wave


def trim(path):
    with wave.open(str(path), "rb") as source:
        params = source.getparams()
        if params.nchannels != 1 or params.sampwidth != 2 or params.comptype != "NONE":
            raise ValueError("Expected mono PCM16")
        pcm = source.readframes(source.getnframes())
    samples = array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    occupied = [i for i, sample in enumerate(samples) if sample != 0]
    if not occupied:
        raise ValueError("Silent clip")
    first = max(0, occupied[0] - round(params.framerate * .05))
    last = min(len(samples), occupied[-1] + 1 + round(params.framerate * .15))
    result = pcm[first * 2:last * 2]
    # No quiet consonants, internal pauses, amplitude or speaking rate change.
    assert not any(samples[:first]) and not any(samples[last:])
    with wave.open(str(path), "wb") as target:
        target.setparams(params)
        target.writeframes(result)
    return len(samples) / params.framerate, (last - first) / params.framerate


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wave", type=Path)
    args = parser.parse_args()
    before, after = trim(args.wave)
    print(f"{args.wave.name}: {before:.3f}s -> {after:.3f}s; nonzero PCM preserved")
