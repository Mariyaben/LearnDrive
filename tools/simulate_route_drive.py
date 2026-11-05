"""Drive LearnDrive's coaching brain along a REAL road route, the way the phone feeds it.

A route between two points is fetched from an OpenStreetMap router (OSRM; turn-by-turn
steps and road geometry, like Google Navigation's route), and the roads and mapped speed
bumps around it from OpenStreetMap (Overpass). A simulated learner drives it: cruising at
learner speeds, slowing for turns, sharp bends and bumps, accelerating gently. Every 0.5 s
the brain receives what Google Navigation gives on the phone (the next maneuver, metres and
seconds to it) and a GPS fix at the car's real position; camera frames show an empty road.
The native core is the same one the Android app runs (through the replay bridge).

Output: every instruction the brain speaks (time, position, speed, what is ahead), and a
check of every turn, sharp bend and speed bump on the route: was the learner prepared in
time, in the right order? Results go to build/route-sims/<name>/.

This is a simulation: map completeness, learner behaviour and Google's own feed timing on
a phone are not reproduced. No road users appear (camera coaching is tested elsewhere).
"""
from __future__ import annotations

import argparse
import bisect
import ctypes as C
import json
import math
import sys
import time
import urllib.parse
import urllib.error
import urllib.request
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import review_video as rv  # noqa: E402  (native bindings shared with the video replay)

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "android/app/src/main/assets"
P = C.c_void_p
USER_AGENT = "LearnDrive route simulation"
ROAD_CLASSES = {"motorway": 0, "trunk": 1, "primary": 2, "secondary": 3, "tertiary": 4, "unclassified": 5,
                "residential": 6, "living_street": 7, "service": 8, "track": 9}
# Native maneuver codes (same mapping the Android app applies to Google's maneuvers).
LEFT, RIGHT, FORK, RAMP, MERGE, UTURN, ROUNDABOUT, STRAIGHT, DESTINATION = 1, 2, 3, 4, 5, 6, 7, 8, 9


# ---------------------------------------------------------------- data
def fetch_json(url: str, data: bytes | None = None, cache: Path | None = None):
    if cache and cache.exists():
        return json.loads(cache.read_text(encoding="utf-8"))
    payload, error = None, None
    # Public servers refuse bursts (429/504); back off, then try the mirror for Overpass.
    urls = [url] + ([url.replace("overpass-api.de", "overpass.private.coffee")] if "overpass-api.de" in url else [])
    for attempt in range(6):
        target = urls[min(attempt // 3, len(urls) - 1)]
        try:
            request = urllib.request.Request(target, data=data, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=90) as response:
                payload = json.loads(response.read().decode("utf-8"))
            break
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            error = exc
            print(f"  fetch retry {attempt + 1}: {exc}", file=sys.stderr)
            time.sleep(5 * (attempt + 1))
    if payload is None:
        raise RuntimeError(f"could not fetch {url.split('?')[0]}: {error}")
    if cache:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(payload), encoding="utf-8")
    time.sleep(1.1)  # Public services: at most one request per second.
    return payload


def fetch_route(origin, destination, cache: Path):
    url = ("https://router.project-osrm.org/route/v1/driving/"
           f"{origin[1]},{origin[0]};{destination[1]},{destination[0]}?overview=full&geometries=geojson&steps=true&annotations=speed")
    data = fetch_json(url, cache=cache)
    if data.get("code") != "Ok":
        raise RuntimeError(f"routing failed: {data.get('code')}")
    return data["routes"][0]


def fetch_roads(bbox, cache: Path):
    box = f"({bbox[0]},{bbox[1]},{bbox[2]},{bbox[3]})"
    query = ("[out:json][timeout:60];(way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|"
             f"living_street|service|track)(_link)?$\"]{box};node[\"traffic_calming\"]{box};);out body geom;")
    return fetch_json("https://overpass-api.de/api/interpreter", data=("data=" + urllib.parse.quote(query)).encode(), cache=cache)


# ---------------------------------------------------------------- geometry
class Path2D:
    """Route polyline in local metres with cumulative distance."""
    def __init__(self, coords):  # [[lon, lat], ...]
        lat0 = coords[0][1]
        self.kx, self.ky, self.lat0, self.lon0 = 111320 * math.cos(math.radians(lat0)), 110540, lat0, coords[0][0]
        pts = [(self.x(c[0]), self.y(c[1])) for c in coords]
        keep = [pts[0]]
        for p in pts[1:]:
            if math.dist(p, keep[-1]) > .5:
                keep.append(p)
        self.p = np.array(keep)
        seg = np.linalg.norm(np.diff(self.p, axis=0), axis=1)
        self.s = np.concatenate([[0], np.cumsum(seg)])
        self.length = float(self.s[-1])

    def x(self, lon): return (lon - self.lon0) * self.kx
    def y(self, lat): return (lat - self.lat0) * self.ky
    def latlon(self, x, y): return self.lat0 + y / self.ky, self.lon0 + x / self.kx

    def at(self, s):
        s = min(max(s, 0), self.length)
        i = min(max(bisect.bisect_right(self.s, s) - 1, 0), len(self.p) - 2)
        f = (s - self.s[i]) / max(1e-9, self.s[i + 1] - self.s[i])
        q = self.p[i] + f * (self.p[i + 1] - self.p[i])
        d = self.p[i + 1] - self.p[i]
        return q, math.degrees(math.atan2(d[0], d[1])) % 360

    def project(self, x, y):
        """Distance along the route of the closest point, and the distance to it."""
        best = (math.inf, 0.0)
        for i in range(len(self.p) - 1):
            a, b = self.p[i], self.p[i + 1]
            d = b - a
            t = float(np.clip(np.dot((x, y) - a, d) / max(1e-9, np.dot(d, d)), 0, 1))
            dist = float(np.linalg.norm(a + t * d - (x, y)))
            if dist < best[0]:
                best = (dist, float(self.s[i] + t * np.linalg.norm(d)))
        return best[1], best[0]

    def heading(self, s): return self.at(s)[1]


def turn_between(a, b): return (b - a + 540) % 360 - 180


def maneuver_code(step) -> int | None:
    m = step["maneuver"]
    kind, modifier = m["type"], m.get("modifier", "")
    if kind in ("depart",):
        return None
    if kind == "arrive":
        return DESTINATION
    if kind in ("roundabout", "rotary", "roundabout turn", "exit roundabout", "exit rotary"):
        return ROUNDABOUT
    if kind == "fork":
        return FORK
    if kind == "merge":
        return MERGE
    if kind in ("on ramp", "off ramp"):
        return RAMP
    if "uturn" in modifier:
        return UTURN
    if kind in ("new name", "continue", "notification"):
        # Google calls a real direction change at a junction a turn; a renamed road is "straight".
        if modifier in ("left", "sharp left"):
            return LEFT
        if modifier in ("right", "sharp right"):
            return RIGHT
        return STRAIGHT
    if "left" in modifier:
        return LEFT
    if "right" in modifier:
        return RIGHT
    return STRAIGHT


def sharp_bends(path: Path2D, exclude, window=35.0, degrees=55.0):
    """Sharp bends of the route itself (same rule as the native scout), away from maneuvers.

    A bend is placed where the road actually starts turning, not where it first comes within
    the 35 m window. Bends within 15 m before or 50 m after a turn are part of that turn."""
    bends, s, step = [], 10.0, 2.0
    while s < path.length - 10:
        h0 = path.heading(max(0, s - 1))
        total = turn_between(h0, path.heading(min(path.length, s + window)))
        if abs(total) >= degrees:
            start = next((p for p in np.arange(s, s + window, 1.0) if abs(turn_between(h0, path.heading(p))) >= 10), s)
            start = max(s, start - 3)
            if all(not (e - 50 <= start <= e + 15) and not (start <= e <= start + window) for e in exclude):
                bends.append({"s": float(start), "right": total > 0, "angle": round(total)})
            s += window + 20
        else:
            s += step
    return bends


def snap_to_corner(path: Path2D, s: float, before=10.0, after=40.0) -> float:
    """Where the route really turns near a routing step's location (the step can sit at a
    junction just before the corner, with no heading change of its own)."""
    best, at = 0.0, s
    for p in np.arange(max(6.0, s - before), min(path.length - 6, s + after), 1.0):
        change = abs(turn_between(path.heading(p - 6), path.heading(p + 6)))
        if change > best + 1e-6:
            best, at = change, float(p)
    return at


# ---------------------------------------------------------------- learner driving
class RoadGraph:
    """OSM road junctions, used only by the checker to decide what a map alone can foresee."""
    def __init__(self, roads):
        self.pos, self.ways, self.node_ways = {}, [], {}
        for el in roads["elements"]:
            cls = ROAD_CLASSES.get(el.get("tags", {}).get("highway", "").removesuffix("_link")) if el["type"] == "way" else None
            geom = el.get("geometry")
            if cls is None or not geom or len(geom) != len(el["nodes"]):
                continue
            index = len(self.ways)
            self.ways.append({"id": el["id"], "cls": cls, "nodes": el["nodes"], "name": el.get("tags", {}).get("name", "")})
            for i, (n, g) in enumerate(zip(el["nodes"], geom)):
                self.pos[n] = (g["lat"], g["lon"])
                self.node_ways.setdefault(n, []).append((index, i))

    def bearing(self, a, b):
        (la1, lo1), (la2, lo2) = self.pos[a], self.pos[b]
        return math.degrees(math.atan2((lo2 - lo1) * math.cos(math.radians(la1)) * 111320, (la2 - la1) * 110540)) % 360

    def foreseeable(self, lat, lon, in_bearing, out_bearing):
        """Could an instructor with only the map (no destination) know to prepare for this turn?

        Yes when the road ends, when our road itself turns away at the junction, when it meets a
        more important road, or at a real crossroads. No when the learner simply leaves a road
        that carries straight on for an equal or minor side road: that is the learner's choice."""
        best, node = 4.0, None
        for n, (la, lo) in self.pos.items():
            if abs(la - lat) > 5e-5 or abs(lo - lon) > 5e-5 or len(self.node_ways[n]) < 1:
                continue
            d = math.hypot((lo - lon) * math.cos(math.radians(lat)) * 111320, (la - lat) * 110540)
            if d < best:
                best, node = d, n
        if node is None:
            return True, "junction not found in the map"
        branches = []
        for w, i in self.node_ways[node]:
            for q in (i - 1, i + 1):
                if 0 <= q < len(self.ways[w]["nodes"]):
                    branches.append((w, self.bearing(node, self.ways[w]["nodes"][q])))
        diff = lambda a, c: abs((a - c + 540) % 360 - 180)
        back = min(branches, key=lambda br: diff(br[1], in_bearing + 180))
        out = min(branches, key=lambda br: diff(br[1], out_bearing))
        in_cls, in_name = self.ways[back[0]]["cls"], self.ways[back[0]]["name"]
        others = [br for br in branches if br is not back]
        straight = [br for br in others if diff(br[1], in_bearing) < 35 and self.ways[br[0]]["cls"] <= max(6, in_cls)]
        if not straight:
            return True, "road ends"
        if out[0] == back[0] or (in_name and self.ways[out[0]]["name"] == in_name):
            return True, "our road turns away here"
        if any(self.ways[br[0]]["cls"] < in_cls for br in others if diff(br[1], in_bearing) >= 35):
            return True, "meets a more important road"
        if len(branches) >= 4 and any(self.ways[br[0]]["cls"] <= in_cls for br in others if diff(br[1], in_bearing) >= 35):
            return True, "crossroads"
        return False, "learner leaves a through road for a side road"


def speed_profile(path: Path2D, route, maneuvers, bends, bumps, cruise_cap=40.0):
    """Target speed (km/h) on a 1 m grid; then gentle acceleration/deceleration limits."""
    n = int(path.length) + 1
    grid = np.arange(n, dtype=float)
    # Base: the router's segment speeds, capped at learner speeds.
    speeds = route["legs"][0]["annotation"]["speed"]
    seg_s = path.s
    base = np.full(n, cruise_cap)
    if len(speeds) == len(seg_s) - 1 or True:
        coords_s = np.linspace(0, path.length, len(speeds) + 1)
        for k, v in enumerate(speeds):
            a, b = int(coords_s[k]), int(coords_s[k + 1]) + 1
            base[a:b] = min(cruise_cap, max(15.0, v * 3.6))
    # Curvature limit: lateral acceleration 1.8 m/s^2 for a learner.
    for i in range(0, n, 2):
        dtheta = abs(math.radians(turn_between(path.heading(max(0, i - 10)), path.heading(min(path.length, i + 10)))))
        if dtheta > .05:
            radius = 20.0 / dtheta
            base[max(0, i - 3):i + 4] = np.minimum(base[max(0, i - 3):i + 4], 3.6 * math.sqrt(1.8 * radius))
    for m in maneuvers:
        target = {LEFT: 12, RIGHT: 12, ROUNDABOUT: 15, FORK: 22, UTURN: 8, MERGE: 25, RAMP: 25, STRAIGHT: None,
                  DESTINATION: 0}.get(m["code"])
        if target is not None:
            i = int(min(n - 1, m["s"]))
            base[max(0, i - 4):i + 5] = np.minimum(base[max(0, i - 4):i + 5], target)
    for b in bumps:
        i = int(b["s"])
        base[max(0, i - 3):i + 4] = np.minimum(base[max(0, i - 3):i + 4], 10)
    base[0], base[-1] = 0, 0
    v = base / 3.6
    for i in range(1, n):  # Gentle acceleration (0.8 m/s^2).
        v[i] = min(v[i], math.sqrt(v[i - 1] ** 2 + 2 * .8 * 1.0))
    for i in range(n - 2, -1, -1):  # Comfortable braking (1.3 m/s^2).
        v[i] = min(v[i], math.sqrt(v[i + 1] ** 2 + 2 * 1.3 * 1.0))
    return grid, v * 3.6


def time_course(grid, kmh, dt=.1):
    """Distance travelled at each 0.1 s step."""
    out, s, t = [], 0.0, 0.0
    while s < grid[-1] - .5 and t < 7200:
        v = max(np.interp(s, grid, kmh) / 3.6, .4)  # Never stall at a node with zero target.
        out.append((t, s, v * 3.6))
        s += v * dt
        t += dt
    out.append((t, grid[-1], 0.0))
    return out


# ---------------------------------------------------------------- simulation
def run(args):
    out = ROOT / "build/route-sims" / args.name
    out.mkdir(parents=True, exist_ok=True)
    route = fetch_route(args.origin, args.destination, out / "route.json")
    coords = route["geometry"]["coordinates"]
    path = Path2D(coords)
    steps = [st for leg in route["legs"] for st in leg["steps"]]
    maneuvers = []
    for k, st in enumerate(steps):
        code = maneuver_code(st)
        if code is None:
            continue
        lon, lat = st["maneuver"]["location"]
        s, _ = path.project(path.x(lon), path.y(lat))
        # A turn whose step shows no heading change of its own is announced at the real corner.
        if code in (LEFT, RIGHT, UTURN) and abs(turn_between(st["maneuver"].get("bearing_before", 0),
                                                            st["maneuver"].get("bearing_after", 0))) < 20:
            s = snap_to_corner(path, s)
        # A "straight" step through a real corner (45 degrees or more) is a turn Google would announce.
        if code == STRAIGHT and st["maneuver"]["type"] in ("turn", "continue", "new name"):
            corner = snap_to_corner(path, s, before=10.0, after=25.0)
            change = turn_between(path.heading(corner - 6), path.heading(corner + 6))
            if abs(change) >= 45:
                code, s = (RIGHT if change > 0 else LEFT), corner
        maneuvers.append({"step": k, "code": code, "s": s, "lat": lat, "lon": lon, "type": st["maneuver"]["type"],
                          "modifier": st["maneuver"].get("modifier", ""), "name": st.get("name", "")})
    maneuvers.sort(key=lambda m: m["s"])
    # Google gives a roundabout as one step (entry, with the exit number); OSRM adds a separate
    # "exit" step. Exits within 150 m of the entry are folded into it; a long ring road keeps them.
    folded = []
    for m in maneuvers:
        if (m["type"] in ("exit roundabout", "exit rotary") and folded and folded[-1]["code"] == ROUNDABOUT
                and m["s"] - folded[-1]["s"] <= 150):
            folded[-1]["exit_s"] = m["s"]
            continue
        folded.append(m)
    maneuvers = folded
    lats, lons = [c[1] for c in coords], [c[0] for c in coords]
    pad = .004
    roads = fetch_roads((min(lats) - pad, min(lons) - pad, max(lats) + pad, max(lons) + pad), out / "roads.json")
    bumps = []
    for el in roads["elements"]:
        if el["type"] == "node" and el.get("tags", {}).get("traffic_calming", "no") != "no":
            s, d = path.project(path.x(el["lon"]), path.y(el["lat"]))
            if d < 8:
                bumps.append({"s": s, "node": el["id"], "kind": el["tags"]["traffic_calming"]})
    bumps.sort(key=lambda b: b["s"])
    # A roundabout's inside (entry to exit) is part of the roundabout, not a bend.
    bends = sharp_bends(path, [m["s"] for m in maneuvers if m["code"] != STRAIGHT] +
                        [e for m in maneuvers if m.get("exit_s") for e in np.arange(m["s"], m["exit_s"], 10.0)])
    grid, kmh = speed_profile(path, route, maneuvers, bends, bumps)
    course = time_course(grid, kmh)

    # Native brain, fed like the phone.
    native = rv.Native(args.library)
    native._bind({"navigation_at": ([P, C.c_double, C.c_uint64, C.c_int, C.c_int, C.c_double, C.c_double, C.c_double,
                                     C.c_double, C.c_double, C.c_double], C.c_int),
                  "road_way": ([P, C.c_int64, C.c_int, P, P, C.c_size_t], C.c_int), "road_bump": ([P, C.c_int64], None),
                  "road_ahead": ([P, C.c_double, P], C.c_int)})
    width, height = 320, 240
    handle = native.create(b"", width, height, None)
    native.bind_navigation()
    if not native.live_navigation(handle):
        raise RuntimeError("live source rejected")
    native.configure_coaching(handle, args.profile)
    native.bind_automatic()
    native.coaching_automatic(handle, 1, 1)
    loaded = 0
    for el in roads["elements"]:
        if el["type"] != "way":
            continue
        cls = ROAD_CLASSES.get(el.get("tags", {}).get("highway", "").removesuffix("_link"))
        geom = el.get("geometry")
        if cls is None or not geom or len(geom) != len(el["nodes"]) or len(geom) < 2:
            continue
        ids = np.array(el["nodes"], np.int64)
        ll = np.array([[g["lat"], g["lon"]] for g in geom], np.float64).ravel()
        loaded += native.road_way(handle, el["id"], cls, ids.ctypes.data_as(P), ll.ctypes.data_as(P), len(ids))
    for el in roads["elements"]:
        if el["type"] == "node" and el.get("tags", {}).get("traffic_calming", "no") != "no":
            native.road_bump(handle, el["id"])

    durations = {}
    for prompt in range(1, native.prompt_count() + 1):
        with wave.open(str(ASSETS / f"audio/prompt-{prompt}.wav"), "rb") as clip:
            durations[prompt] = clip.getnframes() / clip.getframerate()
    fragment_durations = {}
    for fragment in range(1, native.fragment_count() + 1):
        with wave.open(str(ASSETS / f"audio/fragment-{fragment}.wav"), "rb") as clip:
            fragment_durations[fragment] = clip.getnframes() / clip.getframerate()

    def duration(prompt):
        if prompt in durations:
            return durations[prompt]
        parts = np.zeros(8, np.int32)
        k = native.prompt_fragments(int(prompt), parts.ctypes.data_as(P), 8)
        return sum(fragment_durations[int(f)] for f in parts[:k]) + .12 * max(0, k - 1) if k > 0 else 3.0

    frame = np.full(width * height * 3 // 2, 128, np.uint8)
    frame[:width * height] = 90
    tensor = np.empty((1, 3, 320, 320), np.float32)
    empty = np.zeros((84, 1), np.float32)
    decision, trace = np.empty(8, np.float64), np.empty(22, np.float64)
    ahead_buf = np.empty(9, np.float64)
    spoken, interruptions, scout = [], [], []
    playing, playing_until, sequence = 0, 0.0, 0
    nav_step_index = 0
    times = [c[0] for c in course]

    def state_at(t):
        i = min(bisect.bisect_left(times, t), len(course) - 1)
        return course[i]

    t = 0.0
    end = course[-1][0] + 5
    dt = .05
    k = 0
    while t <= end:
        _, s, speed = state_at(t)
        (px, py), bearing = path.at(s)
        lat, lon = path.latlon(px, py)
        if k % 2 == 0:  # Camera frames at 10 Hz (empty road).
            native.prepare(handle, rv.ptr(frame), rv.ptr(tensor))
            native.labels(handle, t, rv.ptr(empty), empty.size, empty.shape[1])
        if k % 10 == 0:  # Navigation feed and GPS at 2 Hz, like the provider callbacks.
            sequence += 1
            upcoming = [m for m in maneuvers if m["s"] > s + 1]
            if args.no_destination or not upcoming:
                native.navigation_at(handle, t, sequence, -1, 0, math.inf, math.inf, speed, lat, lon, bearing)
            else:
                m = upcoming[0]
                arrive_t = next((c[0] for c in course if c[1] >= m["s"]), course[-1][0])
                native.navigation_at(handle, t, sequence, m["step"], m["code"], m["s"] - s, max(0.0, arrive_t - t),
                                     speed, lat, lon, bearing)
            native.road_ahead(handle, t, rv.ptr(ahead_buf))
            scout.append([round(t, 1), round(s, 1), round(speed, 1), *[round(float(v), 1) for v in ahead_buf]])
        if playing and t < playing_until and native.coaching_continue_reason(handle, playing, t).decode() != "OK":
            reason = native.coaching_continue_reason(handle, playing, t).decode()
            native.coaching_continue(handle, playing, t)
            interruptions.append({"time": round(t, 2), "prompt": playing, "reason": reason})
            playing, playing_until = 0, t
        if t >= playing_until:
            if playing:
                native.coaching_completed(handle, playing, t)
            playing = 0
        native.tick(handle, t, speed, 1, playing, rv.ptr(decision))
        selected = int(decision[0])
        if selected and decision[6] and decision[7]:
            if native.coaching_accepted(handle, selected, t) and native.coaching_onset(handle, selected, t):
                native.road_ahead(handle, t, rv.ptr(ahead_buf))
                nxt = next((m for m in maneuvers if m["s"] > s + 1), None)
                spoken.append({"time": round(t, 2), "s": round(s, 1), "speed_kmh": round(speed, 1), "prompt": selected,
                               "text": native.prompt(selected).decode(), "duration": round(duration(selected), 2),
                               "lat": round(lat, 6), "lon": round(lon, 6),
                               "next_maneuver_m": round(nxt["s"] - s, 1) if nxt else None,
                               "next_maneuver": nxt["code"] if nxt else None})
                playing, playing_until = selected, t + duration(selected)
        t += dt
        k += 1
    native.destroy(handle)

    # ---------------------------------------------------------------- evaluation
    def when(s_target):
        return next((c[0] for c in course if c[1] >= s_target), course[-1][0])

    def first(cond, lo, hi):
        return next((e for e in spoken if lo <= e["time"] <= hi and cond(e["text"])), None)

    def latest(cond, lo, hi):
        return next((e for e in reversed(spoken) if lo <= e["time"] <= hi and cond(e["text"])), None)

    graph = RoadGraph(roads) if args.no_destination else None
    findings = []
    prev_t, prev_turn_t = 0.0, -1e9
    checked = [m for m in maneuvers if m["code"] in (LEFT, RIGHT, ROUNDABOUT, UTURN, FORK)]
    for m in checked:
        t_m = when(m["s"])
        # Back-to-back turns (under 10 s apart) leave no room for a separate preparation;
        # the learner still needs the entry cue for the second one, given after the first.
        chained = t_m - prev_turn_t < 10
        if chained:
            prev_t = prev_turn_t - 1
        side = "left" if m["code"] == LEFT else "right" if m["code"] == RIGHT else None
        if args.no_destination:
            side = None  # Without a route the brain cannot know which way the learner will go.
        # The latest preparation at least 3 s before the turn (anything later is for the next
        # junction, or too late to count), and the latest entry cue before the turn itself.
        prep = latest(lambda x: ("turn ahead" in x.lower() or "junction ahead" in x.lower()) and
                      ("signal" in x.lower() or "mirrors" in x.lower() or "ease off" in x.lower()), prev_t, t_m - 3)
        entry = latest(lambda x: "look both ways" in x.lower(), prev_t, t_m + .2)
        right_side = prep is None or side is None or side in prep["text"].lower()
        row = {"kind": {LEFT: "left turn", RIGHT: "right turn", ROUNDABOUT: "roundabout", UTURN: "U-turn", FORK: "fork"}[m["code"]],
               "at_s": round(t_m, 1), "street": m["name"], "osrm": f"{m['type']} {m['modifier']}".strip(),
               "prepare_lead_s": round(t_m - prep["time"], 1) if prep else None,
               "prepare_lead_m": round(m["s"] - prep["s"], 0) if prep else None,
               "entry_lead_s": round(t_m - entry["time"], 1) if entry else None,
               "speed_at_prepare": prep["speed_kmh"] if prep else None, "direction_ok": right_side, "chained": chained}
        # A gentle fork needs looking and positioning, not "clutch down, look both ways".
        needs_entry = m["code"] != FORK
        # Entry by the clock, or by distance at the speed when it was said (the learner slows).
        row["entry_lead_at_speed_s"] = (round((m["s"] - entry["s"]) / max(entry["speed_kmh"] / 3.6, 1), 1)
                                        if entry else None)
        entry_ok = bool(entry and (0 <= row["entry_lead_s"] <= 7 or 0 <= row["entry_lead_at_speed_s"] <= 7))
        if chained:
            row["ok"] = entry_ok
        else:
            # Lead as the brain saw it when speaking: distance at the measured speed.
            row["prepare_lead_at_speed_s"] = (round(row["prepare_lead_m"] / max(prep["speed_kmh"] / 3.6, 1), 1)
                                              if prep else None)
            lead_ok = prep and (5 <= row["prepare_lead_s"] <= 26 or 5 <= row["prepare_lead_at_speed_s"] <= 26)
            row["ok"] = bool(lead_ok and right_side and
                             (not needs_entry or (entry_ok and prep["time"] < entry["time"])))
        if graph:
            # The routing step's own location is the junction node (a turn may be placed at the corner).
            row["foreseeable"], row["why"] = graph.foreseeable(m["lat"], m["lon"], path.heading(max(0.0, m["s"] - 12)),
                                                               path.heading(min(path.length, m["s"] + 12)))
            if not row["foreseeable"]:
                row["ok"] = None  # Not scored: without a destination only the learner knows.
        findings.append(row)
        prev_t, prev_turn_t = t_m + .5, t_m  # Cues after a turn belong to the next one.
    bump_rows = []
    for b in bumps:
        t_b = when(b["s"])
        w = latest(lambda x: "bump" in x.lower(), t_b - 14, t_b)
        bump_rows.append({"at_s": round(t_b, 1), "lead_s": round(t_b - w["time"], 1) if w else None,
                          "ok": bool(w and 2 <= t_b - w["time"] <= 12)})
    bend_rows = []
    for b in bends:
        t_b = when(b["s"])
        w = latest(lambda x: "bend" in x.lower(), t_b - 14, t_b + 1)
        bend_rows.append({"at_s": round(t_b, 1), "angle": b["angle"], "lead_s": round(t_b - w["time"], 1) if w else None,
                          "horn": bool(w and "horn" in w["text"].lower()), "ok": bool(w and 1 <= t_b - w["time"] <= 12)})
    gaps = [b["time"] - a["time"] for a, b in zip(spoken, spoken[1:])]
    summary = {
        "name": args.name, "mode": "no destination (map scout only)" if args.no_destination else "navigating (Google-style feed)",
        "route_km": round(path.length / 1000, 2), "drive_minutes": round(course[-1][0] / 60, 1),
        "roads_loaded": loaded, "instructions": len(spoken), "per_minute": round(len(spoken) / max(1e-9, course[-1][0] / 60), 1),
        "interruptions": len(interruptions), "longest_silence_s": round(max(gaps), 1) if gaps else None,
        "turns_checked": sum(f["ok"] is not None for f in findings), "turns_ok": sum(bool(f["ok"]) for f in findings),
        "turns_learner_choice": sum(f["ok"] is None for f in findings),
        "bumps_on_route": len(bump_rows), "bumps_ok": sum(b["ok"] for b in bump_rows),
        "sharp_bends": len(bend_rows), "bends_ok": sum(b["ok"] for b in bend_rows),
        "meaning": "Simulated learner on a real OpenStreetMap route; tests timing and order of map-based coaching only",
    }
    result = {"summary": summary, "turns": findings, "bumps": bump_rows, "bends": bend_rows, "spoken": spoken,
              "interruptions": interruptions,
              "scout": {"fields": ["t", "s", "kmh", "valid", "dist", "node", "road_ends", "matched", "bump_m", "bend_m",
                                   "bend_right", "way_count"], "rows": scout},
              "track": [[round(c[0], 1), *[round(v, 6) for v in path.latlon(*path.at(c[1])[0])], round(c[2], 1)]
                        for c in course[::10]],
              "route": {"polyline": [[round(c[1], 6), round(c[0], 6)] for c in coords],
                        "maneuvers": [{**m, "t": round(when(m["s"]), 1)} for m in maneuvers],
                        "bumps": bumps, "bends": [{**b, "t": round(when(b["s"]), 1)} for b in bends]}}
    suffix = "-nodest" if args.no_destination else ""
    (out / f"result{suffix}.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name")
    parser.add_argument("--origin", type=float, nargs=2, required=True, metavar=("LAT", "LON"))
    parser.add_argument("--destination", type=float, nargs=2, required=True, metavar=("LAT", "LON"))
    parser.add_argument("--profile", default="guided", choices=["minimal", "balanced", "guided"])
    parser.add_argument("--no-destination", action="store_true", help="no navigation feed: the map scout alone")
    parser.add_argument("--library", type=Path, default=ROOT / "build/native-spatial/liblearndrive_replay.dll")
    args = parser.parse_args(argv)
    result = run(args)
    print(json.dumps(result["summary"], indent=1))
    for f in result["turns"]:
        mark = "-- " if f["ok"] is None else "OK " if f["ok"] else "BAD"
        note = f"  [{f['why']}]" if f.get("why") else "  [back-to-back]" if f.get("chained") else ""
        print(f"  {mark} {f['kind']:10s} at {f['at_s']:6.1f}s  prepare {f['prepare_lead_s']}s/{f['prepare_lead_m']}m"
              f"  entry {f['entry_lead_s']}s  ({f['osrm']}, {f['street']}){note}")


if __name__ == "__main__":
    main()
