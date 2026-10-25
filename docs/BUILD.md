# Build, run and desk mode

## Android

The current Android source is **0.4.8**, with a stable Start action and automatic
level calibration after confirming parked, level ground. See the
[Start repair](reviews/2026-10-22-start-control-repair.md). The charcoal dashboard,
separate preparation/destination pages and both landscape orientations are
recorded in the [UI review](reviews/2026-10-22-ui-redesign.md). The preceding
encounter/cooldown repairs, current camera evidence status and temporary parked
session review are recorded in the
[coaching review](reviews/2026-10-22-coach-reliability-increment.md).
These are engineering checks; road recognition, learner usefulness and sustained
device qualification remain open. Heat work is deferred at the user's request.

The app opens to parked setup with the real road camera, a map in the same
dashboard and collapsed lab controls. Check sound, optionally confirm a level
reference, then explicitly confirm the mount and start a supervised session.
Screen-video testing defaults **OFF** on every launch. See [the setup flow](SETUP_FLOW.md).

Use Android Studio with **JDK 17**, Android SDK **36**, NDK **28.2.13676358** and
CMake **3.22.1**. Open the `android` directory and let Android Studio create
`local.properties` with the local SDK path. The checked-in toolchain is:

| Component | Pin |
| --- | --- |
| Android Gradle Plugin | 8.13.2 |
| Gradle wrapper | 8.13 |
| Kotlin | 2.2.10 |
| Java source/target | 17 |
| Android compile / target / minimum API | 36 / 36 / 28 |
| Google Navigation / Places | 7.6.1 / 5.1.1 |
| NIO core-library desugaring | 2.0.3 |
| Datastore core | 1.2.1 |

AGP 8.13.2 supplies the newer R8 needed to rewrite the existing Valhalla
dependency's Kotlin 2.3 metadata on the NIO desugaring path; the app compiler
remains Kotlin 2.2.10. Keep the matching pins while qualifying this candidate.
The wrapper checks its distribution against SHA-256
`20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78`.

To build with the included Gradle wrapper:

```powershell
cd D:\LearnDrive_mobile\android
.\gradlew.bat :app:assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n ai.learndrive.mobile/.MainActivity
```

On Linux/macOS, use `./gradlew :app:assembleDebug`. The wrapper downloads Gradle
8.13 on the first build. Use the checked-in wrapper and checksum rather than an
unrelated system Gradle. To compile instrumentation tests as well, run
`:app:assembleDebug :app:assembleDebugAndroidTest`. Build success still needs
final APK asset, signature, permission and native-library alignment checks.

Build downloads are permitted during development. Google map, destination
search and routing/location requests use the network. Camera frames, local
detector/road/Supercombo inference, motion rules and the bundled PCM voice remain
on the device; this increment adds no cloud camera inference, image upload or
runtime model downloader. Ordinary diagnostic exports exclude raw GPS
coordinates and API key values. Imported regional maps/search/routing remain
local when the offline provider is selected.

For Google configuration, follow [GOOGLE_NAVIGATION_SETUP.md](GOOGLE_NAVIGATION_SETUP.md).
Enter the restricted Android key in ignored `android/secrets.properties`; never
paste its value into logs or commits. The build injects it into manifest metadata.
Without a configured key the Google controls remain unavailable and the offline
provider remains usable. Navigation already bundles Google's map SDK; the build
excludes a separate `play-services-maps` copy.

The active camera backbone is now the pinned Comma.ai Supercombo model. Before
a fresh native Android build, run `python tools/fetch_supercombo.py`,
`python tools/cache_android_runtime.py`, and `python tools/prepare_android_ort.py`
from the project root. The last step extracts headers and each ABI's native
library from the same ORT AAR that Gradle packages; it does not introduce a
second ORT version. See [the current integration record](INTEGRATION.md).

If Java networking stalls while fetching ONNX Runtime, `python tools/cache_android_runtime.py` can prefetch the pinned official Maven Central POM/AAR into an optional workspace mirror. Gradle uses that mirror for the ONNX Runtime group only. Other dependencies continue to use their normal repositories.

Allow camera access and raise media volume. Ordinary road coaching remains
paused in setup; **Check sound** verifies the voice before starting a supervised
session. Basic camera attention can work without a level reference, GPS or an
optical TTI estimate, but needs its own sustained relevant visual evidence.
Urgent motion-based cues have separate evidence gates. The HUD shows measured
speed when available and leaves gear unknown. The lab **Manual vehicle · gear
guidance** switch does not supply a measured gear or qualify a numbered shift
instruction; vehicle-specific gear/RPM evidence is not connected.

Supercombo, the object detector, road model, their notices/configuration and
**55 PCM WAV clips** are required bundled assets. No phone TTS installation is
required. Google voice is silent; local camera/map/terrain cues share one speech
arbiter. Google feed cues currently cover generic left/right preparation with
independent GPS and timing gates; unsupported maneuvers remain visual, and
Google-derived bend voice is not qualified. See [driving guidance](DRIVING_GUIDANCE.md)
and the [candidate review](reviews/2026-10-19-google-navigation.md).

### Lab inputs and legacy testing controls

The following are explicit lab sources, not ordinary road defaults.
**Camera pointed at a road video (test)** starts OFF and must be enabled for that
launch. Set **Video driving speed (test)** to a known recording speed; the display
labels it **video test setting**. The reviewing phone's GPS is not the recorded
vehicle's position or speed, and route coaching is paused for test input.

The lab **Use normalized phone slope** preference defaults ON; it describes the
reviewing phone's tilt after normalization, not the road grade in a filmed
video. Turning it OFF reveals the simulated **Video slope** selector. Manual
video speed and phone-slope preferences are saved; simulated slope resets to
level. These controls do not replace the mounted-road level-reference process.

Short slope cues use angle bands starting at 2°, 5°, 8° and 12°, with a 1.5-second sustained reading and six-second guidance spacing. Steeper bands can speak without constant repetition. The simulated slope selector offers level and ±3°, ±6°, ±10°, ±15° tests. Keep **Slope guidance** ON. Manual gear wording requires **Manual vehicle · gear guidance** ON. See the complete ranges and low-speed/unavailable behavior in [Camera driving guidance](DRIVING_GUIDANCE.md).

For a mounted vehicle, keep video test OFF and follow the parked setup flow.
Android requests location and uses measured road telemetry directly. Missing
speed stays unavailable; basic camera attention can continue. Manual speeds are
available only in explicit video/Desk tests. An optional inclination reference
requires confirmed level ground and five seconds of stillness. iOS retains its
existing GPS/manual controls and needs a separate parity pass.

Select **MP4** and a local video for decoded-camera input with an explicit manual
recording-speed setting; original calibration and vehicle motion are unavailable.
**Desk** sliders simulate slope, hazard and engine inputs. The build fails if a
required bundled model/voice/config asset is missing. See [model contracts](../models/README.md)
for regenerating assets and notices. Camera timestamp checks require
elapsed-realtime sensor timestamps. An incompatible camera clock needs a
verified clock-translation adapter.

The Android app locks landscape orientation. CameraX selects preview resolution;
detector/road preprocessing uses 320×320 inputs and Supercombo uses its separate
model contract. YUV planes remain owned by ImageProxy/MediaCodec until
preprocessing completes; each ImageProxy closes in `finally`. Live analysis uses
`STRATEGY_KEEP_ONLY_LATEST`. MP4 decoding paces by presentation timestamp and
drops frames more than 100 ms late.

The scheduler permits at most 30 analysis starts/second at normal temperature,
25 at light thermal status and 20 at moderate status. These are scheduling caps,
not verified delivered inference FPS. Severe heat uses a 1.5-second interval;
critical heat pauses visibly. Sustained combined map/model/audio smoothness and
thermal behavior remain unqualified. **Restart detection** rebinds Camera and
reloads models after a failure. Delayed display results retain capture times;
the separate 350 ms hazard-age limit is unchanged.

## iOS

Requires macOS, Xcode 15 or newer, the iOS 16 SDK or newer and [XcodeGen](https://github.com/yonaskolb/XcodeGen). Windows cannot build or validate the iOS application.

```sh
cd ios
xcodegen generate
xcodebuild -project LearnDrive.xcodeproj -scheme LearnDrive \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath DerivedData CODE_SIGNING_ALLOWED=NO build
```

After `xcodegen generate`, run `pod install` in `ios` and open the generated
`LearnDrive.xcworkspace`. The Podfile pins `onnxruntime-c 1.22.0`; the XcodeGen
spec bundles Supercombo and its license from the shared Android asset directory.
Separate Core ML model exports are no longer required for the active iOS path.
Select your development team and run on a device. macOS/iOS validation remains
outstanding in this Windows environment.

The SwiftUI app offers the same desk controls, file importers, calibration and PCM preemption test. Warmup uses the installed platform voice, copies its audio buffers once, then schedules those cached buffers using AVAudioEngine. Camera capture uses full-range NV12 and a one-pending-buffer queue. AVAssetReader applies the video's preferred transform. Backgrounding stops sources, motion and speech. MP4 playback is selected again after resuming.

## Shared C++ tests and desktop simulator

Requires CMake 3.22+ and a C++17 compiler with its complete development libraries. On Windows, run in an x64 Visual Studio Developer PowerShell with the C++ workload installed:

Alternatively, a portable Zig toolchain can compile and run the same tests without Visual Studio. Install `ziglang==0.14.1` into `.tools/zig` using `python -m pip install --target .tools/zig ziglang==0.14.1`, then run `powershell -NoProfile -File tools/run_core_tests.ps1`. The script keeps build and compiler caches in the workspace and runs both the behavioral suite and the CSV scenario.

```powershell
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
.\build\host\learndrive_desk.exe scenarios\desk.csv
```

On Linux/macOS:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure
./build/host/learndrive_desk scenarios/desk.csv
```

`learndrive_desk` exercises the production decision engine using CSV telemetry. Its input is simulated and it prints decisions rather than pretending to run camera inference or play phone audio. The mobile apps accept the same script format.

Camera scheduling/display-health tests require only a JDK 17 or newer and use the same Java policy as the Android app. Run `powershell -NoProfile -File tools/run_camera_health_tests.ps1`. No Android device or external test dependency is needed for those tests.

The native dual-brain fixture/tests are included in desktop builds. Android 0.2.0 enables the bounded reflex and deterministic contextual policy through JNI; no SLM asset is needed. Optional ORT and tokenizers-cpp flags, live model assets and provider setup are documented in [DUAL_BRAIN.md](DUAL_BRAIN.md).
