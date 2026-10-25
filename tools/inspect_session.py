"""Inspect an exported Android JSONL capture; does not evaluate guidance accuracy."""
import argparse
from collections import Counter
import json
from pathlib import Path

LIMIT = 8 * 1024 * 1024


def inspect(path):
    if path.stat().st_size > LIMIT:
        raise ValueError("Session exceeds the 8 MiB capture budget")
    data = path.read_bytes()
    lines = data.splitlines()
    rows, warnings = [], []

    def invalid(value):
        raise ValueError(f"Non-standard JSON number {value}")

    for index, line in enumerate(lines):
        try:
            if len(line) > 65536+1024:
                raise ValueError("Unbounded journal line")
            row = json.loads(line, parse_constant=invalid)
            if not isinstance(row, dict) or row.get("schema_version") != 1:
                raise ValueError("Unsupported record/schema")
            rows.append(row)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            if index == len(lines)-1 and not data.endswith(b"\n") and rows:
                warnings.append(f"Truncated final record: {exc}")
            else:
                raise ValueError(f"Invalid line {index+1}: {exc}") from exc
        except ValueError as exc:
            raise ValueError(f"Invalid line {index+1}: {exc}") from exc
    if not rows or rows[0].get("event") != "session_start" or not isinstance(rows[0].get("metadata"), dict):
        raise ValueError("Missing session metadata header")
    end = rows[-1] if rows[-1].get("event") == "session_end" else None
    body = rows[1:-1] if end else rows[1:]
    complete = bool(end and end.get("complete") is True and not warnings)
    if not end:
        warnings.append("No clean end marker; capture may have been interrupted")
    else:
        if end.get("session_id") != rows[0].get("session_id") or end.get("records_written") != len(body):
            raise ValueError("Footer session/count mismatch")
        dropped = end.get("records_dropped")
        if not isinstance(dropped, int) or dropped < 0:
            raise ValueError("Invalid dropped-record count")
        if dropped or end.get("limit_reached"):
            if complete:
                raise ValueError("Loss incorrectly marked complete")
            warnings.append("Capture lost records or reached its storage limit")
    decisions, counts = {}, Counter()
    for row in body:
        event = row.get("event")
        if event in ("session_start", "session_end"):
            raise ValueError("Unexpected session boundary inside capture")
        counts[event] += 1
        if event == "decision":
            decisions[(row.get("source_id"), row.get("sequence"))] = row
        elif event == "accepted":
            decision = decisions.get((row.get("source_id"), row.get("sequence")))
            if not decision or not decision.get("eligible") or row.get("prompt") != decision.get("prompt"):
                if complete:
                    raise ValueError("Accepted submission lacks its eligible decision")
                warnings.append("Incomplete capture cannot resolve an accepted submission")
    return {"session_id": rows[0].get("session_id"), "journal_status": "complete" if complete else "partial",
            "metadata": rows[0]["metadata"], "records": dict(counts), "warnings": warnings,
            "meaning_of_complete": "Capture interval drained without reported loss; not a complete drive or accuracy result",
            "audio_evidence": "Accepted submission only; audible playback/completion unverified"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.session)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"Invalid/unreadable session: {exc}\n")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
