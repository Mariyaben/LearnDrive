# Automatic horizon readiness — first slice

Implemented 17 October 2026 for Android. This is increment 1a of
[MASTER_ACTION_PLAN.md](MASTER_ACTION_PLAN.md), not completed instant-mount setup
or physical calibration acceptance.

The subsequent [parked setup flow](SETUP_FLOW.md) adds explicit session/level
confirmation, normalization identity and a limited roll-consistency check. Its
physical qualification remains separate from these host checks.

## Behavior

The app already collects IMU data automatically. With a usable steady mount,
native fusion can supply a capture-aligned gravity pose after its initial
sample window (more than 20 samples; about 200 ms at 100 Hz). This is separate
from the five-second level-ground normalization used for relative inclination.
Neither interval is a measured phone startup guarantee.

Android now runs spatial inference only with camera optical metadata and a
usable capture-time pose. A missing pose no longer becomes an assumed upright
phone. A camera aimed too close to the ground or sky is unsupported: its forward
horizontal projection must be at least 0.35 of the unit optical-axis geometry.
The same degeneracy gate is enforced by the shared C++ horizon function.
Landscape rotation and supported tilt remain accepted.

The lab status distinguishes original calibration unknown (recordings/screen),
missing camera optics, waiting for fresh motion and unsupported mount. A usable
pose is described as horizon alignment, not a validated road or clear trajectory.
Basic semantic detections continue in the unavailable states.

On losing readiness, the native source-aware invalidation call removes spatial
outputs, metric/optical tracking history, derived hazard scenes and pending
guidance. Semantic boxes remain displayable with their original freshness;
global speech cooldowns and duplicate locks remain. Repeated unavailable frames
do not cancel newly selected semantic-only guidance. Android clears recurrent
state and refreshes semantic evidence before resuming normal spatial use.

Source IDs and a capture-time watermark reject old-source and pre-loss model
results. Recovery requires newly supplied geometry and a current model result.
Recordings and screen tests still cannot borrow the phone's pose or optics.

## Diagnostics

Android decision traces add optional `spatial_readiness` to schema version 1:

| Value | Meaning |
| --- | --- |
| null | No analyzed frame available |
| 0 | Original calibration unknown |
| 1 | Camera optics unavailable |
| 2 | Fresh usable gravity unavailable |
| 3 | Near-vertical/unsupported mount |
| 4 | Pose and optics available for spatial inference |

This describes readiness for that analyzed frame. `spatial_available` separately
reports freshness of a published spatial result. Neither is a hazard-accuracy metric.

## Reproduce software checks

```powershell
cmake --build build/native-spatial --parallel 2
ctest --test-dir build/native-spatial --output-on-failure
powershell -NoProfile -File tools/run_camera_health_tests.ps1
cd android
.\gradlew.bat :app:assembleDebug --offline
```

Native regressions cover startup/warmup, stale pose, supported rotations and tilt,
invalid/vertical gravity, lost/recovered spatial evidence, semantic continuity,
old-source/older work, obsolete acknowledgements and preserved audio gates.
The Java readiness test runs independently of Android hardware, alongside the
existing eight camera-health cases. Build/check results go in EXECUTION.md.

## Still to validate or implement

- Physical phone startup, tilted/landscape mount fixture, missing sensor behavior,
  background/resume, sensor loss/recovery and audio continuity.
- Physical robustness of the new parked setup and limited roll-consistency check;
  universal mount/yaw-change detection remains unavailable.
- Forward-heading and road-grade assumptions. Gravity cannot validate heading,
  recognize level ground or measure the slope ahead.
- Low-light/blur/obstruction checks with measured limits; no claimed defensive
  spatial fallback when visibility is poor.
- Calibrated optics/distortion, temporal cadence and perception accuracy audit.
- iOS orchestration still has its earlier pose fallback and requires a separate
  migration plus macOS build/device checks; this slice does not qualify iOS.

Experimental emergency commands stay off by default. This increment does not
fix the false/irrelevant cues in PUBLIC_TRAFFIC_REVIEW.md or qualify a learner release.
