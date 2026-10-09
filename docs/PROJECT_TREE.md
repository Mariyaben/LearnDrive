# Project source structure

Generated build outputs, dependency caches and local SDK configuration are excluded from this source tree. Android's exported models, warning audio and license notices are bundled app resources.

```text
LearnDrive_mobile/
├── README.md                          Architecture comparisons and constraints
├── .gitignore
├── CMakeLists.txt                     Shared core, host tests, Android JNI
├── core/
│   ├── include/learndrive/
│   │   ├── types.hpp                  Sensor/telemetry/hazard types
│   │   ├── fusion.hpp                 Phone-plane normalization, inclination and IMU/GPS filter
│   │   ├── decision.hpp               Camera/terrain/gear policy and road geometry
│   │   ├── vision.hpp                 YUV, decoder, segmentation, flow, TTI
│   │   ├── latest.hpp                 One-pending-frame ownership queue
│   │   └── c_api.h                    Exception-contained platform ABI
│   └── src/
│       ├── fusion.cpp
│       ├── decision.cpp
│       ├── vision.cpp
│       └── c_api.cpp
├── android/
│   ├── settings.gradle.kts
│   ├── build.gradle.kts
│   ├── gradle.properties
│   ├── gradlew
│   ├── gradlew.bat
│   ├── gradle/wrapper/
│   │   ├── gradle-wrapper.jar
│   │   └── gradle-wrapper.properties
│   └── app/
│       ├── build.gradle.kts
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── assets/               Bundled ONNX models, PCM WAVs, hashes and licenses
│           ├── cpp/jni.cpp            Direct ByteBuffer camera-plane bindings
│           └── java/ai/learndrive/mobile/
│               ├── MainActivity.kt    Desk UI, lifecycle and rules worker
│               ├── NativeCore.kt      Shared-core JNI facade
│               ├── Sensors.kt         100 Hz IMU / GPS clock adapter
│               ├── Vision.kt          ONNX model contract and inference
│               ├── CameraVisionHealth.java Thermal scheduling and detection display/status
│               ├── DetectionOverlay.kt Road-user boxes and fitted road path
│               ├── Mp4Source.kt       MediaCodec decoded YUV input
│               ├── AudioWorker.kt     Offline speech cache and AudioTrack
│               └── DeskTelemetry.kt   CSV trajectory injection
├── ios/
│   ├── project.yml                    XcodeGen project specification
│   └── LearnDrive/
│       ├── App.swift                  SwiftUI desk UI and lifecycle
│       ├── Telemetry.swift            CoreMotion / CLLocation / CSV
│       ├── DecisionCoordinator.swift  Dedicated serial rules worker
│       ├── Vision.swift               Core ML, camera queue and MP4 reader
│       ├── AudioWorker.swift          Speech buffer cache and AVAudioEngine
│       ├── LDCore.h
│       ├── LDCore.mm                  Objective-C++ borrowed-plane bridge
│       └── LearnDrive-Bridging-Header.h
├── desktop/main.cpp                  Executable decision simulation
├── tests/core_tests.cpp               Shared-core behavioral tests
├── tests/CameraVisionHealthTest.java   Thermal recovery, delayed results and camera health
├── scenarios/desk.csv                 Mobile and desktop scenario
├── models/README.md                   Weights, normalization, I/O and exports
├── tools/
│   ├── check_models.py                Optional legacy TFLite structure verifier
│   ├── export_android_models.py       Pretrained weight exports and provenance
│   ├── cache_android_runtime.py       Optional build-time Maven prefetch
│   ├── render_android_prompts.ps1     Render bundled offline PCM warnings
│   ├── verify_android_assets.py       Model, voice and APK verification
│   ├── run_core_tests.ps1              Optional portable Zig test build
│   └── run_camera_health_tests.ps1     JDK-only camera health tests
└── docs/
    ├── BUILD.md
    ├── TESTING.md
    ├── DRIVING_GUIDANCE.md           Camera bends/slopes and gear data requirements
    └── PROJECT_TREE.md
```

XcodeGen generates `ios/LearnDrive/Info.plist` and `ios/LearnDrive.xcodeproj`. Android Studio generates `android/local.properties`. Android model weights and warning audio are included in `android/app/src/main/assets/`; Core ML compiled resources require separate exports. See BUILD.md for details.
