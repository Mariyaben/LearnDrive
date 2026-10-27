# LearnDrive 0.5.0 — Balanced Coaching Engine implementation

2026-10-26. Work branch: `codex/balanced-coaching`, based on published `master` at `34a63c3`. The initial audit was committed before application edits. This is an Android engineering prototype; the existing dashboard remains in place.

## A. What was implemented

| Deliverable | Actual implementation |
| --- | --- |
| Architecture audit | `docs/architecture_audit.md`: actual tensors, recurrent state, models, JNI, sensor provenance, navigation, concurrency, tests and licensing gaps |
| Temporal scene context | `core/include/learndrive/coaching/TemporalSupport.hpp`, corresponding `.cpp`, and `CoachingEngine` short memory: bounded ten-second scalar snapshots and position histories, independent capture clocks, missing-data/jump handling |
| Tracking integration | Reuses existing optical and CameraCoach encounter IDs; separates attention/optical/spatial namespaces; retains compact histories rather than adding a competing image tracker |
| Navigation context | Existing Google/offline producers feed current maneuver/event identity, route generation, original receipt and independent GPS freshness into `WorldState`; phases share one maneuver ledger entry |
| Decision policy | Replaceable `DecisionPolicy`, deterministic `BalancedPolicy`, SILENCE/COACH/WARN/DEFER, imminent-turn deferral and conservative use of measured decreasing speed |
| Event management | Bounded 64-entry ledger, accepted phrase/evidence history, experimental 12-second Balanced pacing, one deferred slot, stale cancellation, stable encounter suppression and material escalation |
| Independent validation | Approved prompt/origin/identity/evidence checks at selection, acceptance and onset; original camera freshness never renewed by a decision tick; exact settings/source tickets; each onset ticket usable once |
| Mechanical limits | Neutral prerecorded fallbacks; precise shifts require independent current gear, RPM, load and configured bands. Phone speed/pitch cannot supply those measurements. Default emergency-command authorization remains off |
| Android settings/audio | `CoachingPreferences`, `NativeCore`, JNI, `AudioWorker`, setup admission and review: persisted Balanced/voice-on defaults, independent voice toggle, visual logical cues without PCM, scoped WARN interruption and serialized cancellation/writes |
| Capability behavior | `VisionCapabilityPolicy`, `Vision`, `SpatialReadiness`: measured slow/failing spatial stages select semantic Basic; mount guards and evidence ages remain; Advanced explicitly unavailable |
| Replay and annotation | Shared native structured bridge, 17 synthetic fixtures, video opt-in for the new policy, strict local shadow adapter, real instructor schema and blank template |
| Diagnostics | Existing bounded opt-in journal adds scalar coaching/capability fields; logical acceptance, PCM submission and playback onset remain separate; Google-derived navigation details omitted from retained Google-backed decision traces |

The current Start flow, automatic reference after parked/level confirmation, dark dashboard, both landscape rotations, destination search and navigation providers are retained. No model weights or recorded voice assets were changed. No paid service, cloud TTS, analytics or footage upload was added.

## B. Architecture

CameraX keeps the original capture timestamp. Existing YOLO labels publish before Supercombo finishes. Existing trackers, road estimates, fresh GPS/IMU estimates and normalized navigation provide bounded `WorldState` observations. Memory and the ledger supply recent context. The policy selects an approved candidate or silence/defer; independent validation and scheduling determine eligibility. Android revalidates before logical acceptance and again at the PCM onset boundary. Captions and parked review can continue with optional voice off.

Native inference stays outside the core monitor. Delivery/onset/settings use core then audio monitor order. PCM nonblocking writes serialize with cancellation so an old worker cannot write after a flush. Camera source restart cancels old PCM as well as native tickets. Source and settings revisions cannot recover older work.

See [engine design](ENGINE.md) and [Android integration](android-integration.md). `ld_coaching_configure` explicitly activates the new path; legacy clients retain their previous ABI and behavior. iOS has not been migrated to these Android settings or qualified in this increment.

## C. Models used

- **YOLOv8n**: existing local 320-pixel semantic detector/class labels. No accurate metric depth is inferred from class width alone. Commercial redistribution/licensing remains unresolved for this app's intended distribution.
- **Supercombo**: existing openpilot v0.8.13 artifact at commit `6ca0269f47d048600335ac5776fc7966d9ca339f`, SHA256 `2365bae967cce21ce68707c30bf2981bb7081ee5c3e6a3dff793e660f23ff622`. Existing previous/current YUV input, 512-element recurrent state and decoded road/lead estimates are preserved. No fine-tuning or replacement. Phone frame cadence, camera warp assumptions and road usefulness still require qualification.
- **TwinLiteNet**: existing bundled legacy asset; not the active current camera backbone.
- **Reasoning/policy model**: none added to production. The deterministic policy works offline. Qwen3-0.6B and 1.7B were investigated through publisher sources, but no weights were downloaded or benchmarked. The shadow report says `unavailable`; fake adapter tests are plumbing tests only.
- **Voice**: the same 55 local approved PCM WAV clips. Routine wording prefers short phrases, but some necessary approved terrain/obstacle clips exceed the experimental four-second preference; no duration or semantic substitution is concealed.

See [audit](../architecture_audit.md) and [external dependency review](external-dependencies.md) for exact contracts, sources and Google/model licensing limits.

## D. Tests and evidence

Executed on this Windows host with Python 3.11.0, Java 18.0.1 targeting Java 17, Zig 0.14.1, CMake 3.29.2 and host ORT 1.22.1. Android retains its pinned ORT 1.22.0.

| Check | Result |
| --- | --- |
| Current-source native CMake build and CTest | 13/13 groups pass, including eight temporal groups, 21 coaching groups, bounded allocation benchmark, existing regression groups and actual bundled Supercombo smoke execution |
| Python discovery | 67/67 pass, no skips; includes native structured/video integration, shadow mode-downgrade rejection and real-video evidence-inspector regression cases |
| Java host suites | All nine classes pass through the camera-health and road-coaching scripts, including updated mount/Basic guards, seven interruption-policy cases and 13 parked-review cases |
| Structured replay | 17 fixtures through both shared Balanced engine and explicitly limited legacy candidate/audio gate; all expected invariants pass |
| Current Balanced video replay | 28 existing development clips, 760.5 seconds, 22,818 analyzed frames and 59 logical cues. All28 pass the original-evidence, revision/acceptance, routine pacing, prohibited-action and simulated-onset inspector. No instructor quality score |
| Annotation template | Valid pending template; instructor labels unavailable |
| Local pretrained shadow | Correctly unavailable; no model weights or human quality results fabricated |
| Android build / final APK | Both debug application and instrumentation APKs assemble. Application version 0.5.0/code 29, models/55 PCM clips/notices/native runtime matching, signature and ZIP alignment pass; all 18 native libraries have ELF load alignment at least 16 KiB |
| Physical runtime | Not run for 0.5.0: ADB lists no devices. Compiled instrumentation is not device execution |

Commands from repository root:

```powershell
D:\bin\cmake.exe --build build/native-spatial -j2
D:\bin\ctest.exe --test-dir build/native-spatial --output-on-failure
C:\Python311\python.exe -m unittest discover -s tests -p 'test_*.py' -v
powershell -NoProfile -File tools/run_camera_health_tests.ps1
powershell -NoProfile -File tools/run_road_coaching_tests.ps1
$fixturePaths=Get-ChildItem scenarios/balanced-coaching/*.json | ForEach-Object FullName
C:\Python311\python.exe tools/replay_coaching.py @fixturePaths --output build/balanced-coaching/structured-replay
C:\Python311\python.exe tools/benchmark_coaching_shadow.py @fixturePaths --output build/balanced-coaching/shadow-benchmark.json
C:\Python311\python.exe tools/coaching_contract.py docs/coaching/instructor-annotation-template.json
```

Replays are software evidence. For a sustained synthetic person encounter, the narrow legacy audio-gate baseline accepted three cues and Balanced accepted one. A new synthetic person remained a distinct event; both were accepted across the test interval. One route maneuver accepted one preparation cue across its distance phases; rerouting introduced a new event. Stale queued work, missing sensors, poor GPS, contradictory route direction and unsupported approach cases produced no accepted cue. None of these counts is instructor-rated precision or learner usefulness.

Build/test repairs included a C++ name-qualification error, an ambiguous bridge `Decision` type, and an omitted replay expiry being passed as infinity. Omitted expiry now derives from original capture plus its age limit, never replay time. The first Android build compiled application/test sources but its 4 GiB Gradle JVM failed a native memory allocation during packaging on this 8 GB PC. The retry with one worker, a 1536 MiB heap and Serial GC succeeded in 1m26s (76 tasks,9 executed). This changes the build invocation, not the phone workload.

APK: `build/LearnDrive-0.5.0-debug.apk`, **237,891,741 bytes**, SHA256 `031894031de6e8f2b4caa3dd170f1e6267522fdc6dab0e322e6f1302b282cbd6`. Package `ai.learndrive.mobile`, minimum API28, target36. Debug certificate SHA1 remains `C7:6A:AA:C4:F9:5E:0E:3F:97:D8:C0:5E:BB:9D:3C:2A:3B:6D:A4:8F`.

Successful Android retry command, from `android/`:

```powershell
.\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest --no-daemon --max-workers=1 '-Dorg.gradle.jvmargs=-Xmx1536m -XX:+UseSerialGC -Dfile.encoding=UTF-8'
```

The old 0.4.8 phone checks and historical 62 video configurations are not counted as 0.5.0 results.

The new video runs use semantic mode, unknown speed, the actual bundled detector, a 30 Hz analyzed timeline with at most8 Hz labels, and an explicitly assumed224 ms publication delay. They do not supply calibrated original optics, IMU or motion. Fresh source, library and tool hashes were checked against all28 reports. Evidence: `build/balanced-coaching/video-contracts.json` and individual `video-replay/*/report.json`/`decisions.jsonl`. Reproduce the contract inspection with:

```powershell
$reports=Get-ChildItem build/balanced-coaching/video-replay -Filter report.json -Recurse | ForEach-Object FullName
python tools/inspect_coaching_video.py @reports --verify-artifacts --output build/balanced-coaching/video-contracts.json
```

## E. Performance

The Release x64 synthetic policy benchmark ran 20,000 decide/accept ticks: p50 **0.4 µs**, p95 **0.5 µs**, p99 **0.5 µs**, observed maximum **28.1 µs** in the retained run. `sizeof(CoachingEngine)` is **284,296 bytes** and `WorldState` **5,744 bytes**. Ordinary C++ `new` allocation count inside that measured loop: **0**. Setup and diagnostic JSON serialization are excluded; direct `malloc`, aligned allocations, full C API coordination and Android/JNI overhead are outside this counter.

These numbers describe a small synthetic host policy workload. They are not app RSS, perception latency, sensor-to-speaker delay or a phone worst-case bound. Existing inference/performance snapshots and new capability/onset counters provide measurement hooks. No current S24 Ultra CPU/GPU, sustained battery/thermal, frame-drop, peak RAM or acoustic onset measurement was taken. No heat reduction or broad Android qualification is claimed.

## F. Remaining issues

- No qualified instructor-reviewed event/silence labels or learner usefulness study. Existing 70 review rows remain unrated. False/missed cue rates and meaningful timing windows remain unknown.
- No pretrained reasoning model quality/performance comparison. Future model proposals stay shadow-only until better behavior is measured. The new shadow runner rejects Google-derived and unverified recorded inputs.
- New Android runtime, navigation lifecycle under motion, physical audio cancellation/onset and sustained-device checks remain unexecuted. S24 Ultra / Android 16 is the first target, not proof of broad compatibility.
- Optical approach, calibration, pose and Supercombo estimates retain the audit's uncertainty. There is no lane-perfect path, all-clear guarantee, measured clutch/pedal input or ordinary-phone selected gear.
- No new traffic/congestion feed, roundabout-specific phrase or road-surface classifier is claimed. Current route events expose only the integrated provider's supported fields. Exact ten-metre Google coaching is not qualified by a GPS accuracy allowance of up to 25 metres; generic preparation remains conservative.
- Precise mechanical actions require a validated vehicle data source. Synthetic Desk engine inputs are clearly lab data.
- Model commercial licensing, Google contractual/attribution review, privacy/release assessment and a production signing/release process remain launch requirements. Current artifact is a debug engineering build.
- Scalar rule/deferral timing, repeated-warning worsening thresholds and Basic fallback budgets are experimental. A bounded record of accepted cues is not an assessment of driving skill.

## G. Safety and release readiness

The implementation has software, host and replay evidence for the stated engine boundaries. It does not establish a dependable replacement for a qualified instructor or readiness for a public paid learner release. Experimental critical commands remain disabled by default. Real-world recognition, driver workload, useful timing, failure recovery and sustained devices require supervised qualification. The engineering work proceeds without inventing that evidence.

## H. Next steps

1. Run the compiled preferences/audio/native-ticket/lifecycle instrumentation on the S24 Ultra, then measure actual speaker onset and cancellation under interruptions and source changes.
2. Have qualified instructors independently label complete clips, including silence windows and missed encounters, using the supplied schema; preserve disagreement and a held-out split.
3. Measure sustained unplugged device operation, Basic fallback and navigation coexistence on the S24 Ultra and at least one slower Android target.
4. Tune experimental policy thresholds against those labels, then run a supervised learner pilot and complete licensing/privacy/release gates before a paid launch.

Local detailed logs and reports are under `build/balanced-coaching/`; APKs and private diagnostics remain outside Git. Nothing from this branch was pushed by this implementation task.

See [prompt completion matrix](MASTER_PROMPT_STATUS.md) and the checked-in [validation record](validation-20261026.json) for the exact scope and retained evidence hashes.
