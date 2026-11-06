"""Generate explicitly synthetic structured evidence; never instructor labels."""
import json
from pathlib import Path
from coaching_contract import empty_world, validate_world

ROOT = Path(__file__).resolve().parents[1]


def candidate(now, prompt=44, actor=1, space=1, kind=1, origin=4, scope=1):
    return {"prompt": prompt, "origin": origin, "identity": {"space": space, "id": actor, "scope": scope},
            "kind": kind, "capture_time": now, "max_age_seconds": .35, "expires_at": None,
            "confidence": .9, "tti_seconds": None, "supported": True, "urgent": False,
            "critical": False, "mechanical_measured": False, "actor_category": 0,
            "evidence_kind": 1, "box": [.45, .4, .08, .35]}


def actor(now, identity=1, space=1, category=0):
    return {"identity": {"space": space, "id": identity, "scope": 1}, "capture_time": now,
            "confidence": .9, "tti_seconds": None, "category": category, "relevant": True,
            "in_path": False, "optical_closing": False, "lead_supported": False,
            "box": [.45, .4, .08, .35]}


def attention(world, now, identity=1):
    world["actors"] = [actor(now, identity)]
    world["candidates"] = [candidate(now, actor=identity)]


def route(world, now, feature=4, event=101, epoch=1, route_id=1):
    world["route"].update(route_id=route_id, event_id=event, epoch=epoch, capture_time=now,
                          distance_m=100., eta_seconds=9., gps_time=now, feature=feature, valid=True)
    world["candidates"] = [candidate(now, prompt=50 if feature == 4 else 51, actor=event, space=4,
                                      kind=4, origin=5, scope=route_id)]
    world["candidates"][0].update(max_age_seconds=2.5, actor_category=-1, box=[0., 0., 0., 0.])


def build_case(name, description, duration, modify):
    observations = []
    for index in range(round(duration * 10) + 1):
        now = round(index / 10, 4)
        world = empty_world(now, sequence=index + 1)
        world["telemetry"].update(speed_kmh=35., speed_valid=True, acceleration_mps2=0.)
        row = {"published_at": now, "revision": 1, "profile": 1, "voice": True,
               "accept_delay_seconds": 0., "playback_delay_seconds": 0., "world": world}
        modify(world, now, row)
        validate_world(world)
        observations.append(row)
    return {"schema_version": 1, "fixture_id": name, "evidence_kind": "synthetic", "description": description,
            "content_provenance": {"origin":"independent_synthetic", "google_maps_content":False,
                                   "license":"project-authored", "source_reference":None},
            "duration_seconds": duration, "tick_hz": 100, "observations": observations, "instructor_labels": None}


def cases():
    result = []
    def add(name, desc, duration, fn):
        result.append(build_case(name, "SYNTHETIC: " + desc + " No instructor truth or physical sensor acquisition.", duration, fn))
    add("straight-silence", "Straight route/no supported situation must allow silence.", 3, lambda w,t,r: None)
    add("same-pedestrian", "One producer-qualified attention actor persists; compare event versus phrase repetition.", 22,
        lambda w,t,r: attention(w,t))
    add("new-pedestrian", "An explicitly new actor replaces the previous actor at five seconds.", 18,
        lambda w,t,r: attention(w,t,1 if t < 5 else 2))
    add("left-turn", "Supplied current route event/feature plus independently supplied fresh GPS clock.", 15,
        lambda w,t,r: route(w,t))
    def reroute(w,t,r):
        route(w,t,5,101,1)
        if 3 <= t < 5:
            w["route"]["valid"] = False
        elif t >= 5:
            route(w,t,4,202,2,2)
    add("turn-reroute", "Right preparation, explicit reroute unavailable interval, then distinct left event/epoch.", 18, reroute)
    def approach(w,t,r):
        attention(w,t)
        a,c=w["actors"][0],w["candidates"][0]
        a.update(in_path=True,optical_closing=True,tti_seconds=3.,confidence=.9)
        c.update(kind=2,prompt=18,urgent=True,tti_seconds=3.,confidence=.9)
    add("reliable-approach", "Explicit in-path optical closing with finite TTI and reliable confidence exercises a supported neutral attention warning.", 4, approach)
    def unreliable(w,t,r):
        approach(w,t,r)
        w["actors"][0].update(in_path=False,optical_closing=True,tti_seconds=3.)
        w["candidates"][0].update(prompt=37,origin=2)
    add("unreliable-approach", "A tactical gap/approach hypothesis with optical closing but explicit out-of-path evidence cannot become a path warning; neutral camera attention is a separate capability.", 3, unreliable)
    def congestion(w,t,r):
        if t >= 1:
            w["candidates"]=[candidate(t,prompt=8,actor=801,space=8,kind=8,origin=2)]
    add("explicit-congestion", "Congestion is supplied by a hypothetical producer after one second; actor count is never used to infer it.", 15, congestion)
    def slow_response(w,t,r):
        attention(w,t)
        w["telemetry"]["speed_kmh"] = max(8., 35. - max(0.,t-2)*2)
        if t >= 4:
            w["telemetry"]["braking"] = True
    add("measured-response", "Measured decreasing speed and later explicit braking; no inference of pedal state or learner correctness.", 8, slow_response)
    def bad_gps(w,t,r):
        route(w,t)
        w["route"].update(valid=False,gps_time=None)
    add("bad-gps", "Route text alone with absent qualified location cannot support a turn cue.", 3, bad_gps)
    def missing(w,t,r):
        w["telemetry"].update(speed_kmh=None,speed_valid=False,pitch_degrees=None,pitch_valid=False)
        w["candidates"]=[candidate(t,prompt=14,actor=701,space=7,kind=7,origin=3)]
    add("missing-sensors", "Numbered/mechanical shift hypothesis without engine evidence must remain unsupported.", 3, missing)
    def contradictory(w,t,r):
        route(w,t,4)
        w["candidates"][0]["prompt"] = 51
    add("contradictory-route", "Right-turn hypothesis contradicts current supplied left-turn feature.", 3, contradictory)
    def stale(w,t,r):
        attention(w,t)
        w["actors"][0]["capture_time"] = 0.
        w["candidates"][0]["capture_time"] = 0.
        r["accept_delay_seconds"] = .5
    add("stale-late-queue", "Delayed acceptance plus old original actor capture must reject stale work.", 3, stale)
    def future(w,t,r):
        attention(w,t)
        w["candidates"][0]["capture_time"] = t+1
    add("future-capture", "Future original capture clock is invalid even when publication order is monotonic.", 3, future)
    def out_of_order(w,t,r):
        attention(w,t)
        if .5 <= t < 1.:
            w["actors"][0]["capture_time"] = .1
            w["candidates"][0]["capture_time"] = .1
        if t >= 1.5:
            w["source_id"] = 2
        if t == 2.:
            w["source_id"] = 1
    add("out-of-order-source", "Out-of-order old capture and one retired-source publication cannot restore old evidence.", 3, out_of_order)
    def concurrent(w,t,r):
        route(w,t)
        ac=actor(t)
        w["actors"]=[ac]
        w["candidates"].append(candidate(t))
    add("concurrent-events", "Concurrent independently supplied route and actor hypotheses exercise bounded arbitration/defer.", 18, concurrent)
    def revision(w,t,r):
        attention(w,t)
        r["playback_delay_seconds"] = .5
        if t >= .2:
            r.update(revision=2,voice=False)
    add("voice-revision", "Settings change before simulated onset must invalidate queued voice; logical caption delivery remains distinct.", 3, revision)
    return result


def main():
    target=ROOT/"scenarios/balanced-coaching"
    target.mkdir(parents=True,exist_ok=True)
    for value in cases():
        (target/(value["fixture_id"]+".json")).write_text(json.dumps(value,separators=(",",":"),allow_nan=False)+"\n",encoding="utf-8")
    print(f"Generated {len(cases())} explicitly synthetic fixtures")


if __name__ == "__main__":
    main()
