# LearnDrive.AI architecture audit and implementation plan

25 October 2026. Baseline: Android 0.4.8 / 28, published snapshot `34a63c3`.
This audit was completed before application-source changes for Balanced coaching.
Both supplied prompts and the preceding product/UI/session decisions are inputs.

## 1. Confirmed architecture

The Android application uses native Views/Kotlin, CameraX 1.4.1, C++17/JNI and
ONNX Runtime 1.22.0. Compile/target SDK is 36, minimum 28. Gradle 8.13,
AGP 8.13.2 and Kotlin 2.2.10 are pinned. ARM64 and x86-64 are packaged.
The charcoal dashboard, sensor-landscape orientation, in-place destination
entry, stable Start action, explicit mount/ground confirmation and bounded
automatic level-reference start must remain intact.

`MainActivity` owns lifecycle and source generations. CameraX keeps only the
latest analysis frame. `Vision.kt` uses one inference executor, stamps capture
time/source/crop/rotation/calibration, publishes YOLO semantics first and spatial
results later. Short `NativeCore` operations are synchronized; inference runs
outside that lock. A separate rules handler calls `evaluate` approximately every
10 ms. `ld_guidance_decide` combines tactical, reflex, terrain, camera and route
candidates. Submission through `AudioWorker` is acknowledged by sequence;
cached PCM uses one audio worker, cancellation generations and focus handling.
No cloud TTS or language model is required for the active driving path.

Reusable code:

| Concern | Current implementation |
| --- | --- |
| Camera tracking | `ApproachTracker` in `core/src/vision.cpp`: same-class IoU association, optical patch evidence and expansion history |
| Attention encounters | `CameraCoach` in `core/src/decision.cpp`: distinct captures, 750 ms identity history, dwell/clear hysteresis, entry/reference proof and rider association |
| Other episodes | `DrivingCoach` slope/bend/shift hysteresis; `EmbeddedDualBrain` contextual policy at most every 100 ms |
| Capture alignment | `SensorFusion` retains 64 gravity poses; frames carry their original timestamps |
| Navigation | `RouteCoach` and `ProviderTurnCoach`, including session/epoch/sequence, reroute invalidation, independent GPS quality and accepted-step history |
| Common authority | `core/src/c_api.cpp` guidance decide/pending/accepted sequence |
| Optional generative work | Existing bounded ONNX decoder/post-drive summary code; not instantiated as Android live tactical authority |

## 2. Actual signals and limits

Phone sensors supply accelerometer, gyro, optional fresh gravity/linear readings,
GPS position/accuracy, speed and speed accuracy. Bearing requires its quality
check. Mock fixes are rejected. Fusion provides estimated speed, inclination,
acceleration, braking/hard-braking/shock indicators and validity flags. These
indicators do not measure brake/clutch/accelerator pedals. Live gear, RPM,
steering angle, gaze, correct driver actions and safe road speed are unavailable.
Desk gear/RPM are labelled simulations. Live missing speed remains unavailable.

Google Navigation 7.6.1 supplies monotonic receipt time, normalized phase and
maneuver, session/epoch/sequence, step, remaining distance and ETA. Receipt time
is not provider measurement accuracy. No traffic-congestion feed, road-surface
classification, lane position, bend severity or mechanical state is currently
connected. Roundabout exit is UI-only. Places 5.1.1 handles destination search.
Google navigation speech is explicitly SILENT: LearnDrive owns the one voice.
Offline Valhalla/MapLibre provides regional route shape and maneuver metadata.

## 3. Models and temporal contract

YOLOv8n `detector.onnx` is active, letterboxed RGB 320x320, float32 NCHW 0..1;
output `[1,84,2100]`. TwinLiteNet `road.onnx` is retained as a legacy asset but
is not executed in the current Android `Vision` path.

The actual Supercombo file matches its pinned manifest:

- openpilot **v0.8.13**, commit `6ca0269f47d048600335ac5776fc7966d9ca339f`;
- 57,554,657 bytes, SHA-256
  `2365bae967cce21ce68707c30bf2981bb7081ee5c3e6a3dff793e660f23ff622`;
- float32 `input_imgs [1,12,128,256]`, `desire [1,8]`,
  `traffic_convention [1,2]`, `initial_state [1,512]`;
- `outputs [1,6472]`; output offset 5960 supplies the next 512-state input.

Previous/current six-plane YUV images are warped using optical metadata and
gravity-derived pitch/roll. Raw video-range packing and full-range conversion
are implemented. First frame duplicates current image into previous. Source
changes, failures and gaps over 250 ms reset state; nonincreasing time is rejected.
Desire is zero; traffic convention is hardcoded `[0,1]` for India. Upstream
declares 20 Hz, while actual phone cadence varies: temporal road accuracy at
these rates remains unqualified. Gravity cannot establish yaw, mount height or
level ground.

Decoder consumes best-plan mean positions, lane positions/probabilities, edges
and three lead heads; only the current lead is used as a current object. Plan
velocity/acceleration/uncertainty, meta and pose are not consumed. Edge probability
is a local `1/(1+std)` heuristic, not a model probability. A neural lead needs an
independent projected box/closing match before collision-scene use.

No model replacement or fine tuning is warranted for this increment.

## 4. Concrete gaps and defects

1. There is no unified compact ten-second observation memory or general event
   ledger. Different trackers, spatial leads, camera attention and route features
   use different identity namespaces. They must not be conflated by numeric ID.
2. Existing spacing is fragmented: 3.5 s global ordinary audio, 10 s duplicate
   phrase lock, typically 8 s camera attention, 6 s terrain and 8 s navigation.
   There is no unified Balanced 12 s routine policy or explicit bounded DEFER.
3. `ld_guidance_pending` checks decision age, but original capture age is checked
   only for camera actors. Tactical/reflex evidence can age across selection and
   acceptance. Terrain/command support also needs independent revalidation.
4. PCM submission carries an ID without the decision envelope. Generation is
   checked at playback, but original evidence/source/preferences are not checked
   at actual `AudioTrack.play` onset. Queue acceptance is not proof of audibility.
5. Some slope/bend/manual branches select gear or engine-braking wording without
   live vehicle evidence. Explicit shift logic has stronger gear/RPM guards.
   An independent approved-action validator and neutral fallbacks are needed.
6. `MetricEstimator` estimates range from nominal class width and focal length.
   That is not independently measured depth. This provenance/uncertainty is lost
   in `SceneHazard`; fallback reflex TTI must not become trusted measured risk.
7. Cross-origin acceptance can commit `CameraCoach`'s pending actor even when a
   different reflex event caused the accepted phrase. Commit explicit identities.
8. Android session admission currently requires `audio.ready`, and captions/review
   are tied to PCM acceptance. Optional voice must not stop perception/navigation
   or pretend a muted event was spoken. Preferences need immediate revision guards.
9. Replay v2 is strong on provenance and video clocks but lacks structured
   camera/telemetry/navigation replay and a replaceable-policy benchmark.
10. The instructor sheet has 70 cue rows with blank rating fields. Existing event
    labels are provisional engineer review, not qualified instructor agreement.

## 5. Baseline verification and current limits

Executed during audit; raw logs/provenance are in
`outputs/balanced-coaching-audit-20261025/`:

- Existing native CTest binaries: **10/10** passed in 9.16 s, including real
  local Supercombo inference. These binaries were built on 22 October; a fresh
  current-source build is still required for the new implementation.
- Current Java camera/readiness/setup/reference/journal suites passed; current
  road-status **8** and session-review **12** cases passed.
- Python unit discovery: **31/31** passed.
- Host tools: Python 3.11.0, Java/Javac 18.0.1 targeting Java 17, CMake 3.29.2,
  Zig 0.14.1, Ninja. Existing native Release runtime uses local ORT 1.22.1.
- Prebuilt host test output: navigation policy mean 0.06544 microseconds,
  spatial combined mean 1.26447 microseconds, Supercombo warm inference 37.8319 ms.
  These are x64 host checks, not end-to-end or smartphone measurements.
- ADB currently reports **no connected devices**. No new physical, sustained FPS,
  battery, thermal or end-to-end audible-latency result can be claimed.
- No local pretrained coaching-policy model/tokenizer artifact was found. A
  shadow-model benchmark must report this absence, not fabricate model results.

## 6. Implementation architecture and sequence

Extend the current C++ guidance authority; do not create another camera pipeline.
Use bounded typed modules in `ld::coaching` and retain the old proposals as inputs:

1. `ObservationSynchronizer`, `ObjectTracker` adapter and `TemporalMemory` keep
   compact timestamped facts for ten seconds. Historical memory never makes old
   evidence current. `VehicleStateEstimator` exposes supported speed trends,
   never pedal actions or proof that a learner responded correctly.
2. `RouteContext` joins current, quality-gated navigation events to scene state.
   Do not invent unsupported congestion/lane/gear events or persist Google content.
3. A replaceable `DecisionPolicy` emits SILENCE/COACH/WARN/DEFER and approved IDs.
   Default Balanced uses a twelve-second experimental routine interval; Minimal
   and Guided alter optional frequency while sharing every evidence constraint.
4. `InstructionValidator` checks original evidence, source, capability, mechanical
   support, current action/priority and expiry independently of policy. Existing
   research-emergency authorization remains off by default.
5. `EventLedger` and one-slot `InstructionScheduler` suppress an encounter until
   meaningful change, expire deferred work, and record presentation separately
   from actual voice submission. Independent urgent evidence is not hidden by
   routine spacing. Recheck the delivery ticket at playback onset.
6. Android persisted settings default to Balanced + optional voice enabled. A
   preference revision invalidates queued work immediately. Voice off keeps
   perception, map, route context, logical coaching and setup functional. Keep
   one PCM owner and preserve the stable Start/level-reference flow.
7. Add structured replay, synthetic correctness fixtures, annotation validation,
   baseline comparison and optional local shadow-policy interface. Missing models
   or human labels produce explicit unavailable results. No invented agreement.
8. Add capability/performance hooks and bounded degradation without relaxing live
   evidence limits. Do not introduce unbenchmarked acceleration or a live LLM.
9. Run current-source native/Java/Python suites, build Android app/tests, and run
   physical checks only if a device is actually available. Record each stage in
   local commits on `codex/balanced-coaching`, preserving published `master`.

First behavioral milestone: one qualified right-turn event, at most one useful
preparation cue, supported deceleration trend represented as such, then silence
while that same event continues. Fresh urgent evidence remains separately gated.

## 7. Test strategy

Regress timestamp/source/order handling, frame gaps, tracker namespaces/churn,
ten-second memory expiry, route epochs/reroutes/GPS loss, missing telemetry,
measured slowing, event identity, duplicate suppression, ordinary-vs-urgent
spacing, one-slot expiry/replacement, contradictions, mechanical fallbacks,
post-selection capture expiry, preference changes and playback-onset checks.
Test actual C API integration, not just isolated helper classes. Replay all
meaningful combinations deterministically using identical inputs. Preserve old
test cases where behavior is intentionally unchanged; explicitly document changed
Balanced expectations instead of weakening evidence assertions.

Android checks cover preference persistence/fallback, independent voice-off
admission, no rogue test/startup speech while muted, revision cancellation,
captions/review that distinguish visual vs submitted voice, UI touch stability,
navigation ownership and source/lifecycle changes. Supervised/instructor and
sustained-device validation remain release requirements.

## 8. Licensing, provider use and privacy

Bundled Supercombo/upstream and TwinLite notices are MIT; YOLO's bundled notice
is AGPL-3.0. No uniform permissive/commercial clearance is established. The
[Ultralytics licensing page](https://www.ultralytics.com/license) describes its
AGPL and Enterprise paths; proprietary distribution needs a separate assessment.

Google context stays transient and separate from offline OSM data. Keep logos,
terms and provider controls; do not persist raw maneuvers, names, distances or
traffic into new diagnostics, lessons or model datasets. Review is required under
[Navigation policies](https://developers.google.com/maps/documentation/navigation/android-sdk/policies).
No new API requests, cloud TTS, analytics or uploads are needed for the coaching
layer. Existing route generation retains its
[per-destination Navigation billing](https://developers.google.com/maps/documentation/navigation/android-sdk/pricing);
this increment does not change the account or pricing agreement.

Diagnostics remain opt-in, bounded, app-private, without footage, microphone
audio or coordinates. New event counters/reason IDs must not expose provider
content. No external source/data upload is part of implementation.
