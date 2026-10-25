# Deterministic driving decisions and post-drive summaries

Both mobile shells consume the shared C++ safety and tactical policies.
Android and iOS now use the native Supercombo spatial runtime. The source,
model hash, full tensor contract and acceptance checks are recorded in
[INTEGRATION.md](INTEGRATION.md).

The dedicated rules worker evaluates the latest bounded scene every 10 ms.
The safety reflex scans at most 64 objects, performs no heap allocation or
inference, and is `noexcept`. The tactical policy is limited to 100 ms intervals.
All capture, motion and rule timestamps use one monotonic clock per platform.
Vision evidence expires after 350 ms and its capture age is deducted from TTI.
Negative/invalid TTI and nonfinite confidence never create an emergency.

Signed forward IMU acceleration selects coasting above -1.8 m/s², moderate
braking from -3.5 through -1.8, and hard braking below -3.5. Braking suppresses
routine distance/lane advice. TTI below 1.2 s during braking escalates to
`BRAKE_HARDER`; a critical unbraked approach produces `EMERGENCY_STOP`.
The most imminent intersecting hazard wins. Rear tailgaters do not enter the
forward reflex. Fresh spatial lead predictions and optical approach estimates
provide collision evidence; image occupancy remains a caution rather than
calibrated distance.

The tactical engine computes severity × ego speed / (TTI × lateral separation).
Finite positive floors bound centerline and zero-TTI denominators. Bus and
pedestrian severity is 1, pothole severity .25, tailgater severity .10. Measured
trajectory intersections and score comparisons select fixed action IDs. A
lead bus plus pothole selects `ACTION_HOLD_DISTANCE_BUS` rather than lateral
avoidance. Pedestrian/vehicle priority changes with the measured threat score.

Every accepted non-emergency camera, terrain or tactical prompt shares the
native 3.5 s audio gate and a 10 s duplicate-phrase lock. Emergency clips bypass
the gate and preempt current speech. Continuing emergencies repeat no faster
than every 2 s; increasing emergency severity can interrupt immediately. Audio
rejection does not commit cooldowns. A JNI callback stops/flushes cached Android
PCM directly before the emergency is dispatched; iOS uses AVAudioPlayerNode.

`DualBrainEngine` defers the optional model factory until an explicit
`summarizeAfterDrive` call after `stop`. `PostDriveSummary` runs that request
asynchronously and returns a future with no speech dispatcher reference. The
driving tick contains no model loading, model submission or generated-result
consumption. Summary failures cannot disable deterministic driving decisions.
Older ONNX decoder/provider experiments remain documented in
[ONNX_DECODER_REFERENCE.md](ONNX_DECODER_REFERENCE.md).

Automated tests cover thresholds, stale frames, cooldowns, suppression,
escalation, zero heap allocations, threat ordering and the model/speech
separation. Host tests do not establish worst-case ARM64 timing or physical
audio latency. The app logs a native decision benchmark and per-second
inference/FPS/thermal measurements for `tools/profile_android.py`.
