# Build, run and desk mode

## Android

Use Android Studio with JDK 17 or newer, Android SDK 35, NDK 28.2.13676358 and CMake 3.22.1. Open the `android` directory and sync. Let Android Studio create `local.properties` with your SDK path. Gradle 8.9 is the pinned build tool.

To build with the included Gradle wrapper:

```powershell
cd D:\LearnDrive_mobile\android
.\gradlew.bat :app:assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n ai.learndrive.mobile/.MainActivity
```

On Linux/macOS, use `./gradlew :app:assembleDebug`. The wrapper downloads Gradle 8.9 on the first build. To regenerate it using an installed Gradle distribution:

```powershell
gradle wrapper --gradle-version 8.9 --distribution-type bin
.\gradlew.bat :app:assembleDebug
```

Build downloads are permitted during development. Runtime processing is local. No cloud inference, telemetry upload or model downloader is implemented.

If Java networking stalls while fetching ONNX Runtime, `python tools/cache_android_runtime.py` can prefetch the pinned official Maven Central POM/AAR into an optional workspace mirror. Gradle uses that mirror for the ONNX Runtime group only. Other dependencies continue to use their normal repositories.

The Android application opens in **Camera** mode. Allow camera access and raise media volume. It speaks when the camera binds and voice output is ready, then gives guidance from the first relevant road-user detection. No calibration, GPS fix or TTI estimate is required for basic scene guidance. Image-only obstacles produce gap/stop-preparation cautions; low-speed routine speech does not request slowing. Bend cues run from sustained road geometry or paired lane markings, with a yellow trace and Road status. Enable **Manual vehicle · gear guidance** for conditional advice on obstacles, bends and slopes. Exact shift timing needs live gear/RPM; the APK has no vehicle connector. Both pretrained models and 35 spoken WAVs are bundled; no phone TTS installation is required. **Test voice** checks playback. **Desk** sliders simulate slope, hazard and engine inputs. See [Camera driving guidance](DRIVING_GUIDANCE.md).

**Camera pointed at a road video (test)** defaults to ON for the current testing setup. Set **Video / fallback driving speed** to the video's driving speed. In this mode, stationary-phone GPS readings are ignored and the display says **video test setting**. **Use normalized phone slope** defaults to ON: point the camera forward in its intended mount, tap **Normalize phone plane · level ground · hold still 5 s**, and wait for **Phone plane normalized**. The display reports measured degrees and percent grade relative to that level reference. In a video test this describes your phone's tilt; a rigid vehicle mount is required to interpret it as vehicle grade. Turn phone slope OFF to reveal the explicit simulated **Video slope** selector. Speed, mode and phone-slope preferences are saved; simulated slope resets to level.

Short slope cues use angle bands starting at 2°, 5°, 8° and 12°, with a 1.5-second sustained reading and six-second guidance spacing. Steeper bands can speak without constant repetition. The simulated slope selector offers level and ±3°, ±6°, ±10°, ±15° tests. Keep **Slope guidance** ON. Manual gear wording requires **Manual vehicle · gear guidance** ON. See the complete ranges and low-speed/unavailable behavior in [Camera driving guidance](DRIVING_GUIDANCE.md).

For a mounted vehicle, turn video test OFF, mount the phone facing forward and calibrate while parked on **level ground** for five seconds. **Require GPS speed** defaults to OFF. Without a GPS fix, advice uses the manual fallback, initially 30 km/h. Fresh GPS/IMU speed takes priority, including zero. Requiring GPS hides the fallback control and blocks the speed-based tactical path until speed is valid; basic camera and bend guidance continue. Tap **Enable GPS speed (optional)** for location permission. iOS retains its shared GPS/manual selection controls.

Select **MP4** and a local video for decoded-camera input with simulated telemetry. The build fails if any required offline asset is missing. See [model contracts](../models/README.md) for regenerating the bundled assets and license notices. Camera timestamp checks require elapsed-realtime sensor timestamps. A device with an incompatible camera clock reports a visible error and needs a verified clock translation adapter.

The Android app locks landscape orientation. Preview resolution is selected by CameraX; inference inputs are fixed 320x320. YUV planes remain owned by ImageProxy/MediaCodec until preprocessing completes. Each ImageProxy is closed in `finally`, including inference failures. Live analysis uses `STRATEGY_KEEP_ONLY_LATEST`; MP4 decoding paces by presentation timestamp and drops frames more than 100 ms late. Camera scheduling caps analysis at four FPS normally and reduces workload with thermal status. Severe heat uses a 1.5-second interval; critical heat pauses with visible status. **Restart detection** rebinds Camera and reloads models after a failure. Delayed display results retain their capture timestamps, so the 350 ms hazard limit is unchanged.

## iOS

Requires macOS, Xcode 15 or newer, the iOS 16 SDK or newer and [XcodeGen](https://github.com/yonaskolb/XcodeGen). Windows cannot build or validate the iOS application.

```sh
cd ios
xcodegen generate
xcodebuild -project LearnDrive.xcodeproj -scheme LearnDrive \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath DerivedData CODE_SIGNING_ALLOWED=NO build
```

For a phone, open the generated Xcode project, select your development team and run on the device. Add `detector.mlmodelc` and `road.mlmodelc` to `ios/LearnDrive/` before generating the project, or add their compatible `.mlpackage` source equivalents as app resources in Xcode. The bundled model lookup expects the compiled resources named `detector` and `road`.

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
