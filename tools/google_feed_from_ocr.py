"""Rebuild Google's turn feed for a screen-recorded drive from the app's own on-screen text.

The coach card shows Google's next manoeuvre and distance while idle ("Next: Turn left · 120 m");
Google's turn card shows the distance while an instruction is on the coach card. One sample a
second, as the Navigation SDK delivers them. Display values are rounded (10 m steps), so replayed
turn timing is good to about a second.

Usage: python tools/google_feed_from_ocr.py <drive dir with coach_ocr.tsv and turn_ocr.tsv> [--output google_feed.json]
"""
import argparse
import csv
import json
import re
from pathlib import Path

NEXT = re.compile(r"Next:\s*(Turn|Keep|Slight|Sharp)\s+(left|right)\s*\S?\s*(here|[\d.]+\s*(?:km|m))", re.I)
CARD = re.compile(r"^\s*(?:\+\d\s+)?(\d+(?:\.\d+)?)\s*(km|m)\b", re.I)


def load(path):
    out = {}
    with open(path, encoding="utf-8", errors="replace") as source:
        for row in csv.reader(source, delimiter="\t"):
            if row and row[0].split(".")[0].isdigit():
                out[int(row[0].split(".")[0])] = " ".join(row[1:])
    return out


def metres(text):
    text = text.strip().lower()
    if text == "here":
        return 0.0
    value = float(re.match(r"[\d.]+", text).group(0))
    return value * 1000 if "km" in text else value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("drive", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    coach, card = load(args.drive / "coach_ocr.tsv"), load(args.drive / "turn_ocr.tsv")
    samples, side, last, turn = [], None, None, 0
    for t in range(min(coach), max(coach) + 1):
        distance = None
        match = NEXT.search(coach.get(t, ""))
        if match:
            new_side = match.group(2).lower()
            distance = metres(match.group(3))
            if side is not None and (new_side != side or (last is not None and distance > last + 150)):
                turn += 1  # Google moved on to its next manoeuvre.
            side = new_side
        elif side is not None and last is not None:
            # An instruction is on the coach card: Google's own card still counts down.
            match = CARD.match(card.get(t, ""))
            if match:
                value = float(match.group(1)) * (1000 if match.group(2).lower() == "km" else 1)
                # The road shield ("66") and clipped digits are not distances: accept only a
                # plausible continuation of the countdown.
                if last - 60 <= value <= last + 15:
                    distance = value
        if distance is None or side is None:
            continue
        if last is not None and samples and samples[-1][1] == turn and distance > last + 15:
            continue  # A misread within one countdown.
        samples.append([t, turn, side, distance])
        last = distance
    turns = {}
    for t, index, side_name, distance in samples:
        turns.setdefault(index, {"maneuver": side_name, "first": t, "first_m": distance})
        turns[index].update(last=t, last_m=distance)
    output = args.output or args.drive / "google_feed.json"
    output.write_text(json.dumps({
        "source": "on-screen text of the screen recording (coach card and Google's turn card); display rounding 10 m",
        "samples": samples, "turns": [turns[k] for k in sorted(turns)]}, indent=1) + "\n", encoding="utf-8")
    print(f"{len(samples)} samples, {len(turns)} turns -> {output}")
    for k in sorted(turns):
        v = turns[k]
        print(f"  turn {k}: {v['maneuver']:5s} first seen {v['first']//60:02d}:{v['first']%60:02d} at {v['first_m']:.0f} m, "
              f"last {v['last']//60:02d}:{v['last']%60:02d} at {v['last_m']:.0f} m")


if __name__ == "__main__":
    main()
