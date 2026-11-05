# Route-drive simulation

`tools/simulate_route_drive.py` drives the native coaching brain along a real road it has
never seen, the way the phone would feed it. Then it checks what the brain said and when.
It tests the map-based part of coaching on new roads. It is not a test of camera coaching,
and passing it does not show that the teaching is good. Only drives with a real instructor can show that.

## What it does

1. **Route.** It gets a car route between two points from OSRM (`router.project-osrm.org`), with
   OSRM's turn-by-turn steps. A small roundabout's separate "exit" step is folded into the
   roundabout, because Google gives one step ("take the 2nd exit").
2. **Map.** It downloads the OpenStreetMap roads and `traffic_calming` nodes (speed bumps) around the
   route from Overpass. It retries, then falls back to a mirror. Both responses are cached in
   `build/route-sims/<name>/`.
3. **Learner.** A simulated learner drives the route:
   - at most 40 km/h, with a lateral-acceleration limit on bends;
   - 12 km/h through turns and 10 km/h over bumps;
   - accelerating at 0.8 m/s² and braking at 1.3 m/s².
4. **Phone feed.** The brain is fed as on the phone:
   - Google-style navigation updates at 2 Hz (current step, maneuver, metres and seconds left);
   - GPS position, speed and bearing;
   - all the OSM roads and bumps (the same `ld_road_way` / `ld_road_bump` calls the app makes);
   - empty camera frames at 10 Hz;
   - the 20 Hz decision loop.

   Speech is "played" for the real length of each voice clip, and an instruction that becomes
   invalid mid-sentence is cut off as on the phone.
5. **Checks.** It scores every turn, sharp bend and mapped bump on the route:
   - **Turn, with a destination.** "Left/right turn ahead … mirrors and signal" (or "Junction
     ahead" for forks and roundabouts) must come 5–26 s before the turn, either by the clock or
     by distance at the speed when it was said. "Clutch down. Look both ways" must follow 0–7 s
     before the turn; forks don't need it. When two turns are under 10 s apart, the second only
     needs its entry cue.
   - **Turn, no destination (`--no-destination`).** Only junctions that a map alone can foresee
     are scored: the road ends, our road bends away at a junction, a more important road, or a
     crossroads. Turning off a through road into a side road is the learner's choice. It is listed but not scored.
   - **Sharp bend** (≥ 55° within 35 m, away from turns): "Sharp bend ahead. Sound the horn and keep left", 1–12 s before.
   - **Speed bump** mapped in OSM: "Speed bump ahead…", 2–12 s before.

## Running it

```bash
python tools/simulate_route_drive.py kochi-kaloor-panampilly --origin 9.9966 76.2995 --destination 9.9600 76.2938
```

```bash
python tools/simulate_route_drive.py kochi-kaloor-panampilly --origin 9.9966 76.2995 --destination 9.9600 76.2938 --no-destination
```

```bash
python tools/route_sim_viewer.py
```

The viewer writes `build/route-sims/route-coach-playback.html`. It plays each drive back on the
route map, shows each instruction where it was said and speaks it aloud through the browser.

`result.json` (or `result-nodest.json`) holds:
- the summary, plus every turn, bend and bump row;
- every spoken instruction, with time, place, speed and distance to the next maneuver;
- a 1 s track for playback;
- a 2 Hz `scout` trace of what the map scout believed was ahead, for debugging.

## Limits

- The learner is simulated: smooth speed, perfect lane position, no traffic, no stops except at the destination.
- No other road users, so the camera-side reasoning (people, bikes, buses) is not exercised. That is covered by the drive-video replays.
- Google's own announcements, rerouting and timing on the phone are not reproduced. The feed is built from OSRM steps.
- OpenStreetMap in Kerala rarely tags speed bumps, so bump coaching is mostly untested here. It needs bumps learned from the phone's motion sensor or seen by the camera.
- OSRM data and OSM data are © OpenStreetMap contributors (ODbL). Use the public servers lightly; responses are cached.
