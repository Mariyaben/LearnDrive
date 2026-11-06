# LearnDrive execution plan

22 October implemented checkpoint: **0.4.6** is installed and hash-verified on
the S24 Ultra. Native 10/10, final physical 15/15 and 62 replay contract audits
pass; 28 same-input old/new comparisons expose continued repetition and increased
speech burden. This closes specific engineering defects, not the learner launch
gate. Recognition, useful cue timing and instructor acceptance remain next.
[Changes, artifacts and evidence](reviews/2026-10-22-coach-reliability-increment.md).

22 October reliability execution: repair the confirmed actor/cooldown and
following-wording failures, make live evidence age visible, add a grounded
session review, and correct the replay contract before the next comparison.
The [paid-launch execution sequence](product/PAID_LAUNCH_EXECUTION.md) now links
software repairs to recognition, integrated-device, instructor and distribution
gates. Heat work remains deferred at the user's request. Passing software checks
does not close those launch gates.

22 October priority change: the user held heat work to fix unusable setup
interaction. **0.4.5** separates completed UI evidence from intermediate rules
publications and removes repeated presentation/layout mutations. Both UI bugs
reproduce on the old phone APK; seven replacement checks pass. Models, native
libraries, cadence and guards retain the verified baseline. The uninstalled
0.4.4 heat candidate is preserved for later targeted resumption.
[UI hotfix review](reviews/2026-10-22-ui-interaction-hotfix.md).

20 October diagnostic update: **0.4.3** adds per-stage counters and an active-map
display isolation hook. The unplugged Google on/off/on capture completes, but
thermal/Battery Saver/throughput changes prevent causal attribution. CPU remains
about 2.1 cores with the display suspended and inference processing nearly
continuously occupied. Prioritize measured AI duty-cycle optimizations; preserve
cadence/freshness until separately qualified. No thermal fix is claimed.
[Evidence and next experiments](reviews/2026-10-20-map-rendering-thermal-check.md).

20 October checkpoint: **0.4.2** embeds the Google provider, builds successfully
and passes 10/10 native groups plus six physical smoke cases, including restricted
key Places requests. Full Navigation route/feed/lifecycle qualification remains
open. The user's rapid-heating report takes priority in device qualification:
[live observations and isolation protocol](reviews/2026-10-20-thermal-quickcheck.md).
Measure camera/semantic/Supercombo and map rendering separately before changing
cadence or claiming a thermal improvement. Preserve evidence freshness and
thermal protection. [Google increment status](reviews/2026-10-19-google-navigation.md).

19 October product clarification: the user's video defines a **unified driving
coach**, with map and live camera together and one contextual instruction stream.
See [the reference and execution direction](product/UNIFIED_DRIVING_COACH.md).
The separate navigation-interface interpretation is superseded. The user selected
the recommended Google Places/Navigation direction and requested setup help.
This changes the earlier offline-only product direction for navigation; camera
inference remains local. The project and local development-key handoff are ready;
Cloud Console confirms the three enabled APIs and saved key restrictions. Google
source integration is present in 0.4.2 and native Places requests pass. Complete
Navigation runtime, usage/spending controls and device qualification remain open.
See [the account setup steps](GOOGLE_NAVIGATION_SETUP.md).

Foundation checkpoint: Android **0.4.1** implements the unified dashboard foundation,
in-place destination search and shared local route context. Native **10/10** groups
and physical **5/5** cases pass, including camera/model recovery after recreation.
See [the foundation verification](reviews/2026-10-19-unified-coach.md). The 0.4.2
increment above adds Google. Next: qualify thermal behavior and provider runtime,
then complete one turn/bend coaching episode through
observed response and post-drive review. This does not close learner-release gates.

Working plan adopted 17 October 2026 from the supplied master proposal and the
current repository. Android car learners in India remain the first audience.
This is an implementation plan, not a claim that performance or release gates pass.

## Product outcome

Help a learner notice relevant situations with occasional useful cues and
understand what to practise after a supervised drive. Start with supported
Android phones, a fixed forward-facing rear camera, daylight/dry roads and
instructor-supervised evaluation. Newly licensed independent drivers are a
customer-discovery hypothesis; expand the pilot only when evidence supports it.

Success requires useful learning feedback, manageable setup and repeated use.
Camera FPS, a model running successfully and passing software tests do not
establish any of those outcomes. Preserve offline processing and user choice
about retaining/exporting a session.

## Corrections to the supplied proposal

| Proposal | Adopted implementation decision |
| --- | --- |
| Instant mount calibration | Automatic gravity-based horizon alignment when fresh usable motion is available. Forward mounting remains an assumption. Absolute road grade, arbitrary yaw and original recording optics cannot be recovered from gravity. Level-ground normalization remains separate for inclination. |
| Low visibility: relying on defensive spatial buffer | Report limited capability. Never imply that unavailable perception has a dependable substitute. Brightness/blur checks cannot establish rain, dirty glass or hazard recall. |
| Green means clear trajectory | Use neutral monitoring/limited/unavailable states. No detected hazard does not mean a clear or safe path. |
| One-to-three-word phrases everywhere | Use the shortest comprehensible instructor-reviewed phrase. Avoid ambiguous wording such as “Brake ahead”; compare actual clip duration and learner understanding. |
| Drop gear 2 | No numbered gear without separately validated vehicle-state evidence and shift policy. Research phone-only gear estimation in workstream 4a; until its gates pass, conditional manual-gear lessons only. Automatic/CVT coaching is outside that workstream. |
| Potholes, tailgaters, signs in a unified scene | Only expose capabilities with actual producers and validated evidence. A forward camera does not see a tailgater; the current models do not supply reliable pothole/sign coaching. Test-injected hazards remain simulations. |
| Threat score mathematically picks the right action | Keep bounded scoring behind freshness, path, confidence and driver-action gates. Validate the chosen action per encounter; a formula alone cannot establish correctness. |
| INT8 backbone | Keep the pinned model baseline first. Quantization is an experiment requiring output, encounter-quality and device comparisons, not a file-format change. |
| Big cores, two threads guarantee efficiency | Benchmark thread count, affinity and provider choices on physical phones. Faster inference can cost more energy; retain thermal protection and explicit fallback reports. |
| Sub-30 ms and flawless thermal efficiency | Treat 30 ms as a stretch target. Measure capture age, inference, native decision, audio acceptance and audible onset separately. No universal phone guarantee. |
| 15–20 FPS inference | Test the pinned model's 20 Hz temporal contract, independently of camera capture rate. Quantify irregular sampling and thermal reductions. |
| Smoothness score and anxiety index | Report measured motion events with provenance and limitations. No anxiety or driving-skill score from IMU. Optional self-reported confidence is clearly labelled. |

## Reuse before rebuilding

Already present: C++ horizon homography and temporal YUV packing, recurrent
Supercombo runtime, semantic detector, 100 Hz reflex / 10 Hz deterministic policy,
braking suppression and escalation, bounded threat selection, global cooldown
and duplicate locks, 43 cached Android PCM clips, XNNPACK/CPU setup and optional
affinity, source isolation, native/replay tests and exploratory video reports.

These are implementation assets. Their real-road effectiveness, phone power cost
and learner benefit remain unproven. Optional SLM code stays outside live driving.
The current 0.3.2 APK and existing replay artifacts remain historical baselines.

## Ordered increments and completion gates

Each increment ends with a source change, reproducible checks, evidence paths,
known limitations and an updated execution record. Implement one bounded slice
at a time; expand a slice only to resolve a failure it exposes.

### 1. Mount readiness and truthful capability reporting — current increment

1. Reject missing/nonfinite/degenerate gravity instead of assuming an upright phone.
2. Keep semantic detections available during gravity warmup or loss.
3. Invalidate prior spatial/metric evidence on lost readiness; reset recurrent
   history before spatial recovery without resetting global speech cooldowns.
4. Show waiting-for-motion, unsupported-mount and missing-optics states separately.
5. Test tilted/landscape mounts, near-vertical cameras, stale/future motion,
   recovery, stale source work and semantic continuity.
6. Build Android and verify assets; record what still needs a real phone.

Gate: unsupported alignment cannot be presented as fresh spatial evidence;
normal gravity warmup/recovery requires no new manual calibration button.
This does not remove the level-ground reference needed for inclination.

Increment 1b now implements [parked setup, reference identity and a limited roll
consistency check](SETUP_FLOW.md), with a written physical angle-fixture protocol.
Device acceptance remains open. Do not zero road grade automatically on an unknown
slope or interpret a stationary IMU as proven parking.

### 2. Complete the recording and diagnostic contract

1. Define a versioned sidecar for original image dimensions/crop/rotation,
   intrinsics/distortion, camera/mount identity, speed, IMU and clock mappings.
2. Validate finite ranges, units, timestamps, missing fields and provenance.
3. Import video plus sidecar on Android; provide preview and recording time.
4. Persist opt-in local decision/audio/session traces and export through Android's
   document/share flow. Report write failures, disk limits and incomplete sessions.
5. Save diagnostic model input/warp samples outside the real-time rules path.

Gate: desktop and Android consume equivalent recorded evidence, reject malformed
or unrelated sidecars, and never substitute reviewing-phone motion or optics.

Increment **2a** implements [opt-in bounded local evidence capture/export](SESSION_EVIDENCE.md)
and enriched frame provenance in Android 0.3.5. The byte/queue bounds, loss reporting
and export inspector have host coverage; physical export/lifecycle performance
still needs device checks. Original recording sidecar import, preview and calibrated
Android/desktop conformance remain pending; this phase is not complete.

### 3. Establish encounter-level evaluation

1. Retain Kochi and the five public clips as development regressions.
2. Convert known false obstacle/opposing-traffic cases into actor-linked findings.
3. Add encounter IDs, cue attribution, expected wording class, useful timing
   windows and uncertain labels; keep generic time overlap distinct from recognition.
4. Obtain additional consented local clips and independent instructor review.
   Split by session/route/device; reserve unseen footage before further tuning.
5. Report irrelevant/false cues, missed/late cues, availability, cue density and
   speech occupancy together. No accuracy percentage without defined denominators.

Gate: before/after changes can be compared per encounter with traceable evidence;
quieter output cannot hide missed hazards or unsupported capability.

The [17 October traffic replay extension](reviews/2026-10-17-traffic-video-extension.md)
adds CC0 Bangalore footage and nine current native replay configurations. Contract
checks pass; false/irrelevant cues, speech density and semantic freshness remain
unaccepted. Structured findings and source identities are preserved; independent
instructor labels and actor-linked encounter acceptance remain pending.

Android **0.3.6** implements a first [learner camera coaching correction](reviews/2026-10-17-learner-coaching-fixes.md):
temporal/relevance checks, selected-actor traces, short separate person/rider/vehicle
lessons, rider/body duplicate handling and earlier/up-to-8-Hz semantic publication.
Eight development replay configurations pass with preserved positive attention
and removed known false/irrelevant cues. Raw detector errors, general lane/actor
understanding, independent encounter labels, actual phone cost and learner benefit
remain open; this is not completion of steps 3–5.

### 4. Fix perception contribution and relevance

1. Audit native inputs/decoding against pinned upstream behavior on calibrated data.
2. Compare correct 20 Hz sampling with irregular input; verify state reset and crops.
3. Diagnose false large vehicle boxes; require appropriate temporal/track evidence
   before imperative obstacle instructions without blocking valid first attention cues.
4. Separate same-direction leads, oncoming, adjacent and parked traffic; retain
   unknown classifications when evidence is insufficient.
5. Compare semantic-only, Supercombo-only and fused modes on the same encounters.
6. Decide whether to keep Supercombo or evaluate a replacement using measured
   guidance benefit, latency, memory, energy and distribution constraints.

Gate: documented development failures improve, with missed/late-cue regressions
reported on unseen footage. Calibrated road/bend availability is measured.

### 4a. Phone-only gear estimation and specific shift coaching — experimental

Added 17 October 2026 at the user's request. Status: planned research; no gear
estimator or new shift commands implemented. Target one identified manual car
first, using the existing phone microphone, speed and motion sensors without
additional hardware in the deployed product. This is an optional workstream;
the learner product must remain useful when gear is unknown or unsupported.

1. **Define the measurement contract.** Separate confirmed vehicle data, estimated
   RPM/gear, human reference labels and Desk simulation. Carry capture time,
   freshness, vehicle/profile identity, signal quality, uncertainty and explicit
   unknown/transition states. Do not feed an estimate into the existing engine
   sample ABI as though it were confirmed live gear/RPM.
2. **Collect a supervised reference dataset for one car.** Synchronize phone audio,
   GPS/IMU and independently observed gear/tachometer labels, including uncertain
   intervals. The driver must not operate the phone or announce every shift to
   make the feature work. Obtain consent for microphone use; raw audio retention
   is a separate opt-in with local storage limits, export and deletion.
3. **Prototype acoustic RPM estimation offline.** Track engine harmonics and
   compare against reference RPM. Test petrol/diesel only as separately qualified
   profiles; reject speech, music, horns, wind, AC and other engines when they
   prevent a credible estimate. Account for the app's own spoken guidance.
4. **Infer engaged gear from RPM and speed.** With the clutch fully engaged,
   compare engine-RPM/road-speed ratios against validated per-car gear profiles.
   Use manufacturer ratios/final drive/tyres where verified, or supervised
   labelled calibration. GPS/IMU alone is insufficient. Return unknown during
   clutch slip, shifts, neutral coasting, very low speed, stale speed, poor audio
   or ambiguous ratios; do not assume a missed shift changed the gear by one.
5. **Evaluate on separate drives before integration.** Split by session, then
   expand to different phones, mounts, drivers and cars. Report per-gear errors,
   confidently wrong estimates, unknown coverage/duration, shift detection delay,
   RPM error and recovery after noise. Include low-speed manoeuvres, skipped gears,
   hills, clutch use and open windows. Choose numerical activation thresholds
   before the held-out evaluation; confidence scores alone are not validation.
6. **Run in shadow mode on Android.** Estimate and log in a bounded background
   worker without gear speech or changes to driving authority. Test microphone
   permissions/interruptions, navigation/audio coexistence, battery/thermal cost
   and lifecycle/source changes. Preserve offline processing and the 100 Hz rules
   budget; stop audio capture when the session ends.
7. **Validate shift recommendations separately.** Knowing current gear does not
   establish the correct next gear. Use the supported car's RPM bands and gear
   ratios to bound predicted post-shift RPM, plus credible load/motion and road
   context. Inhibit advice during ambiguous clutch/transmission state, conflicting
   hazards or insufficient evidence. Instructor-review wording and timing; enable
   numbered instructions only after supervised recommendation and comprehension
   checks pass. Unknown gear retains general conditional coaching.
8. **Qualify optional existing vehicle integrations separately.** A supported OEM
   or Android Automotive integration may provide actual gear/RPM without a new
   adapter, but availability, permissions and update rate need verification.
   Ordinary Bluetooth pairing and Android Auto must not be assumed to provide
   current gear. Label the data source and handle disconnects/staleness explicitly.

Deliverables: vehicle-profile/measurement schema, consented labelled recordings,
offline estimator and evaluation report, Android shadow implementation, device
cost measurements and a separate instructor-reviewed shift-policy report.

Gate A — estimation: held-out evidence meets predeclared error, availability and
latency targets for an explicitly supported car/phone configuration, including
unknown-state and transition handling. Do not generalize one-car results.

Gate B — numbered coaching: vehicle-specific shift policy and supervised human
evaluation pass independently of Gate A. Until both gates pass, no numbered
gear commands are enabled from phone-only estimates. Automatic/CVT, reverse/parking
instructions and unsupported vehicles remain outside this research scope.

Dependencies: increment 2 supplies synchronized evidence/provenance and increment
3 supplies evaluation discipline; integrate with increments 5/6 for speech and
phone resource checks. Reference-data collection can start earlier with a
consented supervised session. This does not interrupt the current 1b setup work.

Research context: [acoustic speed/gear estimation experiment](https://spiral.ece.cmu.edu/pub-spiral/pubfile/dsp2015_228.pdf)
demonstrates feasibility under limited conditions with multiple microphones;
it does not validate this phone-only implementation. The
[Android Automotive current-gear property](https://developer.android.com/reference/android/car/VehiclePropertyIds#CURRENT_GEAR)
and [Android Auto vehicle-data interface](https://developer.android.com/training/cars/apps/library/car-hardware-api)
have different availability and must not be treated as a universal phone API.

### 4b. Offline route context — Kerala first

Added 19 October 2026 by explicit user request; execute this before resuming the
learner-coaching changes from the screen-recording review. The first pack is
Kerala, with an extensible regional format for later coverage. See
[OFFLINE_NAVIGATION.md](OFFLINE_NAVIGATION.md) for implementation, data preparation,
source/freshness guards and acceptance limits.

1. Embed local Valhalla routing and MapLibre display with distinct routing-graph,
   basemap and place-index data. Keep phone navigation fully offline.
2. Prepare and verify the Kerala pack, including country/left-driving metadata,
   bounds, hashes, missing coverage and actual representative routes.
3. Precompute shared C++ route geometry; match fresh measured GPS conservatively.
   Pause on stale, ambiguous or off-route position. Never use fallback speed or
   current-phone measurements as original recorded-vehicle evidence.
4. Retain router turn instructions for visual navigation and enable only bounded,
   noninterrupting preparation cues through the existing guidance/audio arbiter.
   Do not infer a numbered gear, safe speed, hump or arbitrary junction.
5. Run native, data, Android runtime and packaged-artifact checks. Then measure
   route/geometry/UI/audio behavior on the S24 Ultra and review cue timing with
   instructors. Performance targets are not accepted results until measured.

Status on 19 October: implementation, Kerala pack generation and software/data
checks are complete for the 0.4.0 lab candidate. All 10 native groups and three
physical S24/Android 16 integration cases pass. The pack and APK are installed on
the user's phone. The cold first-route target remains unmet (116–137 ms for the
56 km test); a same-source ARM hot-context microbenchmark passed its mean-latency
and zero-allocation checks. Parked GPS, integrated sustained-device, instructor
and road acceptance remain open. See the
[verification review](reviews/2026-10-19-offline-navigation.md).

The previous learner-coaching and phone-only gear work remains queued. Expanding
map coverage does not expand validated driving rules or release authority.

### 5. Calm speech and arbitration

1. Define a short instructor-reviewed vocabulary and language variants. Preserve
   action meaning; measure listening/comprehension before replacing the 43 clips.
2. Group observations into tracked encounters; avoid re-announcing the same
   episode through category changes or short occlusions.
3. Measure actual accepted/started/interrupted/completed playback and audio duration.
   The existing 3.5 s gate remains a minimum, not a desired speaking frequency.
4. Test a session speech budget and simpler priority policy against cue usefulness.
5. Preserve driver-action suppression, escalation and cooldown across source changes.
   Emergency voice stays research-only until its separate evidence gate passes.

Gate: no known timing/preemption regressions; reduced nuisance and acceptable
comprehension demonstrated with instructors/learners, not only fewer utterances.

### 6. Device performance and battery

1. Select a small supported Android phone matrix, including a modest device.
2. Profile stock release builds: capture-to-analysis age, preprocessing, each model,
   decision p50/p95/p99/max, audio acceptance and externally measured audible onset.
3. Record delivered inference rate, dropped frames, peak RSS, battery/power,
   ambient/device temperature and thermal transitions for over 30 minutes.
4. Compare 20 Hz sampling, semantic cadence, thread/affinity options and providers.
   Only then test quantization/reduced runtime or another backbone.
5. Fix measured bottlenecks; keep thermal pause/recovery and degraded capability visible.

Gate: publish a measured supported configuration and its tradeoffs. Sub-1 ms
native decisions, sub-30 ms stretch latency and sustained throughput are separate
claims. Host timing cannot satisfy phone acceptance. Resolve the older roadmap's
30 FPS requirement explicitly if evidence favors the model's 20 Hz cadence.

First qualification target supplied by the user: [Samsung S24 Ultra running
Android 16](devices/SAMSUNG_S24_ULTRA_ANDROID_16.md). Exact firmware and all device
measurements remain pending. The user reports the stationary setup checklist worked;
also qualify a modest Android phone before broader claims.

### 7. Simple driving and setup experience

1. Separate pre-drive setup, driving, post-drive review and internal diagnostics.
2. Offer large labelled speed/provenance and monitoring/limited/unavailable states;
   avoid promising safety with green indicators or color alone.
3. Make camera/sensor/audio problems actionable while parked. Keep debug settings
   and engine controls out of ordinary driving; preserve explicit lab modes.
4. Test interruptions, navigation/audio coexistence, permission loss, backgrounding,
   lock/unlock and source transitions on physical phones.

Gate: users can mount/start without developer help and explain current limitations;
the driving screen does not demand interaction to interpret ordinary cues.

### 8. Evidence-based post-drive learning

1. Create an opt-in local session lifecycle and bounded background event store.
2. Generate deterministic reviews: observed episodes, delivered cues, unavailable
   periods and a small set of instructor-reviewed practice topics linked to evidence.
3. Distinguish observed motion from inferred mistakes. Hard braking can be appropriate;
   neither silence nor smoothness proves skilled driving.
4. Add instructor corrections and clearly labelled voluntary confidence reports.
5. Consider optional SLM wording only after deterministic facts are stable. No
   invented anxiety, safety score, collision probability or unsupported exact shifts.

Gate: every review statement has a supporting event/provenance or explicit human
input; interruption/crash/storage-limit handling preserves an honest partial review.

### 9. Supervised product and willingness-to-pay pilot

1. Recruit approximately three schools and 20–30 learners as a discovery pilot,
   with instructor-reviewed phrases and supported setup. This is not safety validation.
2. Observe onboarding, repeated voluntary use, nuisance, comprehension and instructor
   assessment of a specific practised skill. Record negative results.
3. Test a learning-package price and school-paid model with actual paid commitments.
4. Decide continue/narrow/pivot from repeat use, useful feedback and payment evidence.

Gate: evidence of useful learning and willingness to pay; no extrapolation from
interest, app downloads or questionnaire praise alone.

### 10. Release qualification and later expansion

Complete supported-device/lifecycle/audio checks, capability claims, distribution
and model-license review, privacy/export/delete behavior and pilot findings.
Reconcile all release documentation with measured behavior before a learner release.
iOS orchestration/macOS validation follows the Android baseline. New conditions,
languages and hazard categories each need their own evidence.

## Dependencies and working rhythm

Order: 1 → 2 → 3 → 4 → 5, with device baseline measurements starting as soon as
hardware is available. UX and session review follow stable evidence contracts.
Customer interviews and instructor phrase review can run throughout. Do not delay
all product discovery until optimization is finished.

Experimental workstream 4a follows the evidence contracts in 2/3 and is evaluated
separately from the core road-coaching path. Failure or low coverage must not
block the useful baseline product or cause guessed numbered shift advice.

Start with a candidate two-week engineering cycle for increments 1–3; revise
scope after each gate. Device work, data access and participant recruitment cannot
be scheduled as guaranteed software delivery. Subsequent increments are measured
work packages, not promised calendar dates.

For every step: inspect → implement → focused regression checks → required wider
checks → record evidence → choose the next slice. Do not claim completion for
hardware, instructor or learner work based on code changes.

Track actual progress in [EXECUTION.md](EXECUTION.md). The detailed capability and
release gates in [RELIABILITY_ROADMAP.md](RELIABILITY_ROADMAP.md) remain applicable;
this document sequences implementation and records deliberate proposal corrections.

## Current checkpoint

17 October 2026: increments **1a and 1b software/host checks complete** in Android
0.3.4. Native 9/9 targets (120 core cases), Java camera-health/readiness and 13
setup cases, Android build, APK checks and the Kochi native regression pass.
Physical-phone acceptance remains open. Samsung S24 Ultra / Android 16 is the
first user-supplied test profile. Next engineering increment: original-recording
sidecars and local evidence export; run the parked setup/device checks when the
phone is available. See EXECUTION.md for exact evidence and limits.
Subsequently, the user reported a stationary setup smoke pass. Increment **2a**
software/export checks are complete in 0.3.5; actual phone export remains pending.
Next: original recording sidecar validation/import and its replay conformance.
Workstream 4a (phone-only gear estimation and separately validated shift coaching)
is now planned; implementation and its two activation gates remain pending.
