"""Strict local structured-coaching contracts; no perception or instructor truth.

Numeric enum values mirror coaching/CoachingEngine.hpp. JSON null is unknown;
Infinity/NaN, duplicate keys and implicit sensor qualification are rejected.
"""
import copy
import hashlib
import json
import math
from pathlib import Path

SCHEMA_VERSION = 1
MAX_BYTES = 8 * 1024 * 1024
WORLD_FIELDS = {"source_id", "frame_sequence", "capture_time", "telemetry", "active", "audio_ready",
                "allow_critical", "simulation", "route", "road", "engine", "actors", "candidates"}
TELEMETRY_FIELDS = {"time", "speed_kmh", "pitch_degrees", "acceleration_mps2",
                    "calibrated", "speed_valid", "pitch_valid", "braking", "hard_braking", "shock"}
ROUTE_FIELDS = {"route_id", "event_id", "epoch", "capture_time", "distance_m", "eta_seconds",
                "gps_time", "feature", "valid", "provider"}
IDENTITY_FIELDS = {"space", "id", "scope"}
ACTOR_FIELDS = {"identity", "capture_time", "confidence", "tti_seconds", "category",
                "relevant", "in_path", "optical_closing", "lead_supported", "box"}
CANDIDATE_FIELDS = {"prompt", "origin", "identity", "kind", "capture_time", "max_age_seconds",
                    "expires_at", "confidence", "tti_seconds", "supported", "urgent", "critical",
                    "mechanical_measured", "actor_category", "evidence_kind", "box"}
PROPOSAL_FIELDS = {"schema_version", "mode", "candidate_index", "rationale"}


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_json(text):
    return json.loads(text, object_pairs_hook=_pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"Invalid number {value}")))


def _object(value, fields, name):
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError(f"{name}: fields must be exactly {sorted(fields)}")


def _number(value, name, minimum=None, maximum=None, nullable=False):
    if value is None and nullable:
        return
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name}: finite number required")
    if minimum is not None and value < minimum or maximum is not None and value > maximum:
        raise ValueError(f"{name}: number out of range")


def _integer(value, name, minimum=0, maximum=2**53 - 1):
    if not isinstance(value, int) or isinstance(value, bool) or not minimum <= value <= maximum:
        raise ValueError(f"{name}: bounded integer required")


def _booleans(value, names, label):
    if any(not isinstance(value[key], bool) for key in names):
        raise ValueError(f"{label}: explicit boolean qualification required")


def validate_identity(value):
    _object(value, IDENTITY_FIELDS, "identity")
    _integer(value["space"], "identity space", 0, 8)
    _integer(value["id"], "identity id")
    _integer(value["scope"], "identity scope")


def validate_candidate(value):
    _object(value, CANDIDATE_FIELDS, "candidate")
    validate_identity(value["identity"])
    for key, low, high in (("prompt", 1, 55), ("origin", 0, 5), ("kind", 0, 8),
                           ("actor_category", -1, 79), ("evidence_kind", 0, 3)):
        _integer(value[key], key, low, high)
    _number(value["capture_time"], "candidate capture", 0, nullable=True)
    _number(value["max_age_seconds"], "candidate max age", .001, 60)
    _number(value["expires_at"], "candidate expiry", 0, nullable=True)
    _number(value["confidence"], "candidate confidence", 0, 1)
    _number(value["tti_seconds"], "candidate TTI", 0, nullable=True)
    _booleans(value, ("supported", "urgent", "critical", "mechanical_measured"), "candidate")
    if not isinstance(value["box"], list) or len(value["box"]) != 4:
        raise ValueError("candidate box requires four normalized coordinates")
    for number in value["box"]:
        _number(number, "box", 0, 1)


def validate_world(world):
    _object(world, WORLD_FIELDS, "world")
    _integer(world["source_id"], "source", 1)
    _integer(world["frame_sequence"], "frame sequence")
    _number(world["capture_time"], "scene capture time", 0, nullable=True)
    _booleans(world, ("active", "audio_ready", "allow_critical", "simulation"), "world")
    t = world["telemetry"]
    _object(t, TELEMETRY_FIELDS, "telemetry")
    _number(t["time"], "telemetry time", 0, nullable=True)
    _number(t["speed_kmh"], "speed", 0, 160, nullable=True)
    _number(t["pitch_degrees"], "pitch", -30, 30, nullable=True)
    _number(t["acceleration_mps2"], "acceleration", -40, 40, nullable=True)
    _booleans(t, ("calibrated", "speed_valid", "pitch_valid", "braking", "hard_braking", "shock"), "telemetry")
    if t["speed_valid"] and (t["speed_kmh"] is None or t["time"] is None):
        raise ValueError("valid speed requires a measurement and original clock")
    if t["pitch_valid"] and (t["pitch_degrees"] is None or t["time"] is None or not t["calibrated"]):
        raise ValueError("valid pitch requires an explicit calibration and measurement")
    route = world["route"]
    _object(route, ROUTE_FIELDS, "route")
    for key in ("route_id", "event_id", "epoch"):
        _integer(route[key], f"route {key}")
    _integer(route["feature"], "route feature", 0, 5)
    for key in ("capture_time", "distance_m", "eta_seconds", "gps_time"):
        _number(route[key], f"route {key}", 0, nullable=True)
    _booleans(route, ("valid", "provider"), "route")
    road, engine = world["road"], world["engine"]
    _object(road, {"capture_time", "bend", "grade_band", "valid"}, "road")
    _object(engine, {"capture_time", "valid"}, "engine")
    _number(road["capture_time"], "road capture", 0, nullable=True)
    _integer(road["bend"], "road bend", -1, 1)
    _integer(road["grade_band"], "grade band", -4, 4)
    _booleans(road, ("valid",), "road")
    _number(engine["capture_time"], "engine capture", 0, nullable=True)
    _booleans(engine, ("valid",), "engine")
    for key, limit in (("actors", 64), ("candidates", 8)):
        if not isinstance(world[key], list) or len(world[key]) > limit:
            raise ValueError(f"{key}: bounded list required")
    identities = set()
    for actor in world["actors"]:
        _object(actor, ACTOR_FIELDS, "actor")
        validate_identity(actor["identity"])
        identity = tuple(actor["identity"][k] for k in ("space", "id", "scope"))
        if identity in identities:
            raise ValueError("duplicate actor identity")
        identities.add(identity)
        _number(actor["capture_time"], "actor capture", 0, nullable=True)
        _number(actor["confidence"], "actor confidence", 0, 1)
        _number(actor["tti_seconds"], "actor TTI", 0, nullable=True)
        _integer(actor["category"], "actor category", -1, 79)
        _booleans(actor, ("relevant", "in_path", "optical_closing", "lead_supported"), "actor")
        if not isinstance(actor["box"], list) or len(actor["box"]) != 4:
            raise ValueError("actor box requires four normalized coordinates")
        for number in actor["box"]:
            _number(number, "actor box", 0, 1)
    for candidate in world["candidates"]:
        validate_candidate(candidate)
    return world


def load_fixture(path):
    path = Path(path)
    if path.stat().st_size > MAX_BYTES:
        raise ValueError("fixture exceeds 8 MiB")
    value = strict_json(path.read_text(encoding="utf-8-sig"))
    _object(value, {"schema_version", "fixture_id", "evidence_kind", "description", "duration_seconds",
                    "tick_hz", "observations", "instructor_labels", "content_provenance"}, "fixture")
    if value["schema_version"] != 1 or value["evidence_kind"] not in ("synthetic", "recorded_structured"):
        raise ValueError("unsupported fixture schema/evidence kind")
    if not isinstance(value["fixture_id"], str) or not value["fixture_id"] or not isinstance(value["description"], str):
        raise ValueError("fixture needs id and provenance description")
    if value["instructor_labels"] is not None:
        raise ValueError("labels must be supplied separately with their own reviewed provenance")
    provenance = value["content_provenance"]
    _object(provenance, {"origin", "google_maps_content", "license", "source_reference"}, "content provenance")
    if provenance["google_maps_content"] is not False:
        raise ValueError("Google Maps content/exports are not accepted by this replay contract")
    if provenance["origin"] not in ("independent_synthetic", "consented_original", "independent_licensed"):
        raise ValueError("explicit independent content origin required")
    if not isinstance(provenance["license"], str) or not provenance["license"]:
        raise ValueError("content license/provenance required")
    if value["evidence_kind"] == "synthetic" and provenance != {
        "origin":"independent_synthetic", "google_maps_content":False,
        "license":"project-authored", "source_reference":None}:
        raise ValueError("synthetic input must declare independent project-authored provenance")
    if value["evidence_kind"] == "recorded_structured" and (provenance["origin"] == "independent_synthetic" or not isinstance(provenance["source_reference"], str) or not provenance["source_reference"]):
        raise ValueError("recorded observations need original/independent source references")
    _number(value["duration_seconds"], "duration", .01, 3600)
    _integer(value["tick_hz"], "tick rate", 1, 100)
    rows = value["observations"]
    if not isinstance(rows, list) or not 1 <= len(rows) <= 100000:
        raise ValueError("bounded nonempty observations required")
    previous = -1
    for row in rows:
        _object(row, {"published_at", "revision", "profile", "voice", "accept_delay_seconds",
                      "playback_delay_seconds", "world"}, "observation")
        _number(row["published_at"], "publication", 0, value["duration_seconds"])
        if row["published_at"] <= previous:
            raise ValueError("publications must be strictly monotonic; capture clocks may be stale/out of order")
        previous = row["published_at"]
        _integer(row["revision"], "revision", 1)
        _integer(row["profile"], "profile", 0, 2)
        _booleans(row, ("voice",), "settings")
        _number(row["accept_delay_seconds"], "accept delay", 0, 30)
        _number(row["playback_delay_seconds"], "playback delay", 0, 30)
        validate_world(row["world"])
    return value


def validate_shadow_fixture(value):
    # There is deliberately no Android-session/Google-export conversion path.
    # Admit only the implemented independent synthetic producer contract. A
    # recorded-source licence/consent validator must precede a future extension.
    if value.get("evidence_kind") != "synthetic" or value.get("content_provenance") != {
        "origin":"independent_synthetic", "google_maps_content":False,
        "license":"project-authored", "source_reference":None}:
        raise ValueError("Shadow model evaluation accepts only independent project-authored synthetic fixtures; Google exports and unverified recorded content are rejected")
    return value


def validate_proposal(value, candidate_count):
    _object(value, PROPOSAL_FIELDS, "shadow proposal")
    if value["schema_version"] != 1 or value["mode"] not in ("SILENCE", "COACH", "WARN", "DEFER"):
        raise ValueError("unsupported shadow proposal schema/mode")
    if not isinstance(value["rationale"], str) or len(value["rationale"]) > 512:
        raise ValueError("bounded diagnostic rationale required")
    if value["mode"] in ("SILENCE", "DEFER"):
        if value["candidate_index"] is not None:
            raise ValueError("silence/defer cannot propose a candidate")
    else:
        _integer(value["candidate_index"], "candidate index", 0, candidate_count - 1)
    return value


def validate_annotation(value, require_reviewed=False):
    fields = {"schema_version", "annotation_id", "source_sha256", "split", "reviewer", "status",
              "reviewed_interval", "encounter_id", "actor_id", "useful_window", "decision",
              "allowed_prompt_ids", "rationale", "uncertainty", "adjudication"}
    _object(value, fields, "annotation")
    if value["schema_version"] != 1 or value["status"] not in ("pending", "reviewed", "uncertain", "adjudicated"):
        raise ValueError("invalid annotation schema/status")
    _object(value["reviewer"], {"role", "id"}, "reviewer")
    if value["reviewer"]["role"] not in (None, "qualified_instructor", "adjudicator", "engineer"):
        raise ValueError("unknown reviewer role")
    for field in ("annotation_id", "source_sha256", "encounter_id", "actor_id", "rationale", "uncertainty"):
        if value[field] is not None and (not isinstance(value[field], str) or len(value[field]) > 4096):
            raise ValueError(f"{field} must be a bounded string or null")
    if value["reviewer"]["id"] is not None and not isinstance(value["reviewer"]["id"], str):
        raise ValueError("reviewer id must be a string or null")
    if value["adjudication"] is not None and not isinstance(value["adjudication"], dict):
        raise ValueError("adjudication must preserve structured reviewer evidence")
    if value["split"] not in (None, "development", "validation", "held_out"):
        raise ValueError("invalid split")
    if value["decision"] not in (None, "SILENCE", "COACH", "WARN", "DEFER"):
        raise ValueError("invalid annotation decision")
    for field in ("reviewed_interval", "useful_window"):
        window = value[field]
        if window is not None:
            if not isinstance(window, list) or len(window) != 2:
                raise ValueError("window must contain start/end")
            for point in window:
                _number(point, field, 0)
            if window[1] < window[0]:
                raise ValueError("reversed annotation window")
    if not isinstance(value["allowed_prompt_ids"], list):
        raise ValueError("allowed prompts must be a list")
    for prompt in value["allowed_prompt_ids"]:
        _integer(prompt, "allowed prompt", 1, 55)
    if len(set(value["allowed_prompt_ids"])) != len(value["allowed_prompt_ids"]):
        raise ValueError("duplicate allowed prompt")
    if value["useful_window"] is not None and value["reviewed_interval"] is not None and not (
        value["reviewed_interval"][0] <= value["useful_window"][0] <= value["useful_window"][1] <= value["reviewed_interval"][1]):
        raise ValueError("useful window must be inside independently reviewed exposure")
    if value["status"] == "pending":
        if require_reviewed:
            raise ValueError("pending instructor labels are unavailable")
        return value
    if value["reviewer"]["role"] not in ("qualified_instructor", "adjudicator") or not value["reviewer"]["id"]:
        raise ValueError("reviewed annotation requires qualified reviewer role/id")
    if not isinstance(value["source_sha256"], str) or len(value["source_sha256"]) != 64 or any(c not in "0123456789abcdef" for c in value["source_sha256"]):
        raise ValueError("reviewed annotation requires pinned source")
    if not all(value[k] is not None and value[k] != "" for k in
               ("annotation_id", "reviewed_interval", "decision", "rationale", "uncertainty", "split")):
        raise ValueError("reviewed label needs timing, decision, rationale and explicit uncertainty")
    if value["decision"] in ("COACH", "WARN") and (value["useful_window"] is None or not value["allowed_prompt_ids"] or value["encounter_id"] is None):
        raise ValueError("coaching label requires encounter, useful window and approved actions")
    return value


def empty_world(now=0., source=1, sequence=1):
    return {"source_id": source, "frame_sequence": sequence, "capture_time": now,
            "telemetry": {"time": now, "speed_kmh": None, "pitch_degrees": None, "acceleration_mps2": None,
                          "calibrated": False, "speed_valid": False, "pitch_valid": False,
                          "braking": False, "hard_braking": False, "shock": False},
            "active": True, "audio_ready": True, "allow_critical": False, "simulation": True,
            "route": {"route_id": 0, "event_id": 0, "epoch": 0, "capture_time": None,
                      "distance_m": None, "eta_seconds": None, "gps_time": None, "feature": 0, "valid": False, "provider": False},
            "road": {"capture_time": None, "bend": 0, "grade_band": 0, "valid": False},
            "engine": {"capture_time": None, "valid": False},
            "actors": [], "candidates": []}


def copy_world(world):
    return copy.deepcopy(world)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Validate an annotation/template; never fill human labels automatically")
    parser.add_argument("annotation", type=Path)
    parser.add_argument("--require-reviewed", action="store_true")
    args = parser.parse_args()
    if args.annotation.stat().st_size > MAX_BYTES:
        raise ValueError("annotation exceeds size budget")
    annotation = validate_annotation(strict_json(args.annotation.read_text(encoding="utf-8-sig")), args.require_reviewed)
    print("valid", annotation["status"], "labels unavailable" if annotation["status"] == "pending" else "review provenance must be independently verified")
