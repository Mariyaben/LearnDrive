# S24 Ultra thermal quick check — 20 October 2026

This is an observational check of the connected phone. It is **not a controlled
map/inference A/B experiment or a sustained thermal qualification**. The phone
was USB charging throughout, and the user entered monitoring during the run,
changing the workload. The sampled readings support an app contribution to heat;
they cannot assign causal shares to inference, maps, display or charging.

## Device and artifact

| Item | Observed value |
| --- | --- |
| Device | Samsung S24 Ultra / Android 16 |
| Installed application | LearnDrive 0.4.2, version code 22 |
| Installed APK SHA-256 | `fff3d4698501072aa7a9c157687beed5393b63ec2e357fb0b90a09b18c467f66` |
| Artifact comparison | Installed APK hash matches the newly built APK |
| Charging | USB charging true throughout the observations |
| Installation during this check | None; the matching APK was already installed |
| Runtime policy changes during this check | None |

MainActivity was launched for this observation run and force-stopped at the end.
No target-APK installation or inference/thermal-policy adjustment was made as
part of this check.

## Observed checkpoints

Temperatures below are the device HAL's reported Celsius values for the named
AP, BAT and SKIN sensors. They are not interchangeable with each other or with
an independently measured surface temperature. No heating rate is calculated:
the observations do not establish matched elapsed sampling windows.

| Checkpoint | App/workload condition | AP °C | BAT °C | SKIN °C | Reported thermal state |
| --- | --- | ---: | ---: | ---: | --- |
| Before launch | No LearnDrive PID; USB charging | 39.9 | 33.9 | 35.8 | Global status 0 |
| Later, app running | Workload changed from setup to monitoring | 51.7 | 36.5 | 41.0 | Skin status 1 |
| Immediately after force-stop | App just stopped; USB charging | 52.9 | 38.7 | 42.8 | Skin status 2 |
| After force-stop | App run ended; USB charging continued | 47.3 | 38.7 | 41.6 | Global status 2 |
| Later cooldown sample | App remained stopped; USB charging | 43.5 | 37.7 | 39.4 | Global status 1 |
| Further cooldown sample | App remained stopped; USB charging | 43.2 | 37.0 | 38.7 | Global status 1 |

Initial app evidence reported **spatial readiness 2 / setup 0**. Later evidence
reported **spatial readiness 4 / setup 1** after the user entered monitoring.
In this source, readiness 2 means fresh gravity/pose was unavailable and readiness
4 means spatial input was ready. Consequently, Supercombo eligibility and
session state were not held constant across the temperature observations.

Two process CPU snapshots reported **159%** and **188%** for LearnDrive, where
100% represents one CPU core. These indicate substantial process CPU work during
their respective sample windows, not a percentage of the entire multicore phone.

A sampled thread list included:

| Thread label | Sampled CPU |
| --- | ---: |
| `vision` thread entries | 93.3%, 30.0%, 23.3%, 13.3% |
| `GL-Map` | 20.0% |
| `RenderThread` | 16.6% |
| Main thread | 13.3% |
| MapLibre TextureView / PMTiles worker entries | 0% in the sampled list |

Do not sum separate snapshots into a simultaneous power attribution. CPU thread
samples do not measure GPU power; 0% in a sample does not prove a component had
no cost elsewhere in the run. In particular, this list does not establish that
offline vectors caused the heat or that map rendering was free.

The active provider log reported **XNNPACK**, a warm Supercombo benchmark of
**35.1646 ms**, and **big-core affinity 1**. This is evidence of the selected CPU
execution path and reported affinity; it is not evidence of Snapdragon NPU use.
The initialization benchmark is distinct from sustained end-to-end latency.

The app's pipeline log showed approximately **8.6–16.4 completions/second** and
latest processing durations of **53–129 ms**. These values are not per-model
FPS or latency percentiles. A pipeline completion can omit the detector or
Supercombo depending on cadence and spatial readiness.

## Source findings relevant to the observations

- The camera analyzer calls `Vision.frame` during parked setup as well as
  monitoring. Session state gates coaching speech; it does not currently gate
  the expensive inference path.
- The active Android Vision class loads the object detector and Supercombo.
  The detector is eligible at up to 8 Hz. Supercombo runs on every admitted
  frame when camera optics and fresh forward pose qualify. The bundled
  `road.onnx` file is not a third model running in this path.
- Native Supercombo attempts XNNPACK, falls back to CPU, requests two intra-op
  threads and pins the calling inference thread to the two highest-frequency
  allowed CPUs when affinity succeeds. Its native ORT spinning options are
  disabled. The Java detector session separately requests two intra-op threads
  but does not set the same explicit no-spinning options.
- The thermal scheduler changes maximum analysis-start rates from 30 Hz at
  status 0 to 25 Hz at status 1 and 20 Hz at status 2. With processing already
  taking 53–129 ms, those light/moderate admission caps may provide little or
  no additional idle time. A displayed reduction message alone does not prove
  that delivered compute dropped.
- Severe status uses a 1.5-second analysis interval; critical status skips
  inference. Camera preview, maps, sensors, UI work and rules are still separate
  workloads. Skipping model work does not stop all app power use.
- Offline MapLibre is constructed, can receive a regional pack, and receives
  lifecycle start/resume even when Google is selected. Whether its detached
  surface performs material rendering work needs measurement. Its 30 FPS
  setting is a maximum, and unchanged GPS positions are deduplicated.
  Offline routing/search is on demand rather than a per-camera-frame operation.

Relevant source: `MainActivity.kt`, `CameraVisionHealth.java`, `Vision.kt`,
`SpatialVision.kt`, `core/src/perception/supercombo_runtime.cpp`, and
`navigation/OfflineNavigationMapSurface.kt`.

## What this check establishes

LearnDrive imposed substantial CPU load while device temperatures rose, and
vision-labelled threads were the largest sampled CPU consumers. This is
consistent with inference contributing materially to heating. The lower AP/SKIN
readings after stopping are also consistent with removal of app workload, but
are not a controlled causal comparison. Charging, elapsed time, changing
readiness/session state and other unmeasured work remain confounders.

The observation does **not** quantify map GPU cost, compare offline versus Google
maps under matched conditions, establish a safe sustained operating envelope,
or qualify learner-facing smoothness. It does not demonstrate a specific fault
in Samsung's thermal reporting.

## Controls needed before choosing and qualifying fixes

1. Use the same APK, brightness, mounted camera scene, connectivity, battery
   range and ambient conditions. Record charging state and start each trial
   from a comparable cooled baseline. Compare charging and non-charging trials
   separately instead of mixing them.
2. Hold setup/monitoring state and spatial readiness constant. Measure idle,
   map/UI only, camera/semantic only, camera/semantic/spatial with map paused,
   then the same inference workload with offline and Google maps. Controlled
   per-stage switches are needed; changing phone orientation or pointing it at
   a different scene changes more than inference eligibility.
3. Record detector and Supercombo call counts/durations, admitted/skipped camera
   frames, spatial readiness, thermal transitions, CPU/GPU activity and render
   cadence without recording raw GPS coordinates or API keys. Existing pipeline
   FPS alone cannot establish actual model cadence or map power.
4. Choose optimizations from the measured duty cycle: setup workload, inactive
   map lifecycle, inference provider/thread policy and separate stage budgets
   are candidates, not proven fixes from this run.
5. Requalify freshness and model behavior after cadence changes. Native
   Supercombo resets temporal state after capture gaps above 250 ms; simply
   lowering its rate can change model semantics. Reduced compute must not
   retain stale metric evidence or pretend unchanged coaching coverage.

No full thermal qualification or corrective-policy pass is claimed by this
quick check.

Follow-up: the [wireless map-display comparison](2026-10-20-map-rendering-thermal-check.md)
completed in 0.4.3, with approximately 2.1 app CPU cores still used while the
active Google display was suspended. Workload matching failed; see that record
for the thermal, throughput and Battery Saver limits and preserved raw evidence.
