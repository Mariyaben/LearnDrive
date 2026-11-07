"""Drive the native brain toward every kind of Google navigation step and print what it says.

Each case is a straight approach fed like the phone: the Google turn feed (step, maneuver,
metres and seconds left) at 2 Hz with its step detail (side, roundabout exit, next step), the
measured speed, and empty camera frames. The learner drives at 30 km/h, slows to 15 km/h for
the maneuver (or stops at the destination), then carries on.

Usage: python tools/navigation_phrases_check.py
"""
from __future__ import annotations

import ctypes as C
import math
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import review_video as rv  # noqa: E402

P = C.c_void_p
ASSETS = ROOT / "android/app/src/main/assets"
LEFT, RIGHT, FORK, RAMP, MERGE, UTURN, ROUNDABOUT, STRAIGHT, DESTINATION = 1, 2, 3, 4, 5, 6, 7, 8, 9
CASES = [  # name, maneuver, side, roundabout exit, next maneuver, gap to it (m)
    ("Left turn", LEFT, 0, 0, 0, math.inf),
    ("Right turn", RIGHT, 0, 0, 0, math.inf),
    ("Fork: keep left", FORK, -1, 0, 0, math.inf),
    ("Fork: keep right", FORK, 1, 0, 0, math.inf),
    ("Roundabout, 2nd exit", ROUNDABOUT, 0, 2, 0, math.inf),
    ("Roundabout, 5th exit", ROUNDABOUT, 0, 5, 0, math.inf),
    ("U-turn", UTURN, 0, 0, 0, math.inf),
    ("Merge (left)", MERGE, -1, 0, 0, math.inf),
    ("Ramp", RAMP, 1, 0, 0, math.inf),
    ("Destination", DESTINATION, 0, 0, 0, math.inf),
    ("Straight on", STRAIGHT, 0, 0, 0, math.inf),
    ("Left, then right 50 m after", LEFT, 0, 0, RIGHT, 50),
]


def run_case(native, durations, fragment_durations, maneuver, side, exit_, nxt, gap):
    handle = native.create(b"", 320, 240, None)
    native.bind_navigation()
    native.live_navigation(handle)
    native.configure_coaching(handle, "balanced")  # the app's default profile
    native.bind_automatic()
    native.coaching_automatic(handle, 1, 1)

    def duration(prompt):
        if prompt in durations:
            return durations[prompt]
        parts = np.zeros(8, np.int32)
        k = native.prompt_fragments(int(prompt), parts.ctypes.data_as(P), 8)
        return sum(fragment_durations[int(f)] for f in parts[:k]) + .12 * max(0, k - 1) if k > 0 else 3.0

    frame = np.full(320 * 240 * 3 // 2, 128, np.uint8)
    tensor = np.empty((1, 3, 320, 320), np.float32)
    empty = np.zeros((84, 1), np.float32)
    decision = np.empty(8, np.float64)
    start, s, t, dt, k, sequence = 320.0, 0.0, 0.0, .05, 0, 0
    playing, playing_until, said = 0, 0.0, []
    stop = maneuver == DESTINATION
    while t < 90:
        left = start - s
        if stop and left <= 0:
            v = 0.0
        else:
            v = 30.0 if left > 60 else max(15.0 if not stop else 3.0, 15 + (left / 60) * 15) if left > 0 else min(30.0, 15 + (-left) * .5)
        if k % 2 == 0:
            native.prepare(handle, rv.ptr(frame), rv.ptr(tensor))
            native.labels(handle, t, rv.ptr(empty), empty.size, empty.shape[1])
        if k % 10 == 0:
            sequence += 1
            lat, lon = 10.0 + s / 110540, 76.0
            if left > 0:
                native.navigation_at(handle, t, sequence, 1, maneuver, left, left / max(v / 3.6, 1), v, lat, lon, 0.0)
                native.navigation_detail(handle, t, side, exit_, nxt, gap)
            else:  # Past it: the next step, far away.
                native.navigation_at(handle, t, sequence, 2, nxt or STRAIGHT, 900 + left, 120, v, lat, lon, 0.0)
                native.navigation_detail(handle, t, 0, 0, 0, math.inf)
        if playing and t < playing_until and native.coaching_continue_reason(handle, playing, t).decode() != "OK":
            native.coaching_continue(handle, playing, t)
            said.append((t, left, v, "   (cut off: " + native.coaching_continue_reason(handle, playing, t).decode() + ")"))
            playing, playing_until = 0, t
        if t >= playing_until:
            if playing:
                native.coaching_completed(handle, playing, t)
            playing = 0
        native.tick(handle, t, v, 1, playing, rv.ptr(decision))
        selected = int(decision[0])
        if selected and decision[6] and decision[7]:
            if native.coaching_accepted(handle, selected, t) and native.coaching_onset(handle, selected, t):
                said.append((t, left, v, native.prompt(selected).decode()))
                playing, playing_until = selected, t + duration(selected)
        s += v / 3.6 * dt
        t += dt
        k += 1
        if left < -60:
            break
    native.destroy(handle)
    return said


def main():
    native = rv.Native(ROOT / "build/native-spatial/liblearndrive_replay.dll")
    native._bind({"navigation_at": ([P, C.c_double, C.c_uint64, C.c_int, C.c_int, C.c_double, C.c_double, C.c_double,
                                     C.c_double, C.c_double, C.c_double], C.c_int),
                  "navigation_detail": ([P, C.c_double, C.c_int, C.c_int, C.c_int, C.c_double], None)})
    durations, fragment_durations = {}, {}
    for prompt in range(1, native.prompt_count() + 1):
        with wave.open(str(ASSETS / f"audio/prompt-{prompt}.wav"), "rb") as clip:
            durations[prompt] = clip.getnframes() / clip.getframerate()
    for fragment in range(1, native.fragment_count() + 1):
        with wave.open(str(ASSETS / f"audio/fragment-{fragment}.wav"), "rb") as clip:
            fragment_durations[fragment] = clip.getnframes() / clip.getframerate()
    for name, maneuver, side, exit_, nxt, gap in CASES:
        print(f"\n== {name}")
        for t, left, v, text in run_case(native, durations, fragment_durations, maneuver, side, exit_, nxt, gap):
            where = f"{left:5.0f} m before" if left > 0 else f"{-left:5.0f} m after "
            print(f"  {where} at {v:4.1f} km/h: {text}")


if __name__ == "__main__":
    main()
