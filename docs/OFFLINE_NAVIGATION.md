# Offline navigation and map preparation — Kerala first

Implementation started 19 October 2026 at the user's request, before resuming
the learner-coaching work identified in the screen-recording review. This is an
experimental Android integration, not certification or a road-release approval.

## Implementation choices

The phone computes routes locally with Valhalla Mobile 0.6.4, whose embedded C++
engine is Valhalla 3.9.1. Android calls its published mobile binding on a dedicated
worker, rather than maintaining a second private NDK build of the engine. Route
geometry and map/GPS coaching policy live in the existing shared C++17 core.
MapLibre Native Android 13.6.1 displays an app-generated local style and a local
PMTiles archive. The manifest explicitly removes INTERNET permission, including
permissions introduced by dependencies. No Google Maps, remote routing, online
geocoder, remote glyphs or remote style are used.

These are different data products:

- An OSM `.osm.pbf` is source data processed on the development computer.
- Valhalla `.gph` graph tiles, packaged as `routing/tiles.tar`, support routing.
- `map.pmtiles` contains vector display tiles. It is not the routing graph.
- `places.json` is a bounded local place-name index extracted from the same OSM
  source. It is not a full street-address geocoder.

The first pack targets Kerala. The pack format supports additional regions;
one selected pack provides routing and display for a route in this increment.
It does not stitch independent packs into a border-crossing routing graph. A
larger combined graph is necessary for such routes. Worldwide data availability
does not imply that country-specific driving coaching has been validated.

## User flow

1. Install the candidate APK, then open **Offline map and destination** in parked
   setup. Opening the planner pauses the camera/coaching activity.
2. Import a local `.ldnav` pack through Android's file picker. Importing checks
   file names, declared sizes, SHA-256 hashes, region metadata and a 2 GiB limit.
   Staging is app-owned and failed imports do not replace an installed pack.
3. Enable precise GPS outdoors. Choose a place from the offline index, tap the
   local map, or enter destination latitude and longitude.
4. Calculate a route while GPS reports stationary. Inspect the result, then
   choose **Use route for supervised evaluation**. Unavailable/outside-coverage
   routes show an error; there is no straight-line or online routing fallback.
5. Complete ordinary mount/voice/setup checks and explicitly start the supervised
   session. A local map overlay, position and next maneuver appear with a route.
   Destination changes remain a parked setup task.
6. Clear the saved route in the planner when finished. Selected route coordinates
   remain in app-private storage until cleared; ordinary diagnostic exports omit
   those coordinates. Navigation diagnostics contain route/feature IDs, progress,
   distance and GPS measurement quality.

Routes reflect the installed map snapshot. Closures, road changes, unrecorded
restrictions and missing roads cannot be assumed current or complete. Kerala's
source polygon may omit roads needed for a border route; that requires a larger
data pack, not an assumption that the road is traversable.

## Geometry and position evidence

`RouteCoach::load` validates and precomputes the route off the hot path. Shapes
are bounded to 100,000 points, 1,000 km and no discontinuous segment above 2 km.
Repeated router vertices are collapsed in the matcher while retaining original
maneuver shape indexes and distances. This was required by an actual phone route.
The hot context/policy functions do not allocate, route, access files or run a
model. This does not make the existing Kotlin/JNI arrays, diagnostic JSON or all
legacy guidance code allocation-free.

Bend candidates compare tangents over a 40 m neighborhood sampled every 5 m,
with a provisional heading-change threshold above 35 degrees. Circular heading
differences handle the 360-degree boundary. Nearby candidate samples are grouped.
This is map geometry, not a measured road radius or a safe-speed calculation.
The reported radius is a rough diagnostic derived from the window/angle.

Router maneuver metadata identifies intended route turns. Polyline shape alone
cannot establish an intersection or intended turn. Known maneuver neighborhoods,
including roundabouts and ramps, suppress duplicate geometry-bend lessons.
Unsupported maneuver families retain their visual router instructions without
being converted into invented spoken coaching. Straight-through intersections
are not reliably supplied by route maneuvers; no general intersection detector
is claimed.

Map preparation requires fresh, original-camera-source GPS position and measured
GPS speed. It never borrows manual fallback speed, phone position for a recorded
vehicle, or a screen-test source. Position matching uses cross-track distance,
heading, plausible forward progress and ambiguity checks. It requires at least
two consistent fixes spanning half a second. Accuracy above 20 m, stale fixes
above 2.5 seconds, wrong-way/ambiguous matches and leaving the route pause advice.
A new GPS fix invalidates an outstanding map-speech decision so it is recomputed
before submission. Source changes invalidate matching and require fresh fixes.

This conservative geometric matcher is not a proven lane-level or probabilistic
map matcher. Parallel roads, flyovers, tunnels, hairpins and poor GPS may cause
abstention or errors. Field evaluation must report coverage and misses alongside
false cues. No numeric location-accuracy claim follows from these gates.

## Speech and arbitration

Five new cached PCM clips, generated with the same installed desktop voice as
the existing engineering vocabulary:

- Left bend ahead. Prepare early.
- Right bend ahead. Prepare early.
- Junction ahead. Check for traffic.
- Left turn ahead. Prepare early.
- Right turn ahead. Prepare early.

The generic junction phrase is reserved; this increment does not infer arbitrary
straight-through junctions from a route polyline. Numbered gears, clutch commands,
fixed hump/turn speeds and permission to accelerate are not produced by this
integration. Existing perception does not detect humps or zebra crossings; the
proposed combined junction/hump rule remains inactive pending a real producer.

Provisional map-cue gates retain the requested 100 m bend / 50 m turn approach
windows, with measured speed above 30 / 20 km/h respectively and enough lead
time to finish a short cue. These thresholds are experimental eligibility rules,
not safe-speed advice. Driver braking/shock suppress routine map advice. A feature
is consumed only after playback submission acceptance; routine map spacing is
at least eight seconds, plus the existing shared 3.5-second/10-second duplicate
gates. Map cues never interrupt speech. Existing higher-priority risk/reflex
commands can preempt them, and research emergency speech remains off by default.

The shared coordinator queries the cached geometry on its existing rules worker.
The heavy route/geometry preparation does not run inside the periodic tactical
loop. No second independent audio queue or map-owned emergency authority exists.

## Kerala data preparation

The development source is the OSM France Kerala extract; display data is a
regional, zoom-14 extraction of Protomaps' 17 October 2026 global basemap.
The local place index contains mapped city/town/village/suburb names, including
English tags where present. Exact hashes, sizes and sources belong in the
generated pack sidecar and build report rather than being implied from filenames.

An important regional-build correction: the state extract contains a complete
Kerala boundary but incomplete India/neighbouring boundaries. The upstream admin
builder consequently produced a Kerala row with `drive_on_right=1`. The build
pipeline adds India as a country parent clipped to the same complete Kerala
polygon, with ISO IN and left-driving metadata, and updates the state parent.
This supplies the applicable country metadata inside the supported polygon;
it does not add roads or claim a complete India boundary. See
`tools/navigation_country_admin.py` and the generated country-admin review.
The packaged graph must be checked for actual country/drive-side output.

Build tools:

- `tools/build_navigation_graph.py` — pinned native graph builder, optional
  `--kerala-country-parent`, graph-only tar and provenance report.
- `tools/navigation_places.py` — bounded offline OSM place-name index.
- `tools/package_navigation.py` — PMTiles/graph checks, hashes and versioned pack.

The graph builder can run with the upstream pyvalhalla 3.9.1 Linux wheel in an
existing WSL/Python 3.12 runtime. Tools/data downloads happen during development
and provisioning, not during phone navigation. Build output and source extracts
are reproducible engineering artifacts and are not bundled into the APK.

## Acceptance still requiring evidence

The first physical S24 Ultra / Android 16 check imported the 305,607,592-byte
Kerala pack and rendered its local PMTiles map with INTERNET permission absent.
Kochi–Alappuzha routed 56.291 km with 1,463 shape points: 402.3 ms engine startup,
115.9 ms first route, 25.6 ms warm median and 36.2 ms warm maximum over 20 repeats.
These are one-device functional measurements, not sustained or worst-case bounds.
The cold request exceeded the proposal's 100 ms target. Geometry preparation took
21.3 ms off the periodic loop; this is not the hot geometry evaluation benchmark.

The final APK rerun measured 137.4 ms for the first route and 36.7 ms warm median
(62.0 ms maximum). A separate same-source ARM64 benchmark at `-O0` measured
0.403 microseconds mean for context/policy over 100,000 active bend evaluations,
with zero counted heap allocations. JNI, camera, map rendering and audio are
excluded; integrated frame pacing and sustained timing remain unqualified.

Five desktop routes also produced valid paths and exclusively left-driving
attributes on their checked edges. Some Kozhikode–Kannur administrative records
remain unclassified near state/enclave boundaries. No timezone database, elevation
or live closure data is supplied. Graph building reported 574 uninitialized source
coordinates, seven failed ferry reclassifications and two clamped speed tags;
these remain map-data qualification limitations. Static route generation does not
prove current access restrictions, ferry suitability or learner-route suitability.

See [the verification review](reviews/2026-10-19-offline-navigation.md) for the final
build, test results and remaining road/device acceptance work.

The original proposal's 50 km route below 100 ms, sub-0.5 ms ARM geometry and
60 FPS HUD figures are targets. Report cold initialization, cold route, warm
route, geometry and display performance separately on the S24 Ultra/Android 16.
Host or emulator timings cannot substitute for physical ARM/thermal/audio tests.

Before ordinary learner release: independently review map/maneuver timing,
parallel-road and flyover matching, stale/off-route behavior, missing coverage,
map/vision conflicts, pronunciation and distraction. Verify a held-out set of
Kerala routes, restrictions and left-driving behavior. Preserve the earlier
screen-recording findings and resume that learner-coaching work after this
integration's software/data checks.

Primary references: [Valhalla Mobile](https://github.com/Rallista/valhalla-mobile),
[Valhalla graph generation](https://valhalla.github.io/valhalla/contributing/architecture/mjolnir/),
[upstream country driving-side metadata](https://github.com/valhalla/valhalla/blob/3.9.1/lua/admin.lua),
[MapLibre local PMTiles](https://github.com/maplibre/maplibre-native/blob/main/platform/android/docs/data/PMTiles.md),
[Protomaps extraction](https://docs.protomaps.com/pmtiles/cli),
[Kerala source directory](https://download.openstreetmap.fr/extracts/asia/india/).
