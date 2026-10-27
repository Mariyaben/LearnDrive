"""Compare shared Balanced engine and legacy candidate/audio gate on local JSON.

This is a structured candidate-stage test, not video perception, an Android
device test, physical audio evidence or an instructor usefulness benchmark.
"""
import argparse
from collections import Counter
import copy
import ctypes as C
import json
import math
import os
from pathlib import Path
import platform
import time

from coaching_contract import canonical, load_fixture, sha256, validate_world

ROOT = Path(__file__).resolve().parents[1]
MODES = {0: "SILENCE", 1: "COACH", 2: "WARN", 3: "DEFER"}
BLOCKS = {0: "none", 1: "inactive", 2: "stale", 3: "unsupported", 4: "cooldown",
          5: "duplicate", 6: "audio", 7: "revision", 8: "critical_disabled", 9: "driver_response"}


def _known(value, negative=False):
    return (-math.inf if negative else math.inf) if value is None else float(value)


def encode_world(world, now):
    validate_world(world)
    t, r = world["telemetry"], world["route"]
    header = [world["source_id"], world["frame_sequence"], now, _known(t["time"], True),
              _known(t["speed_kmh"]), _known(t["pitch_degrees"]), _known(t["acceleration_mps2"]),
              t["calibrated"], t["speed_valid"], t["pitch_valid"], t["braking"], t["hard_braking"], t["shock"],
              world["active"], world["audio_ready"], world["allow_critical"], world["simulation"],
              r["route_id"], r["event_id"], r["epoch"], _known(r["capture_time"], True),
              _known(r["distance_m"]), _known(r["eta_seconds"]), r["feature"], r["valid"], r["provider"],
              _known(r["gps_time"], True), _known(world["road"]["capture_time"], True),
              _known(world["engine"]["capture_time"], True), world["road"]["bend"], world["road"]["grade_band"],
              world["road"]["valid"], world["engine"]["valid"], _known(world["capture_time"], True)]
    actors, candidates = [], []
    for a in world["actors"]:
        identity = a["identity"]
        actors.extend([identity["space"], identity["id"], identity["scope"], _known(a["capture_time"], True),
                       a["confidence"], _known(a["tti_seconds"]), a["category"], a["relevant"],
                       a["in_path"], a["optical_closing"], a["lead_supported"], *a["box"]])
    for c in world["candidates"]:
        identity = c["identity"]
        candidates.extend([c["prompt"], c["origin"], identity["space"], identity["id"], identity["scope"], c["kind"],
                           _known(c["capture_time"], True), c["max_age_seconds"],
                           (c["capture_time"] + c["max_age_seconds"] if c["expires_at"] is None and c["capture_time"] is not None else _known(c["expires_at"])),
                           c["confidence"], _known(c["tti_seconds"]), c["supported"], c["urgent"], c["critical"],
                           c["mechanical_measured"], c["actor_category"], c["evidence_kind"], *c["box"]])
    return tuple((C.c_double * len(values))(*values) for values in (header, actors, candidates))


def decode_snapshot(packed):
    """Invert the v1 native candidate-stage diagnostic layout (no engine RPM).

    This snapshot comes from perception, not invented detections. Unknown values
    remain None and all normal input validation still applies on admission.
    """
    from coaching_contract import empty_world
    if packed.get('packed_version')!=1 or len(packed.get('header',[]))!=34:raise ValueError('Unsupported packed snapshot')
    h=list(packed['header'])
    for i in (3,20,26,27,28,33):
        if h[i] is not None and h[i]<0:h[i]=None  # Native missing-clock sentinel.
    w=empty_world(h[2],int(h[0]),int(h[1]));w['capture_time']=h[33]
    t=w['telemetry']
    for key,index in [('time',3),('speed_kmh',4),('pitch_degrees',5),('acceleration_mps2',6)]:t[key]=h[index]
    for key,index in [('calibrated',7),('speed_valid',8),('pitch_valid',9),('braking',10),('hard_braking',11),('shock',12)]:t[key]=bool(h[index])
    for key,index in [('active',13),('audio_ready',14),('allow_critical',15),('simulation',16)]:w[key]=bool(h[index])
    for key,index in [('route_id',17),('event_id',18),('epoch',19),('capture_time',20),('distance_m',21),('eta_seconds',22),('feature',23),('gps_time',26)]:w['route'][key]=h[index]
    for key in ('route_id','event_id','epoch','feature'):w['route'][key]=int(w['route'][key])
    w['route'].update(valid=bool(h[24]),provider=bool(h[25]))
    w['road'].update(capture_time=h[27],bend=int(h[29]),grade_band=int(h[30]),valid=bool(h[31]))
    w['engine'].update(capture_time=h[28],valid=bool(h[32]))
    for a in packed['actors']:
        if len(a)!=15:raise ValueError('Invalid actor snapshot')
        w['actors'].append({'identity':{'space':int(a[0]),'id':int(a[1]),'scope':int(a[2])},
            'capture_time':a[3],'confidence':a[4],'tti_seconds':a[5],'category':int(a[6]),'relevant':bool(a[7]),
            'in_path':bool(a[8]),'optical_closing':bool(a[9]),'lead_supported':bool(a[10]),'box':a[11:15]})
    for c in packed['candidates']:
        if len(c)!=21:raise ValueError('Invalid candidate snapshot')
        w['candidates'].append({'prompt':int(c[0]),'origin':int(c[1]),'identity':{'space':int(c[2]),'id':int(c[3]),'scope':int(c[4])},
            'kind':int(c[5]),'capture_time':c[6],'max_age_seconds':c[7],'expires_at':c[8],'confidence':c[9],'tti_seconds':c[10],
            'supported':bool(c[11]),'urgent':bool(c[12]),'critical':bool(c[13]),'mechanical_measured':bool(c[14]),
            'actor_category':int(c[15]),'evidence_kind':int(c[16]),'box':c[17:21]})
    validate_world(w);return w


class NativeBridge:
    def __init__(self, library, baseline=False):
        self.path = Path(library).resolve()
        self.directory = os.add_dll_directory(str(self.path.parent)) if os.name == "nt" else None
        self.lib = C.CDLL(str(self.path))
        P, U = C.c_void_p, C.c_uint64
        signatures = {
            "contract_version": ([], C.c_int), "create": ([C.c_int], P), "destroy": ([P], None),
            "configure": ([P, C.c_int, C.c_int, U], C.c_int),
            "world": ([P, P, C.c_size_t, P, C.c_size_t, P, C.c_size_t], C.c_int),
            "decide": ([P, U], C.c_char_p),
            "accept": ([P, U, C.c_int, C.c_double, C.c_int], C.c_int),
            "playback": ([P, U, C.c_int, C.c_double, U], C.c_int),
            "validate": ([P, C.c_int, C.c_double], C.c_int), "diagnostics": ([P], C.c_char_p)}
        for name, (args, restype) in signatures.items():
            function = getattr(self.lib, "coaching_replay_" + name)
            function.argtypes, function.restype = args, restype
            setattr(self, "_" + name, function)
        if self._contract_version() != 1:
            raise ValueError("Unsupported structured replay bridge contract")
        self.handle = self._create(int(baseline))
        if not self.handle:
            raise RuntimeError("Cannot create structured replay engine")

    def close(self):
        if self.handle:
            self._destroy(self.handle)
            self.handle = None
        if self.directory:
            self.directory.close()
            self.directory = None

    def configure(self, row):
        return bool(self._configure(self.handle, row["profile"], row["voice"], row["revision"]))

    def world(self, world, now):
        h, a, c = encode_world(world, now)
        return bool(self._world(self.handle, h, len(h), a, len(world["actors"]), c, len(world["candidates"])))

    def decide(self, sequence):
        raw = self._decide(self.handle, sequence)
        if not raw:
            raise RuntimeError("Native decision unavailable")
        return json.loads(raw)

    def accept(self, event, now, voice):
        return bool(self._accept(self.handle, event["sequence"], event["prompt"], now, voice))

    def playback(self, event, now):
        return bool(self._playback(self.handle, event["sequence"], event["prompt"], now, event["revision"]))

    def validate(self, index, now):
        return int(self._validate(self.handle, index, now))

    def diagnostics(self):
        return json.loads(self._diagnostics(self.handle))

    def delivery(self,event,now,stage=8):
        # Older frozen comparison binaries have no completion hook. Their own
        # policy acknowledges submission; do not retrofit their semantics.
        function=getattr(self.lib,'coaching_replay_delivery',None)
        if function is None:return None
        function.argtypes=[C.c_void_p,C.c_uint64,C.c_int,C.c_double,C.c_int]
        function.restype=C.c_int
        return bool(function(self.handle,event['sequence'],event['prompt'],now,stage))

    def context(self):
        function=self.lib.coaching_replay_context
        function.argtypes,function.restype=[C.c_void_p],C.c_char_p
        return json.loads(function(self.handle))

    def use_hybrid(self):
        function=self.lib.coaching_replay_use_hybrid
        function.argtypes,function.restype=[C.c_void_p],None
        function(self.handle)

    def proposal(self,proposal,context,world):
        function=self.lib.coaching_replay_proposal
        function.argtypes=[C.c_void_p,*([C.c_int]*3),*([C.c_uint64]*5),C.c_double,C.c_double]
        function.restype=C.c_int
        # For normalized approved wording, bind by the native summary's raw index.
        c=world['candidates'][proposal['candidate_index']] if proposal['candidate_index'] is not None else None
        key=c['identity'] if c else {'space':0,'id':0,'scope':0}
        return bool(function(self.handle,{'SILENCE':0,'COACH':1,'WARN':2,'DEFER':3}[proposal['decision']],proposal['instruction_id'] or 0,
            key['space'],key['id'],key['scope'],context['source_id'],context['revision'],context['generation'],context['time'],context['time']+2))


def percentiles(values):
    ordered = sorted(values)
    if not ordered:
        return {k: None for k in ("p50", "p95", "p99", "max", "mean")}
    return {**{name: ordered[min(len(ordered) - 1, math.ceil(q * len(ordered)) - 1)]
               for name, q in (("p50", .5), ("p95", .95), ("p99", .99))},
            "max": ordered[-1], "mean": sum(ordered) / len(ordered)}


def run(fixture, bridge):
    rows = fixture["observations"]
    row_index, row, settings = -1, None, None
    current_world = None
    trace, logical, onsets, latencies = [], [], [], []
    rejected_input, pending, playback = [], None, None
    modes, blocks = Counter(), Counter()
    response_ticks, duplicate_ticks, deferred_ticks, silence_ticks = 0, 0, 0, 0
    queue_rejected = playback_rejected = 0
    tick_hz = fixture["tick_hz"]
    for tick in range(math.floor(fixture["duration_seconds"] * tick_hz) + 1):
        now = tick / tick_hz
        while row_index + 1 < len(rows) and rows[row_index + 1]["published_at"] <= now + 1e-9:
            row_index += 1
            proposed = rows[row_index]
            new_settings = (proposed["profile"], proposed["voice"], proposed["revision"])
            if new_settings != settings:
                if bridge.configure(proposed):
                    settings = new_settings
                else:
                    rejected_input.append({"time": now, "kind": "configuration", "revision": proposed["revision"]})
            if bridge.world(proposed["world"], now):
                current_world, row = copy.deepcopy(proposed["world"]), proposed
            else:
                rejected_input.append({"time": now, "kind": "source_or_world", "source_id": proposed["world"]["source_id"]})
        if current_world is None:
            continue
        if not bridge.world(current_world, now):
            raise RuntimeError("Previously admitted world became invalid")
        if pending and pending["due"] <= now + 1e-9:
            event = pending["event"]
            voice_submitted = settings[1] and current_world["audio_ready"]
            if bridge.accept(event, now, voice_submitted):
                logical.append({**event, "accepted_at": now, "voice_submitted": voice_submitted})
                if voice_submitted:
                    playback = {"due": now + pending["playback_delay"], "event": event}
            else:
                queue_rejected += 1
            pending = None
        if playback and playback["due"] <= now + 1e-9:
            if bridge.playback(playback["event"], now):
                onsets.append({**playback["event"], "onset_at": now})
            else:
                playback_rejected += 1
            playback = None
        started = time.perf_counter_ns()
        result = bridge.decide(tick + 1)
        latency = (time.perf_counter_ns() - started) / 1000
        latencies.append(latency)
        result.update(time=now, mode_name=MODES[result["mode"]], block_name=BLOCKS[result["blocked"]])
        trace.append(result)
        modes[result["mode_name"]] += 1
        blocks[result["block_name"]] += 1
        response_ticks += result["blocked"] == 9
        duplicate_ticks += result["blocked"] == 5
        deferred_ticks += result["mode"] == 3
        silence_ticks += result["mode"] == 0
        if result["eligible"] and pending is None:
            event = {k: result[k] for k in ("time", "sequence", "source_id", "revision", "prompt", "event_number", "capture_time")}
            delay = row["accept_delay_seconds"]
            if delay == 0:
                voice_submitted = settings[1] and current_world["audio_ready"]
                if bridge.accept(event, now, voice_submitted):
                    logical.append({**event, "accepted_at": now, "voice_submitted": voice_submitted})
                    if voice_submitted:
                        playback = {"due": now + row["playback_delay_seconds"], "event": event}
                        if row["playback_delay_seconds"] == 0:
                            if bridge.playback(event, now):
                                onsets.append({**event, "onset_at": now})
                            else:
                                playback_rejected += 1
                            playback = None
                else:
                    queue_rejected += 1
            else:
                pending = {"due": now + delay, "event": event, "playback_delay": row["playback_delay_seconds"]}
    result = {"decision_ticks": len(trace), "mode_ticks": dict(modes), "block_ticks": dict(blocks),
              "silence_ticks": silence_ticks, "duplicate_ticks": duplicate_ticks,
              "deferred_ticks": deferred_ticks, "driver_response_ticks": response_ticks,
              "logical_events": logical, "logical_accepts": len(logical),
              "voice_submissions": sum(e["voice_submitted"] for e in logical),
              "simulated_validated_onsets": len(onsets), "onsets": onsets,
              "queue_rejections": queue_rejected, "playback_rejections": playback_rejected,
              "undrained_queue_items": int(pending is not None) + int(playback is not None),
              "input_rejections": rejected_input, "decision_call_us": percentiles(latencies),
              "native_diagnostics": bridge.diagnostics(),
              "trace_sha256": __import__("hashlib").sha256(canonical(trace).encode()).hexdigest()}
    result["accepted_capture_age_seconds"] = percentiles([e["accepted_at"] - e["capture_time"] for e in logical if e["capture_time"] is not None])
    return result, trace


def provenance(library, fixture_paths):
    files = [Path(__file__), ROOT / "tools/coaching_contract.py", ROOT / "desktop/coaching_replay_bridge.cpp"]
    files += sorted(p for base in (ROOT / "core/src", ROOT / "core/include") for p in base.rglob("*") if p.is_file())
    return {"host": platform.platform(), "python": platform.python_version(),
            "library": {"path": str(Path(library).resolve()), "sha256": sha256(library)},
            "source_sha256": {str(p.relative_to(ROOT)).replace("\\", "/"): sha256(p) for p in files},
            "fixture_sha256": {str(p.resolve()): sha256(p) for p in fixture_paths}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixtures", nargs="+", type=Path)
    parser.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_coaching_replay.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "build/balanced-coaching/structured-replay")
    args = parser.parse_args()
    before = provenance(args.library, args.fixtures)
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = []
    for path in args.fixtures:
        fixture = load_fixture(path)
        paths = {}
        for baseline, name in ((True, "legacy_candidate_audio_gate"), (False, "balanced")):
            bridge = NativeBridge(args.library, baseline)
            try:
                paths[name], trace = run(fixture, bridge)
            finally:
                bridge.close()
            (args.output / f"{path.stem}-{name}.jsonl").write_text("".join(canonical(row) + "\n" for row in trace), encoding="utf-8")
        summaries.append({"fixture_id": fixture["fixture_id"], "evidence_kind": fixture["evidence_kind"],
                          "description": fixture["description"], "input_sha256": sha256(path), "results": paths})
        print(fixture["fixture_id"], "legacy", paths["legacy_candidate_audio_gate"]["logical_accepts"],
              "balanced", paths["balanced"]["logical_accepts"], flush=True)
    if provenance(args.library, args.fixtures) != before:
        raise RuntimeError("Reviewed sources, binary or fixture changed during replay")
    report = {"schema_version": 1, "provenance": before, "runs": summaries,
              "instructor_labels": "unavailable; no accuracy/usefulness metrics computed",
              "limits": ["Identical supplied structured candidate/evidence inputs; no detector, model, camera, route acquisition or Android scheduling execution",
                         "Legacy comparison uses existing global/phrase audio gate and prompt priority after the shared validator; detector-derived distinct-entry exception and full old coordinator are not reproduced",
                         "Synthetic timestamps and simulated validated onsets do not establish physical audio, driver response, usefulness or thermal performance",
                         "Decision timing includes Python/ctypes/JSON overhead on this host; differences do not establish phone savings"]}
    (args.output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
