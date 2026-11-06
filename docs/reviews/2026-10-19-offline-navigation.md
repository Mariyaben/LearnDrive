# Kerala offline navigation integration — 19 October 2026

The user explicitly prioritized the offline map/routing integration before the
learner-coaching redesign in the screen-recording review. Kerala is the first
installed region. The earlier false-detection/usefulness findings remain open.

## Delivered architecture

- Android Valhalla Mobile 0.6.4 / native Valhalla 3.9.1 computes routes locally on
  a dedicated worker. Its published binding avoids maintaining a second private
  cross-build of the routing engine.
- MapLibre Android 13.6.1 renders an app-owned style from local PMTiles. The merged
  APK explicitly has no INTERNET permission. Place-name lookup is a local index
  of 3,665 mapped city/town/village/suburb points, not full address geocoding.
- A regional `.ldnav` file separates the routing graph, vector map and place
  index. Import validates bounded names/sizes, SHA-256 integrity and PMTiles
  metadata before installing into app-private storage.
- The shared C++ core precomputes geometry and conservatively matches measured
  GPS. Stale/poor/ambiguous/off-route position pauses map coaching; recordings
  and simulations cannot borrow current phone position or manual fallback speed.
- Five cached preparation clips use the existing speech coordinator. Map advice
  does not interrupt speech; stronger risk/reflex priorities remain above it.
  Numbered gears, clutch actions, calculated safe speeds and arbitrary junctions
  are not inferred from this data.

The upstream state-only admin build initially marked Kerala as right-driving.
The build tool supplies an India country parent clipped to the complete Kerala
polygon and sets both rows to left-driving before graph construction. This
corrects applicable metadata without expanding road coverage. Checked graph
edges in five routes report left-driving; some border/enclave admin records in
Kozhikode–Kannur remain unclassified.

## Actual data and device evidence

Source: OSM France Kerala extract; Protomaps 17 October 2026 basemap, bounding
envelope west 74.8 / south 8.0 / east 77.7 / north 12.9, maximum zoom 14. The
envelope includes places outside the state's routing polygon; graph availability
is checked by Valhalla. Independent packs are not stitched across their borders.

`build/LearnDrive-Kerala-20261019.ldnav` is 305,607,592 bytes (about 291 MiB), with
164 graph tiles. SHA-256:
`9e1cd026c640a131dc618d6881ac2ce854408d65a92ac2b67412f5d8e56955db`.
The sidecar and source/build reports retain constituent hashes and provenance.

On the user's **Samsung SM-S928B / Android 16 / API 36**, the initial real-pack
test imported and displayed Kerala with app INTERNET permission absent. The
Kochi–Alappuzha route was 56.291 km / 1,463 points:

| Measurement | First physical run | Final APK run |
| --- | ---: | ---: |
| Engine initialization | 402.3 ms | 31.3 ms |
| First route request | 115.9 ms | 137.4 ms |
| Warm route median, 20 requests | 25.6 ms | 36.7 ms |
| Warm route maximum | 36.2 ms | 62.0 ms |
| Cold shared geometry preparation | 21.3 ms | 28.3 ms |

Initialization and routing are separate. The first request did **not** meet the
proposed 100 ms target. Cold geometry preparation is off the periodic loop and
does not establish ARM hot-loop latency. The desktop context/policy test checks
zero heap allocations within the new core hot functions; JNI arrays, diagnostic
JSON, rendering and other app code are outside that claim. On the same S24,
a separate ARM64 microbenchmark compiled with the app's NDK 28.2 at `-O0`
measured **0.403 microseconds mean** over 100,000 context/policy evaluations of
the active 50-degree / 90 m / 45 km/h bend scenario, with zero counted heap
allocations. It compiles the same geometry source, excludes JNI/rendering/camera,
and establishes a mean for this scenario, not a worst-case whole-app guarantee.
No radios were disabled during testing; the app's absent INTERNET permission
prevents its routing/rendering process from making network requests.

Five desktop routes (Kochi–Alappuzha, Alappuzha–Thiruvananthapuram,
Kozhikode–Kannur, Palakkad–Kochi and a Kochi local route) passed response and graph
drive-side checks. London routing was rejected. Desktop cold requests were
5–59 seconds under severe host memory contention; warm medians were 10–26 ms.
Neither host figure substitutes for phone measurements.

The phone fixture exposed a valid route ending in a duplicate coordinate. The
initial core rejected it. The correction removes zero-length matcher segments
while preserving original maneuver indexes; regression checks cover repeated
start/middle/end vertices and entirely collapsed routes.

Final checks: **10/10 native test groups**, including **51 navigation checks**;
**3/3 Android instrumentation cases** on the physical S24; all 51 PCM clips,
models, app-owned routing configuration and both ABI copies verified against the
APK. Signature verifies with the existing debug certificate. All packaged native
libraries have 16 KiB ELF load alignment; no 16 KiB-page device run is claimed.
The planner screenshot was inspected after correcting system-bar overlap. Changing
destination now clears the old route line; position updates reuse its geometry.

Final APK: `build/LearnDrive-0.4.0-debug.apk`, version code 20, 184,321,526 bytes;
SHA-256 `bbfc14341b127070c9300f85dcc5538c90ea58783648dd445b6719fa52f9301a`.
Machine-readable evidence: `build/navigation-data/verification.json`; underlying
phone outputs and screenshots: `build/navigation-data/kerala/device/`.

## Reproduction and remaining acceptance

Build with `android/gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest`.
Run `ctest --test-dir build/native-spatial --output-on-failure` for native tests.
On an authorized physical phone install both APKs, then run instrumentation:

```text
adb shell am instrument -w -e class ai.learndrive.mobile.navigation.OfflineNavigationTest ai.learndrive.mobile.test/androidx.test.runner.AndroidJUnitRunner
adb push build/LearnDrive-Kerala-20261019.ldnav /sdcard/Android/data/ai.learndrive.mobile/files/kerala.ldnav
adb shell am instrument -w -e class ai.learndrive.mobile.navigation.KeralaNavigationTest ai.learndrive.mobile.test/androidx.test.runner.AndroidJUnitRunner
```

The Kerala test requires the real provisioned pack, checks route generation and
native geometry loading without GPS fabrication, and retains a planner screenshot.
It does not select a live route, alter GPS settings or start a driving session.

To repeat the ARM microbenchmark, compile
`core/tests/navigation_device_benchmark.cpp` and
`core/src/navigation/polyline_analyzer.cpp` with the installed NDK clang++,
`--target=aarch64-linux-android28 -std=c++17 -O0 -static-libstdc++ -Icore/include`.
Push the executable to `/data/local/tmp`, grant executable permission and run
through ADB. The tool checks the expected cue, mean latency below 500 microseconds
and zero allocations in its measured loop. APK performance still requires
integrated workload and sustained measurements.

Still required: independent Kerala route/restriction review; real GPS testing at
parallel roads, flyovers, hairpins, tunnels and route departures; hot-loop ARM,
camera/map/audio coexistence, frame pacing and sustained thermal measurements;
audibility/pronunciation/distraction and instructor/learner usefulness assessment.
No timezone database, elevation or live closure source is provisioned. Source
build warnings include 574 uninitialized coordinates, seven failed ferry
reclassifications and two clamped speeds. No ordinary learner-release or
market-readiness conclusion follows from these software checks.

Keep the previous gear-estimation research and screen-recording coaching plan;
route geometry is additional context, not proof of the right gear or pedal action.
