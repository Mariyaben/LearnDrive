"""One focus per drive (0.9.9) on real Kerala routes.

Drives the cached route simulations (build/route-sims/<name>, made by tools/simulate_route_drive.py:
OpenStreetMap routes and the simulator's learner) through the native core as the phone feeds it:
the route line, GPS fixes at 2 Hz and Google-like turn updates. Records what a drive's focus needs:
the route's features (ld_route_features) and the car's place along the route and speed, and what the
simulated learner really did at each bend and turn. Writes android/app/build/focus-fixtures/<name>.txt
for DriveFocusRouteReplayTest (Gradle testDebugUnitTest) and prints what the brief would count.

Needs build/native-spatial/liblearndrive_replay.dll (cmake --build build/native-spatial --target
learndrive_replay) and a Python with onnxruntime (review_video imports it), e.g. the conda one:
    python tools/focus_route_probe.py [route names]

Fixture lines:  F <values...>   ld_route_features (full) whenever its lists change
                S <t> <m along the route or -1> <speed km/h>   every 0.1 s
                T <t> <step> <maneuver code> <m to it>   each navigation update (2 Hz)
                B <bend start m> <speed there> <slowest in its first 35 m>
                U <step> <speed 15 m before the turn> <slowest from there to the step's end>
"""
from __future__ import annotations

import ctypes as C
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import review_video as rv  # noqa: E402
import simulate_route_drive as sim  # noqa: E402

P = C.c_void_p
OUT = ROOT / "android/app/build/focus-fixtures"
OUT.mkdir(parents=True, exist_ok=True)


def route_data(name):
    folder = ROOT / "build/route-sims" / name
    import json
    route = json.loads((folder / "route.json").read_text(encoding="utf-8"))
    route = route["routes"][0] if "routes" in route else route
    coords = route["geometry"]["coordinates"]
    path = sim.Path2D(coords)
    steps = [st for leg in route["legs"] for st in leg["steps"]]
    maneuvers = []
    for k, st in enumerate(steps):
        code = sim.maneuver_code(st)
        if code is None:
            continue
        lon, lat = st["maneuver"]["location"]
        s, _ = path.project(path.x(lon), path.y(lat))
        if code in (sim.LEFT, sim.RIGHT, sim.UTURN) and abs(sim.turn_between(st["maneuver"].get("bearing_before", 0),
                                                                              st["maneuver"].get("bearing_after", 0))) < 20:
            s = sim.snap_to_corner(path, s)
        if code == sim.STRAIGHT and st["maneuver"]["type"] in ("turn", "continue", "new name"):
            corner = sim.snap_to_corner(path, s, before=10.0, after=25.0)
            change = sim.turn_between(path.heading(corner - 6), path.heading(corner + 6))
            if abs(change) >= 45:
                code, s = (sim.RIGHT if change > 0 else sim.LEFT), corner
        maneuvers.append({"step": k, "code": code, "s": s, "type": st["maneuver"]["type"],
                          "modifier": st["maneuver"].get("modifier", "")})
    maneuvers.sort(key=lambda m: m["s"])
    folded = []
    for m in maneuvers:
        if (m["type"] in ("exit roundabout", "exit rotary") and folded and folded[-1]["code"] == sim.ROUNDABOUT
                and m["s"] - folded[-1]["s"] <= 150):
            folded[-1]["exit_s"] = m["s"]
            continue
        folded.append(m)
    maneuvers = folded
    bends = sim.sharp_bends(path, [m["s"] for m in maneuvers if m["code"] != sim.STRAIGHT] +
                            [e for m in maneuvers if m.get("exit_s") for e in np.arange(m["s"], m["exit_s"], 10.0)])
    grid, kmh = sim.speed_profile(path, route, maneuvers, bends, [])
    course = sim.time_course(grid, kmh)
    return coords, path, maneuvers, bends, course


def main(names):
    native = rv.Native(ROOT / "build/native-spatial/liblearndrive_replay.dll")
    native._bind({"navigation_at": ([P, C.c_double, C.c_uint64, C.c_int, C.c_int, C.c_double, C.c_double, C.c_double,
                                     C.c_double, C.c_double, C.c_double], C.c_int),
                  "route_line": ([P, P, C.c_size_t], C.c_int),
                  "route_maneuvers": ([P, C.c_double, P, C.c_size_t], None),
                  "route_features": ([P, C.c_double, P, C.c_size_t], C.c_int)})
    for name in names:
        coords, path, maneuvers, bends, course = route_data(name)
        handle = native.create(b"", 320, 240, None)
        native.bind_navigation()
        if not native.live_navigation(handle):
            raise RuntimeError("live source rejected")
        line = np.array([[c[1], c[0]] for c in coords], np.float64).ravel()
        if not native.route_line(handle, line.ctypes.data_as(P), len(coords)):
            raise RuntimeError(f"{name}: route line rejected")
        buf = np.zeros(13 + 4 * 80 + 5 * 600 + 24, np.float64)
        lines, last_full, first = [], None, None
        times = [c[0] for c in course]
        t, dt, k, seq = 0.0, .05, 0, 0
        end = course[-1][0]
        import bisect
        while t <= end:
            i = min(bisect.bisect_left(times, t), len(course) - 1)
            _, s, speed = course[i]
            (px, py), bearing = path.at(s)
            lat, lon = path.latlon(px, py)
            if k % 10 == 0:
                seq += 1
                upcoming = [m for m in maneuvers if m["s"] > s + 1]
                if upcoming:
                    m = upcoming[0]
                    native.navigation_at(handle, t, seq, m["step"], m["code"], m["s"] - s, 10.0, speed, lat, lon, bearing)
                    ahead = np.array([u["s"] - s for u in upcoming], np.float64)[:24]
                    native.route_maneuvers(handle, t, ahead.ctypes.data_as(P), len(ahead))
                    lines.append(f"T {t:.2f} {m['step']} {m['code']} {m['s'] - s:.1f}")
                else:
                    native.navigation_at(handle, t, seq, -1, 0, math.inf, math.inf, speed, lat, lon, bearing)
            if k % 20 == 0:
                n = native.route_features(handle, t, buf.ctypes.data_as(P), len(buf))
                full = " ".join(f"{v:.2f}" for v in buf[:n])
                # Only the lists matter for a change: positions are re-read each second anyway.
                key = " ".join(f"{v:.1f}" for v in buf[13:n])
                if buf[8] and key != last_full:
                    lines.append(f"F {full}");last_full = key
                    if first is None:
                        first = buf[:n].copy()
            if k % 2 == 0:
                h = np.zeros(13);native.route_features(handle, t, h.ctypes.data_as(P), 13)
                lines.append(f"S {t:.2f} {h[4]:.2f} {speed:.2f}")
            t += dt;k += 1
        native.destroy(handle)
        # What the simulated learner really did: each route-line bend's speed at its start and slowest
        # in its first 35 m; each turn's speed 15 m before it and slowest from there to the step change.
        def speed_at(target):
            return next((v for (_, s, v) in course if s >= target), course[-1][2])

        def slowest(a, b):
            vs = [v for (_, s, v) in course if a <= s <= b]
            return min(vs) if vs else float("nan")
        if last_full is not None:
            final = np.array([float(x) for x in lines[[i for i, l in enumerate(lines) if l.startswith("F ")][-1]].split()[1:]])
            sc, bc = int(final[10]), int(final[11])
            for x in final[13 + 4 * sc:13 + 4 * sc + 5 * bc].reshape(-1, 5):
                lines.append(f"B {x[0]:.2f} {speed_at(x[0]):.2f} {slowest(x[0], x[0] + 35):.2f}")
        for m in maneuvers:
            if m["code"] in (sim.LEFT, sim.RIGHT, sim.UTURN, sim.ROUNDABOUT):
                lines.append(f"U {m['step']} {speed_at(m['s'] - 15):.2f} {slowest(m['s'] - 15, m['s'] - 1):.2f}")
        (OUT / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
        if first is None:
            print(f"{name}: route features never had turns known");continue
        sc, bc, tc = int(first[10]), int(first[11]), int(first[12])
        b = first[13 + 4 * sc:13 + 4 * sc + 5 * bc].reshape(-1, 5)
        counted = [x for x in b if not x[4]]
        turns = [m for m in maneuvers if m["code"] in (sim.LEFT, sim.RIGHT, sim.UTURN, sim.ROUNDABOUT)]
        print(f"{name}: {path.length / 1000:.1f} km, {len(course) * .1 / 60:.1f} min; route line bends {bc} "
              f"({int(b[:, 2].sum()) if bc else 0} sharp), at a turn {bc - len(counted)}, counted {len(counted)}; "
              f"turns {len(turns)} of {len(maneuvers)} steps; sim's own sharp bends {len(bends)}")
        for x in counted:
            print(f"   bend at {x[0]:7.1f} m {x[1]:+6.1f} deg {'sharp' if x[2] else '     '} comfortable {x[3]:4.1f} km/h")


if __name__ == "__main__":
    main(sys.argv[1:] or ["thrissur-round-ayyanthole", "kochi-kaloor-panampilly", "kozhikode-mananchira-beach",
                          "guruvayur-temple-chavakkad"])
