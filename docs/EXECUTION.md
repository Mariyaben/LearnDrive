# Reliability roadmap execution

## 22 October 2026: setup interaction hotfix; heat work held

The supplied recording and physical regressions confirm the Start control can
be disabled between touch DOWN/UP by a semantic-only publication without frame
context. Repeated setup bringToFront/text updates also request layout. Android
**0.4.5 / code 25** reads completed camera evidence for UI/admission, keeps
semantic-first rules unchanged, and makes stable presentation updates idempotent.
Both regressions fail on installed 0.4.3; the hotfix passes **7/7** physical
UI/caption/provider cases with **0 skipped**. No monitoring was approved by the
fixture. Source/freshness/mount/movement/error/thermal guards remain enforced.
Installed APK SHA-256 is
`378c1a96540a576bdfd7e7867a8c753c13804fb6f2d6cbbc34549d677d5d9d63`.
The detector source, 64 assets and app/ORT native libraries match 0.4.3.
The user's requested hold is preserved: uninstalled 0.4.4 heat sources/APKs are
archived outside the active build; no heat improvement is claimed.
[Evidence and held-work restoration notes](reviews/2026-10-22-ui-interaction-hotfix.md).

## 20 October 2026: wireless map-display isolation

Diagnostic **0.4.3 / code 23** is built and installed; its APK/installed SHA-256
is `2a97ad6e1db5bbbd23149522e512d9c040f657bd1eb5a1e8911ddf54bf558f64`.
It adds immutable per-stage counters and reversible display-only map suspension.
Five device contract cases, APK assets and eight analyzer fixtures pass.
A Google display on/off/on bench capture completes with camera/AI continuity,
but fails workload matching: thermal 0→1, spatial rate variation and enabled
Battery Saver with unknown baseline. Observed CPU is 230.2% / 209.9% / 233.7%;
even with the active display suspended, frame processing occupies about 99%
of the serial inference executor. No map power percentage or thermal fix is
claimed. The latest compiled diagnostic now refuses Battery Saver; no further
heated run was attempted. The app is stopped for cooldown.
[Protocol, raw evidence and qualification limits](reviews/2026-10-20-map-rendering-thermal-check.md).

## 20 October 2026: Google candidate and thermal investigation

Android 0.4.2 / code 22 assembles with AGP 8.13.2, Gradle 8.13 and Kotlin 2.2.10;
Navigation 7.6.1, Places 5.1.1, NIO 2.0.3 and DataStore core 1.2.1 are pinned.
The R8 update resolves rewriting of existing Valhalla Kotlin 2.3 metadata.
Full native regression passes 10/10 groups; focused navigation has 103 checks.
Six S24 / Android 16 smoke cases pass: three captions, two adapter contracts and
one real Places prediction/details session (LOCATION-only fetch; observed 425 ms).
Signature, all 55 bundled PCM/model/config/license copies, both ORT runtimes and
18 native libraries' minimum 16 KiB ELF LOAD alignment verify. This is not a
16 KiB-device runtime qualification.

APK: `build/LearnDrive-0.4.2-debug.apk`, 237,675,276 bytes; SHA-256
`fff3d4698501072aa7a9c157687beed5393b63ec2e357fb0b90a09b18c467f66`.
The installed phone APK already matches this hash. Complete Google route/feed,
camera/offline lifecycle regression and sustained workload qualification remain
open; see [the increment review](reviews/2026-10-19-google-navigation.md).

The user reports rapid heating while charging. A live observational check finds
substantial app/vision CPU and rising skin/battery readings; stopping the app
is followed by falling processor/skin readings while USB charging continues.
Charging, changing pose/monitoring state and unmeasured GPU load prevent assigning
causal percentages to models/maps. No inference cadence or thermal guard was
changed during this diagnosis. [Measurements and next isolation steps](reviews/2026-10-20-thermal-quickcheck.md).

Started 16 October 2026. Audience confirmed: Android car learners in India.
This record separates implemented artifacts, automated checks and external evidence.

| Work package | State | Evidence / remaining work |
| --- | --- | --- |
| P0 product definition | Engineering brief and research protocol prepared | `product/FIRST_RELEASE.md`, `product/RESEARCH_KIT.md`; instructor/learner validation pending |
| P1 common pipeline | First engineering increment implemented and verified; phase remains open | Shared native selection, recorded/live provenance, source lifecycle and versioned traces; remaining items below |
| P2 benchmark | Public development suite added; acceptance not passed | Four sourced clips, 13 configurations, false/irrelevant cues found; independent labels and held-out data still needed |
| P3 perception audit | Not complete | Upstream/calibrated-input comparison and cadence experiments outstanding |
| P4–P7 | Planned | See `RELIABILITY_ROADMAP.md`; no release gate waived |

## First engineering increment

- Adopted the engineering release brief for Android car learners in India.
- Prepared learner/instructor session protocols and a worksheet containing all
  43 current phrases. All instructor decisions remain pending.
- Added one native guidance selection/acknowledgement path for Android Camera,
  Android MP4 and desktop replay, with source IDs and source-transition resets.
- Rejected old-source inference results and stale audio acknowledgements while
  preserving the global audio cooldown.
- Separated MP4 from Desk. Recorded guidance no longer borrows the reviewing
  phone's live pitch, acceleration, braking or shock. Original calibration is
  unavailable, so Android MP4/screen tests use semantic detections only.
- Added versioned decision/accepted traces and default-off emergency research
  commands. See [the pipeline contract and phone checks](GUIDANCE_PIPELINE.md).

### Verification results

Android `assembleDebug` succeeds for ARM64 and x86-64. Camera-health checks: 8/8
pass. Python replay syntax check passes. APK asset verification confirms the
pinned Supercombo model/license, detector/road assets, all 43 PCM clips, notices
and matching native ORT runtimes. `apksigner verify` succeeds.

The Release host build succeeds. `ctest --test-dir build/native-spatial
--output-on-failure` passes **9/9 targets**, including the new source/conformance
suite and real Supercombo inference. CTest evidence is under
`build/native-spatial/Testing/Temporary/LastTest.log`.

The full 1,201-frame Kochi replay at assumed 110-degree FOV, six-second startup
pre-roll and 180 ms simulated latency preserves the previous timeline exactly:
five ordinary cues at 1.52, 6.05, 19.12, 24.45 and 29.13 seconds. Candidate tick
counts also match; **zero emergency candidate ticks**, so default-off emergency
speech did not conceal new emergency candidates. The trace contains 4,622
decisions and six accepted submissions including startup. Trace schema and
decision/acknowledgement consistency checks pass.

Evidence: `build/video-review/shared-pipeline-warm-180ms/report.json`,
`guidance.csv`, `observations.json` and `decisions.jsonl`. The new native replay
library SHA-256 is
`76d5ee476285bd7d69e99f575acda65ebcfe380759cedc5f7504f97af4d74ca5`.
Cached raw model inference is reused because the model/runtime/preprocessing
contract is unchanged; fusion, guidance, audio gates and traces are replayed
through the newly built native library. This is a refactor regression, not an
independent accuracy or recall evaluation. Known missed cues remain unresolved.

Artifact: `build/LearnDrive-0.3.2-debug.apk`, version 0.3.2 / code 15,
131,738,637 bytes. SHA-256:
`2288903a69f0a4e540b44396130d000babbb2aa7a3fe4c8395e9b76ef0f7bc9c`.
Debug certificate SHA-256:
`4de464c102c6e7c7604950359d14f594fc671d97da866e88f501b4840d820d71`.

A new APK is an engineering artifact, not proof that product, accuracy or
physical-device acceptance has passed. The prior 0.3.1 artifact is preserved.

### P1 still pending

Original calibration/telemetry sidecar import, an Android MP4 preview, persistent
session export, full shadow playback and calibrated cross-device golden replays.
iOS orchestration has not migrated. Physical-phone source-switch/audio/latency/
thermal checks are pending; `adb devices` reports no attached device.

P0 interviews and instructor approvals, additional independent labels, per-event
accuracy/recall/timing and every learner-release gate remain pending. Next work
should complete the recorded-input contract, then expand the labelled benchmark.

## Public traffic footage test

At the user's request, independently sourced and tested four public clips
(Indian clear/rainy urban traffic, US freeway and winding-road stress cases).
Thirteen configurations process 2,183 unique frames. Trace/input/audio contracts
pass, while visual review identifies false obstacle coaching, an irrelevant
opposing-traffic cue, calibration-sensitive bend availability and high speech
occupancy. These are development findings, not passed learner-release gates.
See [PUBLIC_TRAFFIC_REVIEW.md](PUBLIC_TRAFFIC_REVIEW.md) for sources, measurements,
the interactive evidence viewer and reproduction commands. APK 0.3.2 is unchanged.

## Master plan and mount readiness — 17 October 2026

The supplied execution proposal has been reconciled with the implemented engine
in [MASTER_ACTION_PLAN.md](MASTER_ACTION_PLAN.md). It contains ten ordered
increments, dependencies, deliverables and completion gates. It retains the
existing bounded engine, rejects unsupported safety/grade/gear/anxiety claims,
and makes performance targets subject to device measurement.

Increment **1a software implementation and host checks complete**; physical
acceptance and the remainder of increment 1 are open. See
[MOUNT_READINESS.md](MOUNT_READINESS.md) for the exact contract.

### Implemented

- Android no longer replaces missing capture-time gravity with an upright pose.
  Spatial inference waits for usable motion; basic semantic detection continues.
- Near-vertical/degenerate camera poses are rejected by both the Android
  readiness policy and shared native horizon calibration.
- Source-aware pose/optics loss invalidates spatial output, derived metric/optical
  evidence and pending decisions while preserving semantic display and global
  speech cooldowns. Repeated unavailable frames preserve semantic-only decisions.
- A capture-time watermark rejects pre-loss results on recovery; Android resets
  recurrent history and refreshes semantic evidence when spatial readiness returns.
- Same-timestamp road invalidation also cancels pending bend evidence without
  treating lost observations as a confirmed straight road.
- Lab status and additive `spatial_readiness` trace fields explain readiness.
  Level-ground inclination normalization remains separate from horizon alignment.

### Verification and artifact

- Baseline before edits: 9/9 native CTest targets passed.
- Final rebuilt Release host: **9/9 targets pass**, including real Supercombo,
  new warmup/mount/loss/recovery regressions, zero-allocation spatial contracts,
  source isolation and preserved audio gates. Log:
  `build/native-spatial/Testing/Temporary/LastTest.log`.
- Eight Java camera-health cases and the new hardware-independent readiness
  suite pass via `tools/run_camera_health_tests.ps1`.
- Final Android `assembleDebug --offline` succeeds for ARM64 and x86-64.
  One existing `DetectionOverlay` deprecated-API warning appeared in the initial
  build; no new compilation errors. Models/notices, all 43 PCM clips and matching
  packaged ORT runtime contents pass `verify_android_assets.py --apk ...`.
- `apksigner verify --print-certs` passes with the existing debug certificate.
- Full 1,201-frame Kochi replay reruns inference after the calibration-source
  change. Five ordinary cues, startup events and all candidate tick counts match
  the previous baseline exactly. There are 4,622 decisions, six accepted
  submissions including startup, and zero emergency candidate ticks. Trace
  acknowledgement, 3.5 s spacing and 10 s duplicate-lock checks pass.

APK: `build/LearnDrive-0.3.3-debug.apk`, version **0.3.3 / code 16**,
131,755,021 bytes. SHA-256:
`3d9611826fb30da3800fa681b1e4581ca939dd5f82b0813e88f51bc71b7e4f3a`.
The prior versioned APKs remain preserved. The new copy's hash matches the
verified Gradle output. Debug signing is not store-release qualification.

Replay evidence: `build/video-review/mount-readiness-warm-180ms/` contains
`report.json`, `regression.json`, `decisions.jsonl`, `observations.json` and
`guidance.csv`. Native replay library SHA-256:
`e04b20705e2679468b1383cef26016420b3dbaee1b1e2edf1b0292e0e784d29f`.

The first host rebuild hit sandbox AccessDenied; an approved retry later stalled
in linking. That build's process tree was stopped and a clean incremental build
completed using workspace Zig caches. Reproduce the cache setup in PowerShell:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = 'D:\LearnDrive_mobile\.tools\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = 'D:\LearnDrive_mobile\build\native-spatial\zig-cache'
cmake --build build/native-spatial --parallel 2
ctest --test-dir build/native-spatial --output-on-failure
```

### Remaining and next step

`adb devices` reports no connected phone. Physical startup, mount fixture,
sensor interruptions, audio, latency, battery and thermal acceptance remain
unverified. The replay uses explicitly assumed optics/level pose and does not
exercise Android CameraX/AudioTrack. Existing perception-quality findings remain
open. iOS orchestration has not migrated; its earlier fallback still needs work
and macOS/device validation.

Next slice **1b**: simplify parked setup/readiness, detect mount changes without
mistaking vehicle motion for a changed mount, and define the physical angle-
fixture protocol. Do not automatically zero grade on unknown ground. Then
complete original-recording sidecars and local evidence export (increment 2).

## Phone-only gear research added to the plan — 17 October 2026

At the user's request, added experimental workstream **4a** to
[MASTER_ACTION_PLAN.md](MASTER_ACTION_PLAN.md): microphone-based RPM estimation
plus speed and a validated manual-car profile, independent reference labels,
held-out evaluation and Android shadow mode. Actual gear estimation and shift
recommendations have separate activation gates. Unknown, noisy, slipping-clutch
and transition states must not generate guessed numbered gear instructions.

This is a planning update only. No estimator, microphone capture, new gear
instructions or APK change is implemented by this update. The current next step
remains 1b setup/readiness; data collection for 4a can start with a consented,
supervised reference session and later use the recording/evaluation contracts.

## Parked setup and reference provenance — 17 October 2026

Increment **1b software implementation and host checks complete** in Android
0.3.4; physical acceptance remains open. The user identified **Samsung S24 Ultra
running Android 16** as the first physical test target. Its
[qualification profile](devices/SAMSUNG_S24_ULTRA_ANDROID_16.md) records which
details are user supplied and which still require direct inspection.

### Implemented behavior

- Road-camera launch now shows parked setup with coaching inactive. Lab settings
  are collapsed; screen testing is an explicit per-launch selection rather than
  an inherited saved default. Fresh camera evidence, voice readiness and explicit
  forward-mount confirmation gate Start. Unsupported/failed/stalled camera and
  known moving-speed conditions block starting; limited semantic-only operation
  remains possible without optional spatial readiness.
- The road-session preview expands and hides lab/setup controls. End, source
  switches and backgrounding clear confirmations; coaching cannot silently resume.
- Level-ground confirmation is optional and separate from automatic horizon
  alignment. Shared fusion exports a successful normalization generation, preserved
  across reset and incremented only after a completed normalization. Android
  accepts a newly completed generation only after explicit level confirmation.
- Physical inclination display and advice require the matching reference and
  fresh motion. Motion loss, normalization replacement or lifecycle/source end
  expires it; expiry cancels current audio. Automatic reacquisition cannot reuse
  confirmation. Pending requests expire after 15 seconds; Skip keeps basic coaching
  usable without inclination lessons.
- A limited consistency heuristic requests Check mount after at least 15 degrees
  of roll difference sustained for two seconds of fresh observations with measured
  speed at most 1 km/h. Unknown/manual speed, moving speed, data gaps and brief
  disturbances do not satisfy that dwell; pure hill pitch does not change roll.
  A check cancels audio, resets native motion/source state, expires reference and
  pauses coaching for explicit parked recheck. It does not establish why the
  orientation changed or detect all mount/yaw movements.
- Android decision traces add `setup_state`, `calibration_id` and
  `level_reference_id`, preserving the telemetry ABI and existing source/audio
  contracts. [SETUP_FLOW.md](SETUP_FLOW.md) documents behavior, limits and the
  not-yet-run physical angle-fixture/lifecycle protocol.

### Verification and artifact

- Final native Release build passes **9/9 CTest targets**, including **120 core
  cases**, 25 dual-brain cases, real Supercombo inference and the existing
  perception/allocation/source/audio checks. Two new core cases verify generation
  provenance across explicit reset and automatic reacquisition, including C ABI.
- **13 DriveSetup cases**, the existing eight camera-health cases and spatial
  readiness suite pass via `tools/run_camera_health_tests.ps1`. New checks cover
  level provenance/expiry/skip, late completion, sensor loss, stopped/moving/unknown
  speed, roll dwell, pitch-only hills, data gaps, landscape wrap and session end.
- Final `:app:assembleDebug --offline` succeeds for ARM64 and x86-64.
  `verify_android_assets.py --apk ...` verifies the pinned models, notices,
  all 43 PCM clips and matching packaged native ORT contents. `apksigner verify`
  passes with the existing debug certificate.
- Full 1,201-frame Kochi native replay retains exactly the prior five ordinary
  cue events, startup events and candidate tick counts. Its 4,622 decisions and
  six accepted submissions (including startup) satisfy trace acknowledgement,
  3.5-second spacing and 10-second duplicate checks; zero emergency candidate ticks.
  Unchanged perception inference is cached; the rebuilt native guidance executes.
  This does not exercise Android's new setup UI or physical audio/sensors.

Artifact: `build/LearnDrive-0.3.4-debug.apk`, **version 0.3.4 / code 17**,
132,006,293 bytes. SHA-256:
`9b8edc0e6a783aa3291611843ba7aeed47acdfb8520438ffad678b7361417a71`.
The final copy matches the verified Gradle output. Prior 0.3.3 and earlier APKs
remain preserved. This remains a debug-signed lab build.

Evidence: `build/video-review/parked-setup-warm-180ms/` contains the report,
`regression.json`, decision trace, observations and guidance timeline. Native
replay DLL SHA-256:
`944797b7bfedb874076fb55605688afc09cea0d7d4f5a9854127efd92d8127fb`.
CTest output is in `build/native-spatial/Testing/Temporary/LastTest.log`.

### Remaining and next step

No ADB device or local AVD was available. Android 16 runtime/UI layout, actual
GPS jitter, angle errors, false mount checks, microphone/audio-route coexistence,
audible interruption, battery and thermal behavior remain unverified. The
15-degree/two-second/1-km/h settings are provisional engineering gates. General
mount movement cannot be determined from these sensors alone. Existing road-cue
quality findings remain open; neither passing this suite nor choosing an S24 Ultra
establishes learner-release reliability or market demand. iOS has not migrated.

Next independent engineering increment: original-recording sidecar metadata and
local session/evidence export. Run the parked setup/device protocol on the user's
S24 Ultra / Android 16 when connected. Before using the existing profiler for
full-session qualification, distinguish camera-only measurements from an explicitly
started road session. Gear workstream 4a remains planned and separately gated.

## User-reported stationary setup result — 17 October 2026

After the supplied stationary installation/camera/sound/Start/End/background
checklist and optional level-reference instructions, the user reported
"everything worked" on the Samsung S24 Ultra / Android 16. Recorded as a
user-reported stationary smoke pass. No instrumented logs or per-step measured
results were supplied; angle accuracy, mount-check robustness, physical latency,
thermal behavior and road-guidance quality remain open. The next implementation
slice is opt-in local diagnostic evidence capture/export and frame provenance.

## Local diagnostic evidence export — increment 2a, 17 October 2026

Android **0.3.5 / code 18** implements the first evidence/provenance slice of
increment 2. Full recording sidecar import and calibrated replay conformance are
still pending. [SESSION_EVIDENCE.md](SESSION_EVIDENCE.md) describes the format,
privacy/storage boundaries, test coverage and phone workflow.

### Implemented

- Explicit opt-in capture in lab settings; no image/video/microphone recording,
  GPS coordinates, persistent phone identifiers or document paths/URIs. Device/OS
  and app/model versions, source/frame metadata, sampled detections/telemetry,
  decision reasons and accepted audio submissions are recorded locally.
- Dedicated bounded writer: 128 queued records, one private file at most 8 MiB,
  no producer-side file IO or waiting for queue capacity. Queue/serialization loss
  and byte-cap truncation are explicitly incomplete. Existing saved evidence is
  not overwritten; users export/delete it before a new capture.
- Start cancellation/background safeguards, stop/drain, visible write/export/
  delete failures, recovered partial-file export and Android document-picker
  export. Recording stays off by default and cannot resume silently.
- Source/frame sequences, original decoded presentation time, capture/completion
  and semantic capture times, dimensions/crop/rotation, pixel range/format and
  range provenance are preserved. Advice versus measured-phone telemetry remains
  explicit; unknown original recorded calibration/motion stays unknown.
- A portable inspector distinguishes complete capture intervals from partial
  captures and checks JSON/schema, boundary/count and accepted-decision consistency.
  Complete does not mean a full drive, exhaustive trace, heard audio or accurate
  guidance. Decisions retain changes-or-at-least-once-per-second sampling; frame
  observations follow semantic updates up to 4 Hz.

### Verification

- Java journal checks pass for draining/stopping, preserved prior evidence,
  bounded queue/file, reported loss, IO failure and invalid/oversized records.
  Existing eight camera-health, spatial-readiness and 13 setup cases also pass.
- **9 Python inspector tests pass**, covering complete/interrupted/lossy files,
  corrupt counts, unmatched acknowledgements and non-standard numeric values,
  including distinguishing nonfinite JSON from a merely truncated final record.
- **9/9 native CTest targets pass**. Native driving/perception code and model
  weights are unchanged; these remain regression checks, not Android export proof.
- Final Android ARM64/x86-64 debug build succeeds. All bundled model/license/43
  PCM assets and matching ORT runtime contents verify in the APK; signature checks
  pass with the existing debug certificate. The versioned artifact matches the
  verified Gradle output.

Artifact: `build/LearnDrive-0.3.5-debug.apk`, 132,074,597 bytes. SHA-256:
`74c52324460c4b91bb4dfb1c54b90ffb6d420c2a81baf953e2c7859465b9d38f`.
Verification summary: `build/session-evidence-verification.json`. Earlier APKs
remain preserved. No new road-accuracy or performance qualification is claimed.

### Remaining / next device check

On the Samsung S24 Ultra / Android 16, test parked capture → short setup/session
→ stop → export JSONL, then inspect the exported file. Export chooser cancellation,
actual storage errors, background interruption, recovered partial capture and
diagnostic CPU/battery overhead remain unverified on hardware. The earlier
stationary setup pass was user reported for the prior setup-check instructions;
it does not validate these new controls. Original video identity/hash, sidecar
schema/import, original sensor/time mapping, MP4 preview and Android/desktop
golden replays remain open for increment 2b and later work.

## First physical diagnostic export reviewed — 17 October 2026

The user supplied `D:\Downloads\LearnDrive-session.jsonl` from Android 0.3.5 /
code 18, Samsung SM-S928B / Android 16 / API 36, OS build
`BP4A.251205.006.S928BXXS6DZG1`. The inspector passes: 183 body records, zero
reported drops, no byte-limit event, clean user-stop footer. This is evidence of
the normal local capture/export path. Detailed evidence identity, measurements
and limitations are in [the review](reviews/2026-10-17-s24-ultra-first-export.md).

The approximately 41-second interval contains 140 frame observations and 42
decisions, all SETUP / `source inactive`, with zero accepted submissions. It
does not demonstrate an active supervised session. All measured speeds are
unavailable; advice uses the labelled 30 km/h manual fallback. Native automatic
calibration ID 1 is present, but confirmed level-reference ID remains zero.

Sampled processing median/p95 are 141.870/149.398 ms; capture-to-completion
median/p95 are 224.316/247.794 ms. These are preliminary short camera measurements,
not camera FPS, physical audio timing, overhead or sustained qualification.
24/42 sampled scene ages are at least 350 ms, warranting an active trace to
measure actual guidance availability without relaxing freshness checks.

No app fault or policy change is established by this capture. Next device step:
capture an explicitly confirmed Start-to-MONITORING-to-End interval and inspect
its decisions. The user confirmed they stayed in setup / Lab settings during
this first capture, matching the inactive trace. Failure-path/lifecycle tests,
full recording sidecars and road-quality gates remain open.

## Public traffic replay extension — 17 October 2026

At the user's request, searched for additional footage and evaluated a new CC0
Bangalore road clip plus four existing public development clips. **Nine replay
configurations / 8,954 frame steps** complete through the current shared native
engine. Source identity, protocol, commands, artifacts, measurements and limitations
are in [the traffic review](reviews/2026-10-17-traffic-video-extension.md).
`build/public-traffic-extension/aggregate.json` records the audited results.

All nine trace/audio contract checks pass. Guidance acceptance remains open:
known road/guardrail false-car obstacle cues and opposing-traffic advice reproduce;
Jodhpur clear speech occupies 97.7% of its ten seconds. On the new 48.35-second
Bangalore clip, four semantic cues occupy 33.48% with unknown speed and 45.27% with
assumed 30 km/h, due to longer wording. Actor-linked usefulness is unverified.

Fixed 224 ms delay tests, informed by the earlier short phone capture, expose
roughly 44–47% semantic freshness availability in semantic runs. The added 500 ms
stress control yields zero fresh semantic ticks and zero video cues. A 20 Hz
spatial sensitivity run uses assumed optics/level pose, has zero road-valid
outputs, and supplies no calibrated bend-recall result. No accuracy percentage,
Android runtime acceptance or learner benefit is claimed. No app/policy/model/APK
change was made. Next: active-session device trace, actor-linked failure
regressions, relevance/temporal checks and calmer phrase selection, with unseen
local drives and instructor review before acceptance.

## Learner camera coaching corrections — Android 0.3.6, 17 October 2026

At the user's request, implemented and verified fixes to the reproduced camera
guidance failures, with preservation of meaningful learner attention rather than
cue-count reduction alone. [Design and limits](LEARNER_COACHING.md);
[final evidence and reproduction](reviews/2026-10-17-learner-coaching-fixes.md).

The camera coach now requires distinct, plausible and relevant observations.
Image size alone cannot request slowing or a gear change. New short cues identify
person/two-wheeler/vehicle and give one space/gap action. Person/bike overlap and
weaker bike evidence after a rider cue prevent false pedestrian wording and
repeated physical-actor lessons. Newly entering actors can receive attention
without waiting for the ordinary eight-second lesson gap. Optical escalation,
braking/shock/source guards, global audio limits and the 350 ms freshness cap remain.

Selected camera actor/class/evidence/box fields flow through C ABI/JNI and replay
traces. Android publishes semantic results before spatial inference, up to 8 Hz,
and logs semantic completion independently. Exact eligible decisions are recorded
before accepted acknowledgements even when audio becomes free between sampled
ticks. No model weights/preprocessing math or emergency default changed.

Final `r3` evidence: **eight replay configurations / 7,503 frame steps**. Known
road/guardrail false-car and across-divider cues no longer recur. Jodhpur retains
two distinct lessons, at 1.99 s for the actual motorcyclist and 5.51 s with a box
matching the crossing white car. Actor association is visually inspected, not
instructor-approved crossing intent/risk/timing. Speech occupancy falls from
97.70% to 54.39% in Jodhpur clear and 45.27% to 16.44% in Bangalore. No ordinary
cue interrupts another. The rain clip's last cue is truncated by source end and
cannot support a strong speech/timing improvement claim.

At fixed 224 ms delay, Bangalore's semantic freshness availability is 46.72% at
the old 4 Hz schedule and 93.44% at 8 Hz under the same final policy; at 500 ms
it is zero, with no video cues. This is a replay sensitivity result, not measured
phone performance. Spatial sensitivity has no calibrated road-valid acceptance.

**9/9 native targets pass**, including **131 core cases** and 25 dual-brain cases.
Eight replay contract and positive/negative development checks pass. Existing
Java camera/readiness/setup/journal and nine inspector cases pass; two PCM-padding
tests verify that trimming retains all nonzero speech and internal pauses.
Final Android ARM64/x86-64 build, all 46 PCM/model/license/runtime assets and APK
signature verify. `build/learner-coaching/verification.json` identifies the build.

Artifact: `build/LearnDrive-0.3.6-debug.apk`, **code 19**, 135,196,409 bytes;
SHA-256 `b7dd3a8250f609dbffc55993110a68b924a4f7d879a1d359873115bcbcd6899e`.
Native replay SHA-256:
`144c2b5eca8294c7187c5b5447e017d95ceccbecbd688e2f0be2abf866ab2b1f`.
Prior APK versions and intermediate rejected candidate traces remain preserved.

No ADB device is attached. Next: parked, explicitly active 0.3.6 device capture,
actual semantic/audio/sustained-cost measurements, independent instructor labels
and supervised learner comprehension/usefulness review on reserved local sessions.
General false detections, ambiguous lanes/crossings, missed bends and real-road
benefit are not declared solved. iOS runtime and hardware qualification remain open.


## 2026-10-19 ? Kerala offline route context, Android 0.4.0

Implemented the user's requested offline navigation integration before resuming
learner-coaching fixes. Valhalla Mobile 0.6.4 / native 3.9.1 routes locally;
MapLibre 13.6.1 renders local PMTiles. The parked planner imports a checked
regional pack, searches local towns, accepts a destination and saves a reviewed
route. Shared C++ geometry feeds conservative preparation cues through the existing
coordinator. Map cues require measured GPS/source/quality evidence and never infer
numbered gears, clutch actions or safe speeds. INTERNET permission is absent.

Built Kerala's 164-tile graph, zoom-14 basemap and 3,665-place index into
`build/LearnDrive-Kerala-20261019.ldnav` (305,607,592 bytes). Corrected the regional
builder's missing India parent/right-driving default before graph construction.
Five host routes passed graph/response/left-driving checks; London was rejected.
Unclassified border/enclave admin records and source/build warnings remain open.

Installed and tested on the user's Samsung SM-S928B / Android 16 / API 36,
preserving app data. Actual-phone tests found a repeated destination coordinate
that initially failed core validation; normalization now retains original maneuver
indexes and removes zero-length matcher segments. Final **10/10 native groups**
(including 51 navigation checks) and **3/3 phone integration cases** pass. The
Kerala pack imported and the map rendered locally; planner screenshots inspected.

Kochi?Alappuzha is 56.291 km. Across first/final phone runs, first requests took
115.9 / 137.4 ms, warm medians 25.6 / 36.7 ms. Engine initialization was separately
402.3 / 31.3 ms. The cold 100 ms target is not met. A separate ARM64 microbenchmark
of the same context/policy source at -O0 measured 0.403 microseconds mean across
100,000 active bend evaluations, with zero heap allocations. This excludes JNI,
camera, map drawing and audio and is not a whole-loop/worst-case claim.

Final APK `build/LearnDrive-0.4.0-debug.apk`, code 20, 184,321,526 bytes; SHA-256
`bbfc14341b127070c9300f85dcc5538c90ea58783648dd445b6719fa52f9301a`.
All 51 PCM clips/config/model assets match their APK copies; both ABI libraries,
16 KiB ELF load alignment and existing debug signature verified. Kerala pack SHA:
`9e1cd026c640a131dc618d6881ac2ce854408d65a92ac2b67412f5d8e56955db`.

[Implementation and user flow](OFFLINE_NAVIGATION.md),
[verification and limitations](reviews/2026-10-19-offline-navigation.md).
The map foundation is ready for parked/device evaluation. Real GPS/maneuver timing,
map/vision/audio coexistence, sustained thermal/frame pacing, independent road
restrictions and instructor/learner usefulness remain acceptance work. Earlier
screen-recording coaching findings and gear-estimation research remain queued.

## 2026-10-19 — Unified coach foundation, Android 0.4.1

Adopted the user's generated-video reference as a product target: one dashboard
with map, measured status, the original live camera/detection overlay and one
coaching strip. Removed the separate navigation activity; destination suggestions
and route attachment now stay in MainActivity without restarting capture. Lab
controls remain in parked settings. Monitoring keeps the display awake.

Built the Kerala v3 pack with 178,079 named places in a precomputed SQLite FTS4
index. A draft destination preserves the committed route until its replacement
is ready. Map display uses local PMTiles and an independent display interface.
Distance-specific turn phrases require qualified GPS evidence; all speech still
passes the shared native coordinator. Captions now expire on time, source/route
invalidation and passed map events, and clear with interrupted action speech.

Ordinary road coaching now uses measured telemetry directly; unavailable speed
cannot inherit a manual lab value. Mock positions qualify neither speed nor map
context. The policy no longer selects “Maintain speed” from empty detections.
An observed camera-start layout wait was repaired using a bounded preview-size
retry with cancellation on pause/binding. The final test also covers recreation.

Verification: **10/10 native groups**, **132 core cases**, **55 navigation checks**,
and **5/5 S24 Ultra / Android 16 cases in 8.558 seconds** pass. Physical checks cover
camera/model publication, route/search source continuity, committed-route
retention, visible map/camera regions and background/recreation recovery. The
monitoring screenshot is a bench layout fixture. All 55 PCM/config/model/license
assets and both ABI runtimes match the APK; signature and all 14 native libraries'
16 KiB minimum ELF load alignment verify. Existing app data/route are preserved.

APK: `build/LearnDrive-0.4.1-debug.apk`, code 21, **184,525,384 bytes**; SHA-256
`f9bdd53a611823e868fe35f65ffab1e5a2ed97195ffcb440d30e229c56d3e026`.
Pack: `build/LearnDrive-Kerala-20261019-v3.ldnav`, **334,770,022 bytes**; SHA-256
`bd761926cb83efb86b1dacd19adf1b43341d859638017d8b0ba9260ed0182e1a`.
Evidence and source hashes: `build/unified-coach/verification.json`.

The user selected Google Places/Navigation and supplied project ID
`learndrive-development-512108`. [Setup instructions](GOOGLE_NAVIGATION_SETUP.md)
and the local development key are ready. Cloud Console confirms saved Android
package/certificate restrictions and access to only the three required, enabled
APIs. Google SDK/provider integration, billing readiness checks and Google device
qualification remain pending; no Google provider or INTERNET permission is active
in this APK. Restriction proof: `build/unified-coach/google-key-restrictions.png`.
Next: provider integration and one complete turn/bend lesson with observed
response and an evidence-linked review. Full road-feature/gear capability,
instructor/learner usefulness and sustained thermal/audio behavior remain open.

See [the detailed review](reviews/2026-10-19-unified-coach.md) and
[product checkpoints](product/UNIFIED_DRIVING_COACH.md).
