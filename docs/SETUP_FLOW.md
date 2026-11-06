# Parked setup, level references and mount consistency

Android increment 1b, 17 October 2026. Software verification is recorded in
EXECUTION.md; physical-device qualification is a separate gate. This follows
[automatic horizon readiness](MOUNT_READINESS.md).

## User flow

1. Open to the unified map/road-camera dashboard and a parked setup panel. Lab settings and test
   sources are collapsed. Screen-test mode must be selected explicitly each launch;
   an old saved test-mode preference no longer changes the default road source.
   In 0.4.7, Get ready and Destination have separate pages. Where to opens search
   directly; a successful destination selection returns to preparation. Start
   stays fixed below the scrollable options. Closing the drawer exposes the map,
   smaller camera pane and shared coaching strip. Both landscape directions are
   supported, including updates to CameraX's preview and analysis rotations.
2. Fix the phone facing forward, check its view and use Check sound. Camera
   analysis can run during setup, but road coaching is inactive before Start.
3. Allow location for measured speed and route context, or continue basic camera
   attention with speed unavailable. Android 0.4.1 never substitutes a manual
   speed in ordinary road mode. Simulated speeds belong to explicit lab tests.
4. In 0.4.8, tap Start session, then Level ground · start to confirm the fixed
   forward-facing mount and parked, level ground. Level-reference calibration
   begins automatically; hold still for the existing five-second normalization.
   The bounded request starts coaching only after a new confirmed reference and
   fresh camera admission. Back cancels it. Skip slope lessons remains available
   when level ground or motion support is unavailable. This is a human declaration
   of level ground, not a sensor-established fact. Missing/unsupported sensors
   must not prevent a session without inclination lessons; Skip is available.
5. Actual monitoring starts only after a current camera result and voice readiness, and explicitly
   confirm the fixed forward-facing mount while parked. Missing optical/motion
   readiness permits a clearly limited semantic-only session. An unsupported
   near-vertical pose, hot/failed/stalled camera or measured speed above 1 km/h
   blocks starting. These are engineering settings awaiting device qualification.
   The Start entry itself keeps a stable label and remains available while parked
   to explain readiness; opening its dialog never establishes monitoring.
6. During the road session, map, measured status, camera and one coaching strip
   remain visible; lab/setup controls are hidden and the screen stays awake.
   Ending a session returns to setup and clears its level confirmation. Pausing
   or backgrounding also requires starting again; coaching does not silently resume.

Desk, MP4 and screen tests remain explicit lab paths. Their existing simulated
inputs and provenance rules are separate from the mounted-road setup gate.
Experimental emergency commands still reset off.

## Reference identity and freshness

The shared fusion exposes a monotonically increasing normalization generation
within an engine's lifetime. Reset preserves the counter; only a successful
normalization increments it. Existing telemetry layout is unchanged.

Android records the generation at an explicit level request and accepts only a
new, completed generation with fresh motion. An automatic normalization at launch
does not qualify. A request that cannot complete within 15 seconds of its first
update expires, so a much later normalization cannot inherit an old declaration.
Users can skip or retry while parked.

A confirmed reference expires when fresh motion or native normalization is lost/replaced or the
session/source/lifecycle ends. Automatic reacquisition does not confirm level
ground again. Expiry cancels current audio. Without the matching reference, Android clears the advice pitch
validity and disables slope coaching while retaining measured braking/shock state
and basic camera guidance. Displayed physical inclination is gated the same way.

This does not prove absolute grade, forward heading, mount height, camera
calibration accuracy or the slope ahead. Normalization on wrongly declared ground
still produces a wrong reference; the UI explains that limit.

## Limited mount consistency check

During a confirmed road session, observe camera-up roll relative to the starting
pose. A difference of at least 15 degrees sustained over two seconds of fresh
updates, with measured GPS/fusion speed between 0 and 1 km/h throughout that
interval, enters Check mount. Gaps above 250 ms, missing pose, unknown speed or
moving speed restart the dwell. Angle wrapping across landscape orientations is
handled. Pure pitch changes on hills do not change this roll comparison.

Check mount pauses road coaching, cancels current speech, invalidates the source's
pending work and level reference, and returns to parked setup. It never silently
establishes a new level reference. The native motion frame resets so the previous
mount's normalization is not reused; automatic reacquisition remains unconfirmed
for inclination coaching. The message describes an observed roll change while measured
speed was near zero, not a proven cause.

Limits: this cannot detect yaw-only movement, every changed mount, or movement
without eligible speed/pose. Vehicle banking, GPS error and phone motion can be
ambiguous; thresholds are provisional. The app must not claim a universally
verified mount. This is a consistency heuristic requiring physical evaluation.

## Diagnostic additions

Android's additive schema-v1 fields:

| Field | Meaning |
| --- | --- |
| `setup_state` | 0 setup, 1 monitoring, 2 check mount; null outside the mounted-road path |
| `calibration_id` | Native successful normalization generation; zero before completion |
| `level_reference_id` | Matching explicitly confirmed generation, or zero; null outside road mode |

An ID does not imply fresh motion or validated level ground. Existing source IDs,
availability flags and speech acknowledgement records retain their meanings.

## Automated checks

Run `powershell -NoProfile -File tools/run_camera_health_tests.ps1` for the existing
camera/readiness checks and `DriveSetupTest`. Setup cases include missing motion,
automatic versus explicit references, re-normalization, sustained roll, moving
and unknown speed, hills, short disturbances, angle wrapping, sensor gaps,
source/session reset, skipping optional reference and expired requests.

The C++ suite checks generation provenance across explicit resets and automatic
reacquisition, including the C ABI. Native policy/reflex/perception regressions
remain required. No synthetic test qualifies hardware, UI usability or road accuracy.

## Physical angle-fixture and lifecycle protocol

Status: not run here; record results per phone/Android version/APK hash. Park the
vehicle and use an observer and a measured phone-angle fixture. Record whether
GPS is actually available and separate simulated sensor tests from physical ones.

| Check | Expected result / evidence to capture |
| --- | --- |
| Fresh launch and previously saved screen-test preference | Road setup appears, lab panel collapsed; no road coaching before Start. Camera and voice errors remain visible. |
| Optional reference omitted | Limited/current capabilities are labelled; inclination coaching stays off even after automatic native normalization completes. |
| Explicit level reference | On independently checked level ground, fixed forward mount and five seconds still: reference completes with a new generation. Invalid setup remains unavailable. |
| Known fixture pitch +/-5 and +/-10 degrees | With a confirmed reference, compare displayed inclination/grade and reference angle; record errors. This is phone inclination, not perception of an upcoming hill. |
| Optional reference cannot complete | Sensor unavailable or sustained movement must allow Skip; a completion after the 15-second request limit must not reuse confirmation. |
| Fixed mount, vehicle stationary, known GPS fix, sustained roll over 15 degrees | After the two-second observation dwell, Check mount pauses coaching, cancels speech and expires reference; confirm source/decision IDs in logcat. |
| Brief roll disturbance, pure pitch, missing GPS | No claim that mount movement was reliably detected. Brief changes must not accumulate; pitch-only changes must not trigger the roll check. |
| Turning/banking under supervised evaluation | Quantify false check-mount states and missed mount changes separately; do not infer success from synthetic moving-speed tests. No deliberate risky manoeuvres. |
| Pause/resume, camera restart, Desk/MP4/screen switches | Old decisions/results cannot leak; paused/ended sessions require explicit restart, and new automatic normalization cannot reuse a level confirmation. |
| Measured movement and setup controls | Start/level setup is blocked on reported moving speed; GPS/manual/unknown provenance remains clear. Observe GPS jitter and false blocks while actually parked. |
| Real audio start/stop and interrupted dialogs | Camera rebind/Start/End/Check mount and backgrounding preserve cooldowns and stop obsolete speech; check actual sound, not just submission logs. |

Retain screenshots, logcat source/decision/reference IDs, fixture angles, actual
GPS status, observed timing and participant notes. Fix false blocks and ambiguous
wording before qualifying the setup flow. No completed results are implied by
this protocol.
