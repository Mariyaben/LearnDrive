"""Validated metadata and clocks for local recorded-video evaluation.

These contracts do not establish source calibration or driving safety. They
prevent stale inference and missing telemetry from being reported as current.
"""
from bisect import bisect_right
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import uuid

import numpy as np

CACHE_SCHEMA = 2


def file_sha256(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        for part in iter(lambda: source.read(1024 * 1024), b""):
            result.update(part)
    return result.hexdigest()


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def inference_key(provenance):
    return hashlib.sha256(canonical_json(provenance).encode("utf-8")).hexdigest()


def inference_provenance(root, video, assets, library, geometry, fps, semantic_hz,
                         spatial, runtime, decoder, reviewer):
    root, assets, library = Path(root), Path(assets), Path(library)
    sources = sorted(p for base in (root / "core/src", root / "core/include")
                     for p in base.rglob("*") if p.is_file())
    sources += [root / "desktop/replay_bridge.cpp", Path(reviewer),
                Path(__file__)]
    source_hashes = {str(p.relative_to(root)).replace("\\", "/"): file_sha256(p) for p in sources}
    native_runtime = {p.name: file_sha256(p) for p in sorted(library.parent.glob("onnxruntime*.dll"))}
    return {"schema_version": CACHE_SCHEMA,
            "video_sha256": file_sha256(video),
            "models": {"detector.onnx": file_sha256(assets / "detector.onnx"),
                       "supercombo.onnx": file_sha256(assets / "supercombo.onnx") if spatial else None},
            "native_library_sha256": file_sha256(library),
            "native_runtime_sha256": native_runtime, "source_sha256": source_hashes,
            "runtime": runtime, "decoder": decoder,
            "input": {"spatial": bool(spatial), "geometry": list(geometry) if spatial else None,
                      "analysis_fps": int(fps), "semantic_max_hz": float(semantic_hz),
                      "pixels": "ffmpeg packed limited-range BT.601 yuv420p; unrotated stream pixels"}}


def cache_metadata(provenance, output):
    return {"schema_version": CACHE_SCHEMA, "provenance": provenance,
            "provenance_sha256": inference_key(provenance),
            "creation_id": uuid.uuid4().hex,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "created_for_output": str(Path(output).resolve())}


def array_fingerprints(arrays):
    return {name: {"shape": list(value.shape), "dtype": str(value.dtype),
                   "sha256": hashlib.sha256(np.ascontiguousarray(value).reshape(-1).view(np.uint8)).hexdigest()}
            for name, value in sorted(arrays.items())}


def expected_semantic_indices(frame_count, fps, semantic_hz):
    indices, last = [], -math.inf
    for i in range(frame_count):
        timestamp = i / fps
        if timestamp - last >= 1 / semantic_hz - 1e-9:
            indices.append(i)
            last = timestamp
    return np.asarray(indices, dtype=np.int64)


def load_cache(path, provenance):
    """Load only strict, same-provenance inference. No pickle or silent fallback."""
    try:
        with np.load(path, allow_pickle=False) as source:
            if set(source.files) != {"metadata", "outputs", "detections", "semantic_indices", "inference_ms"}:
                raise ValueError("missing/extra cache fields; legacy cache needs regeneration")
            raw_metadata = source["metadata"]
            if raw_metadata.shape != () or raw_metadata.dtype.kind != "U":
                raise ValueError("cache metadata must be a scalar JSON string")
            metadata = json.loads(str(raw_metadata.item()))
            if metadata.get("schema_version") != CACHE_SCHEMA or metadata.get("provenance") != provenance:
                raise ValueError("cache provenance does not match current inputs/binaries/runtime")
            if metadata.get("provenance_sha256") != inference_key(provenance):
                raise ValueError("cache provenance checksum is invalid")
            if not all(isinstance(metadata.get(k), str) and metadata[k] for k in
                       ("creation_id", "created_at_utc", "created_for_output")):
                raise ValueError("cache origin is missing")
            if uuid.UUID(hex=metadata["creation_id"]).hex != metadata["creation_id"] or not Path(metadata["created_for_output"]).is_absolute():
                raise ValueError("cache origin is malformed")
            if datetime.fromisoformat(metadata["created_at_utc"]).utcoffset() is None:
                raise ValueError("cache creation time has no timezone")
            arrays = {name: source[name].copy() for name in
                      ("outputs", "detections", "semantic_indices", "inference_ms")}
        outputs, detections = arrays["outputs"], arrays["detections"]
        indices, timings = arrays["semantic_indices"], arrays["inference_ms"]
        spatial = provenance["input"]["spatial"]
        if outputs.dtype != np.float32 or outputs.ndim != 2 or outputs.shape[0] == 0 or outputs.shape[1] != (6472 if spatial else 0):
            raise ValueError("invalid model output shape/type")
        if indices.dtype != np.int64 or indices.ndim != 1:
            raise ValueError("invalid semantic frame indices")
        expected = expected_semantic_indices(len(outputs), provenance["input"]["analysis_fps"],
                                             provenance["input"]["semantic_max_hz"])
        if not np.array_equal(indices, expected):
            raise ValueError("semantic frame indices do not match sampling contract")
        if detections.dtype != np.float32 or detections.ndim != 3 or detections.shape[0] != len(indices) or detections.shape[1] != 84 or not 0 < detections.shape[2] <= 30000:
            raise ValueError("invalid raw detector output shape/type")
        if timings.ndim != 1 or len(timings) != (len(outputs) if spatial else len(indices)):
            raise ValueError("invalid timing sample count")
        if not all(np.isfinite(a).all() for a in (outputs, detections, timings)) or np.any(timings < 0):
            raise ValueError("nonfinite/negative inference values")
        if metadata.get("arrays") != array_fingerprints(arrays):
            raise ValueError("cache payload does not match its recorded array fingerprints")
        return arrays, metadata
    except Exception as error:
        raise ValueError(f"Invalid inference cache {path}: {error}. Use --fresh-inference or a new output parent.") from error


def save_cache(path, provenance, output, *, replace_existing=False, **arrays):
    path = Path(path)
    metadata = cache_metadata(provenance, output)
    metadata["arrays"] = array_fingerprints(arrays)
    pending = path.with_name(path.name + "." + metadata["creation_id"] + ".part")
    try:
        with pending.open("wb") as target:
            np.savez_compressed(target, metadata=np.asarray(canonical_json(metadata)), **arrays)
        # Validate the complete file before making it available to another run.
        load_cache(pending, provenance)
        if replace_existing:
            pending.replace(path)
        else:
            # Publish once without replacing a complete cache another worker
            # may be reading. NTFS denies replacing that open file on Windows.
            # A hard link atomically creates the final name only if absent.
            try:
                os.link(pending, path)
            except FileExistsError:
                _, winner = load_cache(path, provenance)
                return winner
    finally:
        if pending.exists():
            pending.unlink()
    return metadata


@dataclass(frozen=True)
class SpeedObservation:
    speed_kmh: float | None
    sample_time: float | None
    age_seconds: float | None
    source: str


class SpeedTimeline:
    def __init__(self, samples=(), manual_speed=None, max_age_seconds=1.5):
        if not math.isfinite(max_age_seconds) or max_age_seconds <= 0:
            raise ValueError("Speed freshness must be a finite positive duration")
        if manual_speed is not None and (samples or not math.isfinite(manual_speed) or not 0 <= manual_speed <= 160):
            raise ValueError("Use recorded samples OR a finite 0-160 manual test speed")
        self.samples = list(samples)
        for i, (timestamp, speed) in enumerate(self.samples):
            if not math.isfinite(timestamp) or timestamp < 0 or not math.isfinite(speed) or not 0 <= speed <= 160:
                raise ValueError("Invalid recorded speed sample")
            if i and timestamp <= self.samples[i - 1][0]:
                raise ValueError("Recorded speed samples must be strictly ordered")
        self.times = [r[0] for r in self.samples]
        self.manual_speed = manual_speed
        self.max_age_seconds = max_age_seconds

    def at(self, recorded_time):
        if not math.isfinite(recorded_time) or recorded_time < 0:
            return SpeedObservation(None, None, None, "unknown; before recording")
        if self.manual_speed is not None:
            return SpeedObservation(self.manual_speed, None, None, "assumed manual test input; NOT recorded speed")
        index = bisect_right(self.times, recorded_time) - 1
        if index < 0:
            return SpeedObservation(None, None, None, "unknown; no past recorded speed sample")
        timestamp, speed = self.samples[index]
        age = recorded_time - timestamp
        if age >= self.max_age_seconds:
            return SpeedObservation(None, timestamp, age, "unknown; recorded speed sample expired")
        return SpeedObservation(speed, timestamp, age, "reviewed overlay CSV; held within explicit freshness limit")


def publication_events(frame_count, fps, semantic_indices, semantic_latency, spatial_extra_latency, spatial):
    """Ideal fixed-rate completion timeline; not Android serial/drop scheduling."""
    if not math.isfinite(semantic_latency) or semantic_latency < 0 or not math.isfinite(spatial_extra_latency) or spatial_extra_latency < 0:
        raise ValueError("Publication delays must be finite and nonnegative")
    semantic = set(int(i) for i in semantic_indices)
    events = []
    for index in range(frame_count):
        capture = index / fps
        if index in semantic:
            events.append((capture + semantic_latency, 0, index, "semantic"))
        if spatial:
            events.append((capture + semantic_latency + spatial_extra_latency, 1, index, "spatial"))
        events.append((capture + semantic_latency + (spatial_extra_latency if spatial else 0), 2, index, "observation"))
    return sorted(events)
