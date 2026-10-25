# S24 Ultra map display isolation — 20 October 2026

## Artifact and controls

Diagnostic Android **0.4.3 / code 23** is installed on the paired S24 Ultra,
Android 16. Its installed SHA-256 matches the staged APK:
`2a97ad6e1db5bbbd23149522e512d9c040f657bd1eb5a1e8911ddf54bf558f64`.
The APK is 237,612,997 bytes and retains the existing debug signing certificate.
The earlier 0.4.2 artifact and its verification evidence are preserved.

Assembly and AndroidTest compilation pass. Five physical caption/provider contract
cases pass. APK asset verification passes. The analyzer's eight synthetic cases
check valid, aborted, charging, reset-counter, intermediate-throttle and blank-map
records, plus enabled/missing Battery Saver observations. These are not
substitutes for a completed physical comparison.

This increment adds cumulative immutable Vision snapshots and an explicit
diagnostic map display suspension hook. It does not change model providers,
cadence, calibration, thermal admission limits or coaching selection.
Successful native Supercombo completions are counted separately from attempted
spatial processing stages. Constructor hashing/warmup is excluded.

The intended bench protocol is active map display **on → off → on**, with a
10-second settling interval before each measured window. The same camera source,
models, scene, fixed phone, provider, setup/session and route remain in use.
Window brightness is fixed at 0.4 for the test activity. Map suspension hides
only the map child and pauses/stops its display lifecycle; Navigator/feed,
position intake, native route context, camera and speech are not intentionally
stopped. The activity and camera close after completion or abort.

This is an active-provider display comparison. An inactive map provider remains
in its existing lifecycle state. It does not measure all map/network/location
work, pure GPU power, or sustained driving performance. Tilt checks cannot
detect yaw/translation or prove that the scene stayed unchanged.

## Preparation observation

Wireless ADB connected successfully. Initially, no LearnDrive process was
running: thermal status 0, AP 37.0°C, battery 34.9°C and skin 35.2°C. All three
charging flags were false.

During preparation, the existing 0.4.2 app was subsequently found running. While
still unplugged, it reached thermal status 2, AP 53.1°C, battery 39.8°C and skin
43.1°C. Its performance log reported approximately 6 completed pipeline frames
per second and latest processing durations of 152–164 ms. Those logs do not
establish per-model cadence. This was not a controlled inference/map comparison;
the app's intervening launch time and other workloads were not recorded.

LearnDrive was force-stopped for cooldown before installation/measurement. The
allowlisted cooldown trace records a residual skin peak of 43.3°C, falling to
40.0°C after approximately 130 seconds. Charging remained absent. This supports
that charging alone does not explain the observed heat; it does not allocate
heat to a particular component.

## Physical comparison result

The first attempt aborted after an intermediate semantic-first publication had
no FrameContext, which the harness incorrectly treated as changed camera geometry.
The camera and completed-inference counters were still advancing. The harness
was corrected to wait up to 1.5 seconds for a completed camera result. This was
a diagnostic sampling defect, not evidence of a camera rebind. Its raw data and
failed instrumentation result remain in `run-1-*`.

The second attempt completed all three phases in 93.807 seconds. Google was the
active provider, with no destination/guidance and setup state 0. Each phase has
10 seconds of settling followed by approximately 20 measured seconds. All
completed frames in the measured windows included a successful native spatial
inference. No frame, detector or spatial failures occurred. Camera source,
model instance, tilt, geometry, map host bounds and setup state stayed stable
at the recorded samples. The map rendered again in the final on phase.

| Measured phase | Process CPU | Detector calls/s | Native spatial completions/s | Detector mean ms | Spatial stage mean ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Google display on, first | 230.2% | 6.80 | 8.25 | 45.5 | 70.5 |
| Google display suspended | 209.9% | 6.85 | 6.85 | 48.2 | 80.7 |
| Google display restored | 233.7% | 6.95 | 7.20 | 47.7 | 76.1 |

100% CPU means one core. The instrumentation runs in the app process, so these
values include its small sampling overhead. Detector timing covers model run
and output copy. Spatial timing covers geometry/packing/JNI/inference and is
not pure model latency. Frame processing occupied **99.2–99.4%** of measured
wall time on the serial inference executor, including when the display was
suspended. This is elapsed occupancy, not a CPU-power measurement.

**Collection completed; matched-workload qualification failed.** Thermal status
changed from 0 to 1 during the first measured window. Spatial throughput's
maximum/minimum ratio was approximately 1.204, exceeding the declared 1.2
matching bound. Battery Saver was observed enabled at battery level 20 during
the off phase; its baseline state was not recorded. This does not establish
when or why it enabled. The independent analyzer therefore withholds a causal
CPU difference. JUnit's one completed case verifies collection/continuity,
not a passing thermal or matched-performance qualification.

The trace spans roughly 37.8°C to 41.8°C at the skin sensor, while unplugged.
Do not attribute this temperature rise or the CPU difference solely to maps.
OLED pixel content also changes when the map child is hidden; a fixed window
brightness does not fix display power. Inactive offline map cost and other
Google background/network/location work were not removed by this active-display
hook. This does not compare Google with Kerala vectors.

The defensible finding is that substantial app load persists without the active
map display: about **2.1 CPU cores**, with the inference executor nearly always
occupied. Combined with the earlier vision-thread samples and CPU provider
evidence, this makes AI duty cycle a stronger optimization priority than removing
the offline pack. It does not quantify causal shares or prove any proposed fix.

## Follow-up controls and optimization order

The latest compiled harness now records Battery Saver on every sample and
refuses to start/continue a normal-performance comparison when it is enabled.
The host sampler also records the allowlisted global power-saving setting.
These guards were added **after** physical run 2; its missing fields remain
missing, and it remains unmatched. No third heat-producing comparison was run.
The physical run's test APK is preserved as `test-run-2.apk`, SHA-256
`854990f9a1b96032b6fef69b8c7333509e3a98bdae25b0e0bd13cd5a5800b418`.

Next engineering experiments, individually qualified:

1. Disable Java detector ORT worker spinning in both provider branches, while
   retaining provider choice, threads and cadence. Native Supercombo already
   disables spinning. Check detector wake-up latency, outputs and freshness
   before claiming a CPU/heat improvement.
2. Avoid duplicate overlay invalidations for the same immutable published
   result, retaining new-result and stale/source-change clearing.
3. Define a reduced parked-setup workload and an effective active-session
   compute budget. These change product/runtime behavior and need separate
   qualification. Current 30/25/20 Hz admission caps add little idle when each
   frame already takes much longer than 33–50 ms. Simply reducing Supercombo
   cadence can reset temporal state beyond its 250 ms gap threshold.

No optimization or thermal-protection bypass was applied in this increment.
LearnDrive was force-stopped after the capture for cooldown.

Evidence directory: `build/thermal-map-comparison/`. Measurement source:
`ThermalMapComparisonTest.kt`; host capture:
`tools/capture_android_thermal.py`; independent analysis:
`tools/analyze_thermal_map_comparison.py`.
