# Spatial driving integration and acceptance record

This is the requirement-by-requirement record for the supplied integration prompt.
Software implementation and physical-device acceptance are separate evidence.

The subsequent 0.3.2 engineering increment adds a shared Android/replay guidance
coordinator, source isolation, recording provenance and default-off experimental
emergency commands. See [EXECUTION.md](EXECUTION.md) for its artifact/checks and
[GUIDANCE_PIPELINE.md](GUIDANCE_PIPELINE.md) for current limitations. The 0.3.1
results below remain the historical false-alert correction baseline.

The Kochi/Munnar replay subsequently exposed false model-led collision alerts.
The 0.3.1 correction requires a fresh, spatially matching vehicle detection
with independent optical closing evidence before a Supercombo lead can enter
the collision-policy scene. Raw neural estimates remain diagnostic data.
Urgency downgrades no longer bypass the emergency repeat interval. This reduces
false alerts at the cost of delaying or missing alerts when the detector or
optical tracker cannot confirm a real threat. The iOS shell currently lacks
the independent semantic producer, so neural lead alerts there are inhibited
by this gate until that producer is integrated and tested. No iOS safety
acceptance is claimed.

The corrected Android artifact is `build/LearnDrive-0.3.1-debug.apk` (version
code 14), SHA-256
`5096de70d99d258ae62f7523f157b297ea2d12c3f81806b2670bf4864b2011ff`.
Its ARM64/x86-64 build, signature and packaged assets verify. All eight native
test targets pass after the correction. See [KOCHI_VIDEO_REVIEW.md](KOCHI_VIDEO_REVIEW.md)
for exact before/after alert counts, replay artifacts and remaining missed cues.

| Requirement | Implementation | Verification |
| --- | --- | --- |
| Actual Comma.ai `supercombo.onnx` | Bundled 57,554,657-byte upstream v0.8.13 model, commit `6ca0269f47d048600335ac5776fc7966d9ca339f`; SHA-256 `2365bae967cce21ce68707c30bf2981bb7081ee5c3e6a3dff793e660f23ff622` | `tools/fetch_supercombo.py`, `tools/verify_supercombo.py`; native `supercombo_smoke` |
| Extract vision, no vehicle-control framework | Independent C++17 YUV warp, temporal input, recurrent state, spatial decoder and coordinate transforms | No openpilot runtime, CAN, OBD, panda or control-daemon dependency |
| Gravity horizon calibration actually applied | `supercomboWarp` generates source-from-canonical-model homography; `packSupercombo` samples every incoming YUV frame through it | Identity, inverse, coordinate and channel-layout tests |
| Capture-time sensor alignment | Fixed 64-sample gravity/gyro pose history selects the pose nearest the original camera capture time | Delayed-camera/newer-IMU and expired-pose regression checks |
| Pinhole distance, five-frame filtering and relative speed | Vehicle width priors, `KalmanFilter1D`, fixed five-observation regression, signed relative velocity, positive closing TTI | Geometry suite; out-of-order/unknown-class rejection |
| 100 Hz safety reflex | Bounded 64-object `noexcept` scan; current IMU acceleration, optical/model closing velocity and capture-age compensation | Boundary, stale-data, escalation and allocation-count tests |
| Driver suppression and critical escalation | Coasting above -1.8; moderate from -3.5 through -1.8; hard below -3.5 m/s². Active braking suppresses routine advice; TTI below 1.2 s emits `BRAKE_HARDER` | `core/tests/test_reflex_suppression.cpp` |
| Deterministic 10 Hz tactical policy | Threat scores use severity × ego speed / (TTI × lateral separation); explicit finite denominator floors; bus/pothole conflict holds lane/distance; rear threats ignored | Real arbiter + policy tests, including score-dependent selection |
| Global audio timing | Native global 3.5 s gate across camera/terrain/tactical guidance; 10 s duplicate lock; only accepted playback commits timing | C ABI and arbitration tests |
| Level 0 preemption | Cached `BRAKE_HARDER` and `EMERGENCY_STOP`; synchronous native JNI callback pauses, flushes and stops Android AudioTrack | Native preemption contract and Android compilation; physical audio timing outstanding |
| SLM demotion | Driving coordinator does not instantiate a model. Explicit `summarizeAfterDrive` requires stopped drive and returns text through a future with no speech callback | Model-spy test verifies zero driving calls and no summary-to-audio path |
| XNNPACK/CPU baseline | Shared native ORT runtime, two threads, graph optimization, disabled spinning; Android worker/pool inherit two highest-frequency allowed cores when sysfs/affinity permits | Provider report records actual selection, fallback reason, affinity and warm latency |
| QNN conditional profiling | Probe supplied HTP backend, create session without hidden CPU partitions, run warm benchmark, require numerical agreement and at least 10% measured improvement | Implemented; QNN backend/device not present here, so HTP cannot be validated |
| Cross-platform runtime | Android JNI and iOS Objective-C++ both use the same native Supercombo implementation and offline model | Android build available here; iOS needs macOS/Xcode/CocoaPods |
| ARM64 <1 ms combined decision latency | Native 10,000-cycle diagnostic logs p50/p95/p99/max/mean on the phone | Pending physical ARM64 device; host timing is not substituted |
| 30 FPS camera and >30 minutes above 20 FPS without throttling | Latest-frame capture; normal analysis target 30 Hz; per-second FPS/inference/thermal logs; `tools/profile_android.py` enforces duration/FPS/thermal/decision gates | Pending physical device. Severe/critical heat protections remain active |

## Exact Supercombo contract

Inputs are float32 `input_imgs [1,12,128,256]`, `desire [1,8]`,
`traffic_convention [1,2]`, and `initial_state [1,512]`. Output is float32
`outputs [1,6472]`. The final 512 outputs become the next recurrent input.
The image channels are previous/current `Y00,Y10,Y01,Y11,U,V`, using 0–255
video-range values, not normalized RGB. Full-range phone frames are converted.
Long frame gaps and capture-clock changes reset temporal state.

The decoder selects the highest-probability trajectory mixture; parses four
3D lane lines, two road edges, and three lead-selection horizons. The +2/+4 s
lead heads are not counted as independent current vehicles. Lead velocity is
absolute and must have ego speed subtracted before computing closing TTI.
This is checked against the pinned upstream `driving.h`, `driving.cc`,
`loadyuv.cl`, `model.py`, and `modeldata.h` retained in `models/comma-v0.8.13`.
The upstream MIT notice is bundled as `COMMA-LICENSE.txt`.

Android uses Supercombo for the primary spatial scene. YOLO supplies road-user
class labels at 4 Hz; TwinLiteNet is retained as a legacy asset but is no longer
run in the active camera path. iOS now uses shared ORT instead of requiring
separate Core ML exports. Traffic convention defaults to left-hand traffic for
the current India testing setup. Source camera optical metadata and a fixed,
forward-facing mount are assumptions; video replay uses the selected camera's
optics as an approximation unless recorded intrinsics are supplied.

Supercombo does not classify potholes, bus types or rear tailgaters. Bus/person
labels come from the semantic detector; pothole/rear-threat policy inputs can
be injected by tests or another producer. No unobserved hazard is fabricated.
Monocular class-width and learned model distances remain estimates. A single
gravity vector cannot establish yaw or distinguish road grade from mount tilt;
level-ground normalization and the forward-mount assumption remain necessary.

The pinned upstream runtime declares a 20 Hz model frequency. The current
30 Hz camera scheduling target and variable phone inference rates require
temporal/road-accuracy validation; passing a tensor-contract test does not
establish that behavior. Windows CPU smoke runs measured about 19–157 ms warm
inference under different host workloads. These measurements do not establish
sustained throughput and are not ARM64/XNNPACK device benchmarks.

## Completed verification and Android artifact

The 2026-10-15 Android debug build compiles for ARM64 and x86-64 and is available
at `build/LearnDrive-0.3.0-debug.apk` (134,541,185 bytes). Its SHA-256 is
`eb6fede39955926e6a2286d226b3693c2747bbaebd864c642a9d4d515f405205`.
Android `apksigner verify` passes. This is a debug-signed build, not a store release.

The APK's models, manifests, license notices and all 43 non-silent PCM clips
match workspace assets. The packaged ORT loadable ELF sections match the pinned
1.22.0 AAR for both ABIs; debug stripping is allowed without weakening runtime
content verification. Native three-frame YUV input packing matches the independent
Python reference exactly, and recurrent model outputs agree within
`rtol=1e-4, atol=1e-3`.

All eight CTest targets pass. Native regressions include 118 core tests, 25 dual-brain cases, real-model
execution, spatial/zero-allocation contracts, the multi-hazard arbiter, reflex
suppression (including exact acceleration/TTI boundaries) and metric geometry.
The bus/pothole arbitration test includes valid incline telemetry.
Eight Java camera-health cases also pass.
The full 64-hazard reflex plus tactical loop measured approximately 0.56 µs mean
on the Windows host; physical ARM64 latency acceptance remains outstanding.

## Reproduce the checks

```powershell
python tools/fetch_supercombo.py
python tools/cache_android_runtime.py
python tools/prepare_android_ort.py
python tools/verify_supercombo.py
python tools/verify_android_assets.py
cd android
.\gradlew.bat :app:assembleDebug
```

For CMake on Windows, the repository's portable Zig compiler can be used with
`-G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/zig-toolchain.cmake` and the Android SDK's
Ninja executable via `CMAKE_MAKE_PROGRAM`. Set `LEARNDRIVE_WITH_SPATIAL_ORT=ON`
and `ONNXRUNTIME_ROOT` to a matching native ORT SDK to include real-model tests.
Put its `onnxruntime.dll` beside host executables, then run `ctest`.
`python tools/verify_supercombo.py --native-prefix build/native-spatial/supercombo-smoke`
compares native temporal inputs and recurrent outputs against independent Python ORT.

For iOS, run `xcodegen generate`, `pod install`, and build the generated
`LearnDrive.xcworkspace` on macOS. The Podfile pins `onnxruntime-c 1.22.0`.
The XcodeGen spec includes the shared model and license as resources.

With a mounted, authorized ARM64 Android phone and the APK installed, run
`python tools/profile_android.py --seconds 1801`. It writes measured evidence
to `build/device-profile.json` and fails if acceptance criteria are unmet.
This environment currently has no connected ADB device, so that evidence and
physical-road/audio/iOS validation cannot be claimed.
