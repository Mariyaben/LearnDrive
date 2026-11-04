"""Instructor benchmark: compare a driving instructor's spoken instructions with the app's.

App side (one of):
  * an exported diagnostics journal (LearnDrive-session.jsonl). Its coaching_accepted
    records carry the composed instruction text (0.7.4+), and instructor_audio_start
    gives the start of the instructor recording on the same clock;
  * a review_video.py report.json or guidance.csv (video time).

Instructor side: a transcript of the lesson audio with times in seconds from the start
of the audio: Whisper JSON ({"segments": [...]}), SRT, WebVTT, or CSV/TSV with columns
start[,end],text. Hand annotation in the CSV form works too. With --audio and the
optional faster-whisper package the tool transcribes itself (the package downloads model
weights on first use; --translate turns Malayalam/Hindi/... speech into English text).

Both sides are classified by the same instruction lexicon (multi-label: brake, clutch,
gear, signal, mirror, wait, ...). Instructions agree when the same intent is given within
+-window seconds. The microphone also hears the app: transcript segments that repeat an
instruction the app spoke at that moment are removed, and app words are stripped from
mixed segments, so the app is never credited with its own echo.

This measures agreement with one instructor on one drive. It is not a validation of
teaching quality, safety or legality; an instructor-only instruction is not always an
app error, and the app deliberately never says that it is clear or safe to go.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import difflib
import json
import math
import re
import sys
from pathlib import Path

# Order matters: each match consumes its words, so "let them go" is waiting, not
# permission to go, and "off the accelerator" is slowing, not accelerating.
LEXICON: list[tuple[str, list[str]]] = [
    ("wait", [r"\bwait\b", r"\bhold (it|on|there|back|here)\b", r"\blet (them|him|her|it|the \w+) (pass|go|cross)\b",
              r"\bgive way\b", r"\byield\b", r"\bdon'?t go\b", r"\bnot yet\b"]),
    ("slow", [r"\bease off\b", r"\b(off|lift off|foot off) the (accelerator|gas|throttle|pedal)\b", r"\b(go|drive|take it) (slow|slowly|easy)\b", r"\bslow( down| it down)?\b", r"(?<!clutch )\bslowly\b",
              r"\breduce (your |the )?speed\b", r"\btoo fast\b", r"\bnot so fast\b"]),
    ("brake", [r"\bbrak(e|es|ing)\b"]),
    ("stop", [r"\bstop\b", r"\bhalt\b"]),
    ("clutch", [r"\bclutch\b"]),
    ("gear", [r"\b(first|second|third|fourth|fifth|1st|2nd|3rd|4th|5th) gear\b",
              r"\b(change|shift|go|drop|come) (up|down)( to| into)? (first|second|third|fourth|fifth)\b",
              r"\binto (first|second|third|fourth|fifth)\b", r"\bgears?\b", r"\b(change|shift) (up|down)\b", r"\bneutral\b"]),
    ("signal", [r"\bsignal(l?ing)?\b", r"\bindicat(e|or|ors|ing)\b", r"\bblinker\b"]),
    ("mirror", [r"\bmirrors?\b", r"\bblind spot\b", r"\bover your shoulder\b", r"\bcheck behind\b"]),
    ("look", [r"\blook (left|right|both ways|around|again|out)\b", r"\bcheck (left|right|both)\b", r"\bwatch( out)?\b", r"\bcareful\b"]),
    ("keep_left", [r"\bkeep (to the )?left\b", r"\bstay (in your lane|left)\b", r"\bmove (back )?(to the )?left\b",
                   r"\b(centre|center) of (your|the) lane\b", r"\bdrifting\b", r"\btoo far (left|right)\b"]),
    ("give_space", [r"\bgive (them |him |her |it )?(more |plenty of |some )?(space|room)\b", r"\bmove (out|away|over)\b"]),
    ("proceed", [r"\bmove off\b", r"\bproceed\b", r"\bpull away\b", r"\bcarry on\b", r"\bgo ahead\b",
                 r"\b(now |you can |ok(ay)? |right,? )?go\b(?!\s\s)(?! (slow|slowly|easy|gently|down|up|left|right|straight))"]),
    ("accelerate", [r"\baccelerate\b", r"\bspeed up\b", r"\bmore (gas|power|throttle)\b", r"\b(add|give|press)( it)?( some| a bit of| a little| gentle)? (gas|throttle|power|accelerator)\b"]),
    ("steer", [r"\bstraighten\b", r"\bsteer(ing)?\b", r"\b(turn|rotate) the (steering )?wheel\b"]),
    ("horn", [r"\bhorn\b", r"\bhonk\b"]),
    # Context: what the instruction is about, not an action.
    ("junction", [r"\bjunction\b", r"\bintersection\b", r"\bcrossroads?\b", r"\broundabout\b", r"\bturn(ing)? (left|right)\b",
                  r"\b(left|right) turn\b", r"\btake the (left|right|next)\b"]),
    ("slope", [r"\b(up|down)hill\b", r"\bhill\b", r"\bslope\b", r"\bincline\b", r"\bgradient\b"]),
    ("road_user", [r"\b(pedestrian|person|people|man|woman|child|children|kid|cyclist|bicycle|bike|motorbike|scooter|rider|truck|lorry|bus|tractor|car|vehicle|auto|rickshaw|cow|dog|animal)s?\b"]),
]
CONTEXT = {"junction", "slope", "road_user"}
# "Slow down" and "brake gently" both ask for less speed; agreement is also scored on the family.
FAMILIES = {"reduce_speed": {"slow", "brake"}}
# Intents the app does not give by design: it never grants permission to go, and it
# never tells the learner to use the horn.
NOT_GIVEN_BY_DESIGN = {"proceed", "horn"}
ORDINALS = {"first": 1, "1st": 1, "one": 1, "second": 2, "2nd": 2, "two": 2, "third": 3, "3rd": 3, "three": 3,
            "fourth": 4, "4th": 4, "four": 4, "fifth": 5, "5th": 5, "five": 5}
TOKEN = re.compile(r"[a-z0-9']+")


def compile_lexicon(extra: dict | None = None):
    lexicon = [(name, list(patterns)) for name, patterns in LEXICON]
    for name, patterns in (extra or {}).items():
        for entry in lexicon:
            if entry[0] == name:
                entry[1].extend(patterns)
                break
        else:
            lexicon.append((name, list(patterns)))
    return [(name, [re.compile(p, re.IGNORECASE) for p in patterns]) for name, patterns in lexicon]


COMPILED = compile_lexicon()


def classify(text: str, lexicon=None) -> tuple[list[str], dict]:
    """Intents in order of first mention, plus attributes (gear number, signal side)."""
    lexicon = lexicon or COMPILED
    remaining = " " + text.lower().replace("-", " ") + " "
    found: dict[str, int] = {}
    attributes: dict = {}
    gear_text = signal_text = ""
    for name, patterns in lexicon:
        for pattern in patterns:
            for match in pattern.finditer(remaining):
                found.setdefault(name, match.start())
                if name == "gear":
                    gear_text += " " + remaining[match.start():match.end() + 25]
                if name == "signal":
                    signal_text += " " + remaining[max(0, match.start() - 12):match.end() + 12]
            remaining = pattern.sub(lambda m: " " * len(m.group(0)), remaining)
    if "gear" in found:
        numbers = [ORDINALS[w] for w in TOKEN.findall(gear_text) if w in ORDINALS]
        if numbers:
            attributes["gear"] = numbers[0]
    if "signal" in found:
        side = re.search(r"\b(left|right)\b", signal_text)
        if side:
            attributes["signal_side"] = side.group(1)
    intents = sorted(found, key=found.get)
    intents += [family for family, members in FAMILIES.items() if members & set(intents)]
    return intents, attributes


def tokens(text: str) -> list[str]:
    return TOKEN.findall(text.lower())


# ---------------------------------------------------------------- inputs
def parse_clock(value: str) -> float:
    parts = value.strip().replace(",", ".").split(":")
    seconds = 0.0
    for part in parts:
        seconds = seconds * 60 + float(part)
    return seconds


def read_transcript(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8-sig")
    suffix = path.suffix.lower()
    segments: list[dict] = []
    if suffix == ".json":
        data = json.loads(text)
        rows = data.get("segments", []) if isinstance(data, dict) else data
        for row in rows:
            start = float(row.get("start", row.get("time")))
            segments.append({"start": start, "end": float(row.get("end", start + 2)), "text": str(row["text"]).strip()})
    elif suffix in (".srt", ".vtt"):
        cue = re.compile(r"((?:\d+:)?\d+:\d+[.,]\d+)\s*-->\s*((?:\d+:)?\d+:\d+[.,]\d+)[^\n]*\n(.*?)(?:\n\s*\n|\Z)", re.S)
        for start, end, body in cue.findall(text.replace("\r\n", "\n")):
            body = re.sub(r"<[^>]+>", "", body).strip().replace("\n", " ")
            if body:
                segments.append({"start": parse_clock(start), "end": parse_clock(end), "text": body})
    else:
        dialect = "excel-tab" if suffix == ".tsv" or "\t" in text.splitlines()[0] else "excel"
        for row in csv.DictReader(text.splitlines(), dialect=dialect):
            row = {k.strip().lower(): (v or "").strip() for k, v in row.items() if k}
            start = row.get("start") or row.get("time")
            if not start or not row.get("text"):
                continue
            begin = parse_clock(start)
            segments.append({"start": begin, "end": parse_clock(row["end"]) if row.get("end") else begin + 2, "text": row["text"]})
    segments = [s for s in segments if s["text"] and math.isfinite(s["start"]) and math.isfinite(s["end"])]
    if not segments:
        raise ValueError(f"No timed transcript segments in {path}")
    return sorted(segments, key=lambda s: s["start"])


def transcribe(audio: Path, model: str, language: str | None, translate: bool) -> list[dict]:
    try:
        from faster_whisper import WhisperModel  # optional; downloads model weights on first use
    except ImportError as exc:
        raise SystemExit("--audio needs the optional faster-whisper package; or pass --transcript") from exc
    whisper = WhisperModel(model, device="cpu", compute_type="int8")
    parts, _ = whisper.transcribe(str(audio), language=language, task="translate" if translate else "transcribe",
                                  vad_filter=True)
    return [{"start": p.start, "end": p.end, "text": p.text.strip()} for p in parts if p.text.strip()]


def native_prompt_text(library: Path | None):
    if not library:
        return None
    lib = ctypes.CDLL(str(library))
    lib.ld_prompt_text.argtypes = [ctypes.c_int]
    lib.ld_prompt_text.restype = ctypes.c_char_p
    return lambda prompt: (lib.ld_prompt_text(int(prompt)) or b"").decode()


def read_app(path: Path, include_visual: bool, prompt_text=None) -> tuple[list[dict], float | None, list[str]]:
    """App instructions [{time, text, prompt, duration}], instructor-audio start on that clock, warnings."""
    warnings: list[str] = []
    events: list[dict] = []
    audio_start = None
    if path.suffix.lower() == ".jsonl":
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            event = row.get("event")
            if event == "instructor_audio_start":
                audio_start = float(row["time"])
            elif event == "coaching_accepted" and (include_visual or row.get("voice_submitted")):
                text = row.get("text")
                if text is None and prompt_text and int(row.get("prompt", 0)) < 1000:
                    text = prompt_text(row["prompt"])
                if not text:
                    warnings.append(f"Instruction at {row['time']:.1f} s has no text (older journal or composed utterance); not classified")
                    continue
                events.append({"time": float(row["time"]), "text": text, "prompt": row.get("prompt")})
    elif path.suffix.lower() == ".csv":
        with path.open(encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                events.append({"time": float(row["time"]), "text": row["text"], "prompt": int(row["prompt"]),
                               "duration": float(row.get("duration") or 0) or None})
    else:
        report = json.loads(path.read_text(encoding="utf-8"))
        for row in report.get("events", []):
            events.append({"time": float(row["time"]), "text": row["text"], "prompt": row.get("prompt"),
                           "duration": row.get("duration")})
    for event in events:
        if not event.get("duration"):
            event["duration"] = 0.8 + len(tokens(event["text"])) / 2.6  # spoken words at about 2.6 per second
    return sorted(events, key=lambda e: e["time"]), audio_start, warnings


# ---------------------------------------------------------------- benchmark
def app_words_in(words: list[str], app_text: str) -> set[int]:
    """Indices of segment words that repeat the app's sentence verbatim: runs of at least
    four words in order (or the whole of a shorter instruction). Shared single words such as
    "down" or "second" are not echo; an instructor may well use the same words."""
    said = tokens(app_text)
    run = min(4, len(said))
    if not words or not run:
        return set()
    matcher = difflib.SequenceMatcher(None, words, said, autojunk=False)
    return {i for block in matcher.get_matching_blocks() if block.size >= run for i in range(block.a, block.a + block.size)}


def remove_app_echo(segments: list[dict], app: list[dict], offset: float) -> tuple[list[dict], int, int]:
    """Drop transcript segments that are the app's own voice; strip the app's words from mixed
    segments, so the app is never credited with its own echo."""
    kept, echoes, mixed = [], 0, 0
    for segment in segments:
        start, end = segment["start"] + offset, segment["end"] + offset
        words = tokens(segment["text"])
        echo: set[int] = set()
        for event in app:
            if start <= event["time"] + event["duration"] + 1.5 and end >= event["time"] - 1.0:
                echo |= app_words_in(words, event["text"])
        if words and len(echo) >= .6 * len(words):
            echoes += 1
            continue
        segment = dict(segment)
        if echo:
            mixed += 1
            segment["text"] = " ".join(w for i, w in enumerate(words) if i not in echo)
            segment["mixed_with_app_speech"] = True
        kept.append(segment)
    return kept, echoes, mixed


def match(instructor: list[dict], app: list[dict], intent: str, window: float):
    """Nearest-first one-to-one matching of one intent within +-window seconds."""
    left = [i for i, e in enumerate(instructor) if intent in e["intents"]]
    right = [j for j, e in enumerate(app) if intent in e["intents"]]
    pairs = sorted((abs(app[j]["time"] - instructor[i]["time"]), i, j) for i in left for j in right
                   if abs(app[j]["time"] - instructor[i]["time"]) <= window)
    used_i, used_j, matched = set(), set(), []
    for _, i, j in pairs:
        if i not in used_i and j not in used_j:
            used_i.add(i)
            used_j.add(j)
            matched.append((i, j))
    return matched, [i for i in left if i not in used_i], [j for j in right if j not in used_j]


def benchmark(segments: list[dict], app: list[dict], offset: float, window: float, lexicon=None) -> dict:
    kept, echoes, mixed = remove_app_echo(segments, app, offset)
    instructor = []
    unclassified = []
    for segment in kept:
        intents, attributes = classify(segment["text"], lexicon)
        row = {"time": round(segment["start"] + offset, 2), "audio_time": round(segment["start"], 2), "text": segment["text"],
               "intents": intents, "attributes": attributes}
        if segment.get("mixed_with_app_speech"):
            row["mixed_with_app_speech"] = True
        (instructor if intents else unclassified).append(row)
    app_rows = []
    for event in app:
        intents, attributes = classify(event["text"], lexicon)
        app_rows.append({"time": round(event["time"], 2), "text": event["text"], "prompt": event.get("prompt"),
                         "intents": intents, "attributes": attributes})
    all_intents = sorted({i for row in instructor + app_rows for i in row["intents"]})
    per_intent, lags = {}, []
    matched_instructor, matched_app = set(), set()
    attribute_checks = {"gear": [0, 0], "signal_side": [0, 0]}
    for intent in all_intents:
        pairs, only_instructor, only_app = match(instructor, app_rows, intent, window)
        intent_lags = [app_rows[j]["time"] - instructor[i]["time"] for i, j in pairs]
        if intent not in CONTEXT:
            lags.extend(intent_lags)
            matched_instructor.update(i for i, _ in pairs)
            matched_app.update(j for _, j in pairs)
        key = {"gear": "gear", "signal": "signal_side"}.get(intent)
        for i, j in pairs if key else ():
            a, b = instructor[i]["attributes"].get(key), app_rows[j]["attributes"].get(key)
            if a is not None and b is not None:
                attribute_checks[key][0] += a == b
                attribute_checks[key][1] += 1
        total_instructor, total_app = len(pairs) + len(only_instructor), len(pairs) + len(only_app)
        per_intent[intent] = {
            "kind": "context" if intent in CONTEXT else "not_given_by_design" if intent in NOT_GIVEN_BY_DESIGN
            else "family" if intent in FAMILIES else "action",
            "instructor": total_instructor, "app": total_app, "matched": len(pairs),
            "instructor_only": len(only_instructor), "app_only": len(only_app),
            "recall": round(len(pairs) / total_instructor, 3) if total_instructor else None,
            "precision": round(len(pairs) / total_app, 3) if total_app else None,
            "median_lag_seconds": round(sorted(intent_lags)[len(intent_lags) // 2], 2) if intent_lags else None,
        }
        for i in only_instructor:
            instructor[i].setdefault("unmatched", []).append(intent)
        for j in only_app:
            app_rows[j].setdefault("unmatched", []).append(intent)
    actionable = [i for i, row in enumerate(instructor) if set(row["intents"]) - CONTEXT - NOT_GIVEN_BY_DESIGN]
    app_actionable = [j for j, row in enumerate(app_rows) if set(row["intents"]) - CONTEXT]
    covered = [i for i in actionable if i in matched_instructor]
    agreed = [j for j in app_actionable if j in matched_app]
    lags.sort()
    return {
        "meaning": "Agreement with one instructor's spoken instructions on one drive; not a validation of teaching quality, safety or legality",
        "window_seconds": window, "audio_offset_seconds": offset,
        "transcript_segments": len(segments), "app_echo_removed": echoes, "mixed_segments_stripped": mixed,
        "instructor_instructions": len(instructor), "unclassified_instructor_segments": len(unclassified),
        "app_instructions": len(app_rows),
        "instructor_actions_covered": {"covered": len(covered), "of": len(actionable),
                                       "share": round(len(covered) / len(actionable), 3) if actionable else None},
        "app_actions_agreed": {"agreed": len(agreed), "of": len(app_actionable),
                               "share": round(len(agreed) / len(app_actionable), 3) if app_actionable else None},
        "median_lag_seconds": round(lags[len(lags) // 2], 2) if lags else None,
        "lag_sign": "app time minus instructor time; negative means the app spoke first",
        "attribute_agreement": {k: {"same": v[0], "of": v[1]} for k, v in attribute_checks.items()},
        "per_intent": per_intent,
        "instructor": instructor, "unclassified": unclassified, "app": app_rows,
    }


def markdown(result: dict, title: str) -> str:
    lines = [f"# Instructor benchmark: {title}", "", f"_{result['meaning']}._", ""]
    cov, agr = result["instructor_actions_covered"], result["app_actions_agreed"]
    lines += [f"- Instructor instructions: **{result['instructor_instructions']}** "
              f"({result['unclassified_instructor_segments']} other segments unclassified; {result['app_echo_removed']} app-echo segments removed, "
              f"{result['mixed_segments_stripped']} mixed segments stripped of app words)",
              f"- App instructions: **{result['app_instructions']}**",
              f"- Instructor actions the app also gave within ±{result['window_seconds']:g} s: **{cov['covered']}/{cov['of']}**",
              f"- App actions the instructor also gave: **{agr['agreed']}/{agr['of']}**",
              f"- Median timing (app − instructor): {result['median_lag_seconds']} s", "",
              "| Intent | Kind | Instructor | App | Matched | Instructor only | App only | Median lag s |",
              "|---|---|---:|---:|---:|---:|---:|---:|"]
    for intent, row in sorted(result["per_intent"].items(), key=lambda kv: (kv[1]["kind"], -kv[1]["instructor"])):
        lines.append(f"| {intent} | {row['kind']} | {row['instructor']} | {row['app']} | {row['matched']} | "
                     f"{row['instructor_only']} | {row['app_only']} | {row['median_lag_seconds'] if row['median_lag_seconds'] is not None else ''} |")
    lines += ["", "## Timeline", "", "| Time s | Who | Words | Intents | Unmatched |", "|---:|---|---|---|---|"]
    rows = [(r["time"], "Instructor", r) for r in result["instructor"]] + [(r["time"], "App", r) for r in result["app"]]
    for time, who, row in sorted(rows, key=lambda x: x[0]):
        text = row["text"].replace("|", "/")
        lines.append(f"| {time:.1f} | {who} | {text} | {', '.join(row['intents'])} | {', '.join(row.get('unmatched', []))} |")
    if result["unclassified"]:
        lines += ["", "## Unclassified instructor speech (review; extend the lexicon if these are instructions)", ""]
        lines += [f"- {r['time']:.1f} s: {r['text']}" for r in result["unclassified"]]
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("app", type=Path, help="diagnostics journal (.jsonl), review report.json, or guidance.csv")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--transcript", type=Path, help="Whisper JSON, SRT, VTT, or CSV/TSV (start[,end],text)")
    source.add_argument("--audio", type=Path, help="lesson audio to transcribe with optional faster-whisper")
    parser.add_argument("--whisper-model", default="small")
    parser.add_argument("--language", help="spoken language code for Whisper, e.g. en, ml, hi")
    parser.add_argument("--translate", action="store_true", help="Whisper translates speech to English text")
    parser.add_argument("--audio-offset", type=float, help="app-clock time of audio second 0 (default: journal instructor_audio_start, else 0)")
    parser.add_argument("--window", type=float, default=3.0)
    parser.add_argument("--include-visual", action="store_true", help="count instructions shown but not voiced")
    parser.add_argument("--native-library", type=Path, help="replay bridge library for prompt texts of older journals")
    parser.add_argument("--lexicon", type=Path, help="JSON {intent: [regex, ...]} to extend the lexicon (other languages)")
    parser.add_argument("--output", type=Path, help="write JSON result here")
    parser.add_argument("--markdown", type=Path, help="write a readable report here")
    args = parser.parse_args(argv)
    if not (args.window > 0 and math.isfinite(args.window)):
        parser.error("--window must be positive")
    app, audio_start, warnings = read_app(args.app, args.include_visual, native_prompt_text(args.native_library))
    offset = args.audio_offset if args.audio_offset is not None else audio_start if audio_start is not None else 0.0
    if args.audio_offset is None and audio_start is None and args.app.suffix.lower() == ".jsonl":
        warnings.append("Journal has no instructor_audio_start; assuming audio second 0 is journal time 0")
    segments = read_transcript(args.transcript) if args.transcript else transcribe(args.audio, args.whisper_model, args.language, args.translate)
    lexicon = compile_lexicon(json.loads(args.lexicon.read_text(encoding="utf-8"))) if args.lexicon else None
    result = benchmark(segments, app, offset, args.window, lexicon)
    result["warnings"] = warnings
    result["inputs"] = {"app": str(args.app), "transcript": str(args.transcript or args.audio)}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown(result, args.app.name), encoding="utf-8")
    summary = {k: result[k] for k in ("instructor_instructions", "app_instructions", "instructor_actions_covered",
                                      "app_actions_agreed", "median_lag_seconds", "app_echo_removed", "warnings")}
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
