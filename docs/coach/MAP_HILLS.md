# 0.8.2: hills from the map

Slope advice used to come from the phone's tilt. A mount that sags or falls reads as a hill, and
even a steady mount only tells the coach about the hill the car is already on. With a Google
route, the coach now reads the road's height along the route ahead (Google Maps Elevation API)
and names a climb or descent, and the gear for it, before the car gets there.

## Which Google resource

Only the **Elevation API** gives heights. The Navigation SDK (already used for the route line and
turns), the Routes and Directions APIs and the Roads API do not return elevation. The app samples
Google's own route line and asks the Elevation API for the height at each sample.

## How it works

**Phone** (`RouteElevation.kt`): Google's route line is sampled every 20 m and sent a stretch at a
time, up to 500 points per request (about 10 km). The next stretch is asked for when the car is
within 4 km of the end of what is known, up to 100 km per route. The heights stay in memory with
the route and are dropped with it, as Google's terms ask. The same route coming back after a short
navigation glitch reuses them instead of asking again. The key and the request URL are never
logged or shown. The **Hills ahead** row in setup shows what is happening.

**Core** (`RouteLine::elevation`):

1. Heights are resampled every 10 m and averaged over ±40 m. The grade is the rise over 100 m.
2. A climb or descent starts at 5 % and lasts while the grade stays at 3.5 % or more the same way.
   Stretches up to 40 m apart are merged.
3. It counts when it averages at least 6 % over at least 100 m, and the road stays at least 5 m
   higher or lower beyond it. Trees and buildings in the terrain model rise and fall back, so they
   fail this.
4. A short descent followed by a climb of about the same height (or the reverse) within 300 m is
   left out. Terrain models give the ground, not the road deck, so a bridge over a river reads as
   a dip and a tunnel or cutting as a hump.
5. The grade where the car is now replaces the phone's tilt everywhere it is used. That includes
   "Downhill." before a road-user instruction and stopping distance on a slope. Without the
   route's elevation (no Google route, a request failed, or past the end of what is known), the
   phone's tilt works as before.

**Coach** (`ControlCoach`, `SituationPlanner`): the hill is named about 7 s before it starts, or
9 s when the learner must slow down first, and never less than 40 m ahead. It is named once. If it
is first seen with the car already on it, it is said once ("Steep downhill.") when at least 60 m
remain. The next stretch the same way within 45 s, such as a ghat road between hairpins, is the
same hill: it is named again only if it is steeper. The words depend on the speed.

The gear advice uses the coach's own speed bands: second above 10 km/h, third above 22, fourth
above 37, fifth above 52. These are instructor rules of thumb, never the selected gear. A hill's
gear is second when it is steep (10 % or more), otherwise third.

| Hill | Speed | Said |
|---|---|---|
| Steep descent | up to 22 km/h | Steep downhill ahead. Do not coast in neutral. Control your speed with the brake. |
| | 22-37 km/h | Steep downhill ahead. Change down to second. Control your speed with the brake. |
| | above 37 km/h | Steep downhill ahead. Brake gently. Change down to second. |
| Descent | up to 37 km/h | Downhill ahead. Do not coast in neutral. Control your speed with the brake. |
| | 37-52 km/h | Downhill ahead. Change down to third. Control your speed with the brake. |
| | above 52 km/h | Downhill ahead. Brake gently. Change down to third. |
| Steep climb | up to 22 km/h | Steep uphill ahead. Steady accelerator. |
| | 22-37 km/h | Steep uphill ahead. Change down to second. Steady accelerator. |
| | above 37 km/h | Steep uphill ahead. Change down a gear. Steady accelerator. |
| Climb | up to 37 km/h | Uphill ahead. Steady accelerator. |
| | 37-52 km/h | Uphill ahead. Change down to third. Steady accelerator. |
| | above 52 km/h | Uphill ahead. Change down a gear. Steady accelerator. |

Going down, a learner two gears above the hill's gear slows down before changing down. Going up,
a fast learner changes down one gear at a time.

## Testing

- **Synthetic profiles** (`tests/road_knowledge_tests.cpp`): each slope is found where it starts,
  with its grade and length, for an 8 % descent and a 12 % climb with model noise. A 4 % rise, a
  bridge over a river, and a clump of trees in the terrain model are not hills. A real descent and
  a real hill's climb and descent are. Slope ids are stable when the next stretch arrives.
  Samples off the route are ignored, and a new route forgets the old profile. The C API passes the
  grade and the next hill.
- **Coaching** (`tests/drive_coach_tests.cpp`):
  - A descent is named once, 4-7.5 s ahead, while a sagging mount reads uphill.
  - The sentence for seven grade and speed cases matches the table above.
  - A slope first seen underfoot is said once if enough of it is left.
  - A flat map silences the tilt.
  - A ghat road's climbs make one cue, plus one when the climb gets steeper, plus one for the
    descent after.
- **The app's request, live** (9 November, from a PC, as the phone was not connected):
  - A script sent what `RouteElevation.kt` sends: the same 20 m points, Google's polyline
    encoding, and the app's package name and signing-certificate SHA-1, read from the debug
    APK. The development key answered `OK`. The same request without those two headers was
    refused (`REQUEST_DENIED`), so the key's Android restriction works for the Elevation API.
  - The app's own sampling and encoding code passes a JVM unit test
    (`android/app/src/test/.../RouteElevationTest.kt`): Google's documented polyline example,
    20 m steps along the road around a corner, and the 100 km limit.
- **Real roads**: the detector was tuned on SRTM 30 m heights (OpenTopoData) along OSRM routes.
  Google's own heights along the same routes were then piped straight into it, never stored. In
  Kerala they come from a 38 m grid and sit a near-constant 19-20 m above SRTM, a different
  vertical reference, so grades are the same. Hills said at 40 km/h (25 km/h on the ghat):

| Route | SRTM, before tuning | SRTM | Google | Notes |
|---|---|---|---|---|
| Aluva to Angamaly, NH 544 (13.5 km, flat) | 9 | 0 | 0 | Before tuning, the Periyar crossing read as a 12 m dip and a 14 m climb |
| Thrissur city loop (6.7 km) | 2 | 1 | 1 | |
| Thrissur to Peechi (21.2 km) | 12 | 5 | 4 | Mostly the real hills near Peechi |
| Thamarassery to Lakkidi (26 km, ghat) | 58 named | 12 said | 10 said | The ghat is one steep climb, named at its start and again near the top; the rest are on the rolling road before it |

- **No regressions**: every native suite fails only the groups that failed before this change (18
  older groups). The Android app builds (0.8.2, versionCode 43).

## Setting it up

Done for the development key on 9 November: the Maps Elevation API is enabled in
`learndrive-development-513812` and allowed for the key. See
[Google navigation setup](../GOOGLE_NAVIGATION_SETUP.md#6-hills-along-the-route-elevation-api).

## Limits

- **Not yet tried on the phone or on the road.** The setup row, the cues and the timing have been
  checked on the PC only. The new phrases use the recorded fallback voice until the phone's
  natural voice renders them on first launch.
- **Ground, not deck.** Flyover ramps are not seen. The bridge and tree filters also drop some
  short real hills, such as a road dipping to a stream and climbing straight out within about
  300 m.
- **Hairpins.** On a ghat road the terrain model mixes the hillside's heights. The grade reads 10-25
  % where the road is probably 6-12 %, so a climb is called steep, but it is named once.
- **Gears are rules of thumb from speed.** The selected gear is not sensed, so "Change down to
  third" may already be done.
- **Without a Google route** (free practice or the offline provider), hills still come from the
  phone's tilt.
