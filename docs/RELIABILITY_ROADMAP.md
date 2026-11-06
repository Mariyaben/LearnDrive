# LearnDrive reliability and learner-product roadmap

Planning baseline: 15 October 2026. First audience, confirmed by the user:
**Android car learners in India**. This document is a proposed implementation
and validation plan; its targets are not achieved results or certification criteria.

The [master action plan, workstream 4a](MASTER_ACTION_PLAN.md#4a-phone-only-gear-estimation-and-specific-shift-coaching--experimental)
now includes experimental phone-only gear estimation. Gear estimation and numbered
shift coaching have separate validation gates; neither capability is implemented
or approved for learner use by that planning addition.

## 1. Product outcome and release scope

Build a learning companion that gives a small number of useful, understandable
cues at the right time, works offline on supported phones, and helps the learner
understand specific improvements after the drive. Prove that it helps without
increasing distraction or dependence on warnings. Feature count, model size,
passing unit tests and a quiet demonstration are insufficient release evidence.

The first supported operating conditions should be defined explicitly: a fixed,
forward-facing rear phone camera in a car, a validated mount, supported Android
phones, daylight, dry conditions and initially urban/suburban routes at 0–60 km/h.
These are proposed pilot boundaries, subject to instructor review and data.
Expand conditions only after separate validation. Low light, heavy rain,
obstructed views and unsupported optics should produce a clear limited-capability
state rather than trusted metric guidance.

Use warning-only coaching language. The app does not control steering, brakes
or throttle and should not be presented as Level 1 vehicle automation. NHTSA's
description separates momentary warning assistance from continuous Level 1
steering or longitudinal control. This is a product-description correction, not
a claim that the app has an automotive safety classification or certification.
[NHTSA driver-assistance descriptions](https://www.nhtsa.gov/vehicle-safety/driver-assistance-technologies).

| Capability | First-release approach | Evidence needed before activation |
| --- | --- | --- |
| Pedestrian/cyclist awareness | Short attention cues for relevant road users | Event precision/recall, proximity/context checks, useful warning timing |
| Following-gap coaching | Enable only with credible tracking, speed and geometry | Calibrated recordings, lead identity, range/closing accuracy and action timing |
| Oncoming-traffic awareness | Conservative clearance/attention wording | Direction classification and trajectory reasoning; do not confuse it with a same-direction lead |
| Bend preparation | Enable for supported geometry with sustained evidence | Marked/unmarked-road results, turns versus curves, held-out timing and direction accuracy |
| Junction awareness | Separate capability to research and validate | Actual junction evidence; curvature or a busy image alone is insufficient |
| Phone inclination | Describe measured mount/vehicle inclination and its limits | Mount stability, level calibration, gyro/gravity checks; no inferred hill ahead |
| Gear coaching | Instructor-reviewed general lessons and conditional advice | Exact shifts remain unavailable without credible live gear/RPM/load data |
| Emergency commands | Internal research until independently reviewed gates pass | Controlled true-threat tests, nuisance exposure, timely detection and human-factors review |
| Post-drive review | Deterministic summaries first; optional SLM wording later | Every statement traceable to an observed event; uncertainty and omissions retained |
| Potholes, rear threats, blind spots | Excluded from first-release claims | Suitable sensing/models and separate evaluation; forward Supercombo does not supply them |

Pilot recruitment should begin with supervised instruction and appropriately
controlled testing. Do not create dangerous traffic encounters to collect data.

## 2. Starting evidence and blockers

The 0.3.1 APK builds and passes the existing native and asset checks. The supplied
Kochi clip has 1,201 frames and only 40 seconds of footage. It is a development
regression clip, not a statistically meaningful safety study or a fresh test set.

The original 110-degree-FOV replay produced 86 utterances, including 84 collision
warnings. The corrected replay produces six utterances including startup, or
five contextual cues with startup completed beforehand and 180 ms of simulated
processing delay. Road geometry passed its gates in only 1 of 1,201 frames.
Junction and later pedestrian cues remain missing. See
[the replay evidence](KOCHI_VIDEO_REVIEW.md).

Current blockers:

1. Supercombo executes, but its useful contribution to guidance is unproven.
2. The current optical confirmation path uses a road corridor derived from
   confident Supercombo lanes. When those lanes fail, optical confirmation and
   neural collision guidance can both be unavailable. Requiring several checks
   to agree is not automatically a reliable fusion design.
3. Camera mode, Android MP4 mode and desktop replay do not yet exercise identical
   application orchestration. Headless C++ success does not prove phone behaviour.
4. Recorded video optics, speed and motion provenance are incomplete. Filming a
   screen through a second camera cannot validate metric depth or vehicle IMU.
5. Phone performance, thermal behaviour, audio onset and learner benefit remain
   unmeasured. The iOS semantic producer is incomplete and outside first-release scope.

## 3. Work packages, dependencies and exit gates

### P0 — Define what learners and instructors will trust

Suggested effort: 3–5 working days, with instructor/learner recruitment continuing
through the project. Product owner plus an experienced local driving instructor.

- Interview approximately 10–15 learners and 3–5 instructors. Ask about situations
  that cause anxiety, confusing feedback, useful vocabulary, phone mounting,
  audio burden, language and situations where silence is preferred.
- Observe existing supervised lessons with consent. Distinguish learner mistakes
  from hazards an attentive driver is already managing successfully.
- Agree on the first supported conditions, feature list and phrases. Choose the
  pilot's language from participant needs; validate translations with instructors.
- Separate public coaching, internal shadow evaluation and simulation modes.
  Experimental emergency commands stay out of an ordinary learner release until
  their separate validation gate passes.
- Record unsupported claims explicitly: crash prevention, exact safe speed,
  automatic vehicle control, rear visibility, unmeasured gear/RPM or hill-ahead
  knowledge. Suppress reassuring claims such as “all clear” from an empty detection list.

Deliverables: product brief, feature/capability matrix, instructor-approved phrase
list, operating-conditions definition and interview findings.

Exit gate: a learner and instructor can explain what the app can detect, what it
cannot detect, and when its guidance is unavailable.

### P1 — Make every test exercise the production pipeline

Suggested effort: 1–2 engineering weeks. Android engineer plus C++ engineer.

- Introduce a common source contract for live camera, recorded video and replay:
  capture/completion timestamps, frame ID, pixel format/range, crop, rotation,
  physical camera ID, calibration ID and measurement provenance.
- Carry speed, IMU, accuracy and freshness separately. Support recorded sidecars
  for speed/IMU/calibration. Mark manual speed and estimated intrinsics clearly.
  Never apply the phone's camera optics or stationary motion to a recording as
  though they were the original vehicle's measured sensors.
- Route Camera and MP4 scenes through one decision coordinator, source lifecycle,
  freshness policy and audio acceptance path. Keep Desk as an explicit simulation.
- Add an internal diagnostics view with original frame, exact warped model image,
  packed-channel inspection, horizon/road projections, tracks and confidence.
  Keep tensor/provider details out of the learner's driving screen.
- Log each candidate, suppression reason and accepted prompt: source model,
  object/episode ID, capture age, uncertainty, driver action, cooldown and audio
  rejection. Record the origin of every spoken cue so Supercombo's contribution
  can be measured directly.
- Verify video PTS, sensor and app monotonic-clock conversion through seeks,
  pauses, background/resume, camera changes and long inference gaps.

Deliverables: shared source/decision interface, synchronized recorder, diagnostic
overlays, replay conformance tests and machine-readable event traces.

Exit gate: the same recorded inputs and clock schedule produce the same native
decisions in desktop replay and Android MP4 tests. Separately verify real audio
and camera lifecycle behaviour on a phone; do not substitute simulated playback.

### P2 — Establish a labelled benchmark before further tuning

Suggested effort: 2–3 weeks initially, parallel with P1; annotation continues later.
Data/QA owner and instructors must participate.

- Start with 30–50 diverse clips to debug the annotation/evaluation workflow.
  Grow toward several hours of consented local footage and at least 500 labelled
  encounters for a pilot benchmark. These are practical starting quantities,
  not proof of adequate emergency-system exposure.
- Include quiet roads and harmless close passes as well as true hazards: parked
  cars, oncoming traffic, pedestrians at edges/crossing, bicycles, motorcycles,
  auto-rickshaws, buses/trucks, unmarked roads, turns, junctions, occlusion, glare,
  vibration and unsupported night/rain conditions.
- Label encounter type, track, motion direction, road/path relation, whether a cue
  is warranted, earliest reasonable and latest useful cue times, acceptable phrase
  classes and uncertain cases. Do not label exact depth from visual impressions.
- Use measured geometry or suitable calibrated reference data for distance/TTC
  evaluation. Use controlled recordings/simulation for dangerous scenarios.
- Have two reviewers independently label an overlap sample, resolve disagreement
  with an instructor and report agreement. Preserve uncertain labels.
- Split by recording session, route, camera and vehicle rather than adjacent
  frames. Keep a locked held-out set. The Kochi clip already informed development
  and cannot serve as an untouched test set.
- Version model/calibration/code/configuration with every result. Cache inference
  for policy experiments and invalidate caches when relevant model/preprocessing changes.

Deliverables: dataset manifest, annotation schema, provenance/licence records,
reviewed labels, locked test split and automatic comparison reports.

Exit gate: evaluate useful and harmful guidance per encounter, including missed
and late cues. Frame count and total utterance count are not accuracy metrics.

### P3 — Diagnose Supercombo and choose perception from evidence

Suggested effort: 2–3 engineering weeks after initial P1/P2 outputs. Perception/C++
engineer with calibrated recordings and real-phone access.

- Freeze the pinned model and establish a golden upstream comparison on recordings
  with known camera geometry. Native/Python agreement currently proves agreement
  with our reference; it does not prove our shared assumptions match upstream use.
- Audit pixel range, YUV channel order, UV offsets, interpolation/rounding, border
  handling, crop/rotation, pixel-centre convention, source resizing and the complete
  upstream camera-to-model transform. Save exact visual inputs beside predictions.
- Audit output offsets, units, axes, uncertainty, lead selection, absolute versus
  relative velocity, state update/reset and traffic convention against the pinned
  upstream implementation. Add semantic golden fixtures, not only finite-output tests.
- Separate 30 FPS camera capture from the model's temporal contract. The pinned
  upstream model declares 20 Hz. Test correct 20 Hz temporal sampling and separately
  quantify dropped/irregular frames; do not assume feeding 30 Hz is equivalent.
  [Pinned upstream contract](https://raw.githubusercontent.com/commaai/openpilot/6ca0269f47d048600335ac5776fc7966d9ca339f/selfdrive/modeld/models/driving.h).
- Verify focal length, distortion correction, physical lens selection, camera
  height/mount assumptions, crop and stabilization. Maintain distinct measured,
  estimated and unknown calibration states. Gyro/gravity pitch/roll cannot recover
  arbitrary yaw or establish road grade by themselves.
  [Android camera calibration metadata](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#LENS_INTRINSIC_CALIBRATION).
- Compare calibrated native recordings, downscaled versions and filmed-screen
  copies to isolate resolution, lens and domain effects. Do not tune an arbitrary
  FOV to make one clip look correct.
- Compare Supercombo alone, detection/tracking alone, current gated fusion and
  candidate fusion on the same held-out encounters. Measure each model's useful
  contribution, missed hazards, supported coverage, latency, energy and memory.
- Evaluate road-edge/drivable-area alternatives for unmarked roads, including a
  measured fallback using existing assets. Do not depend solely on painted lanes.
  Select a fallback only after its own held-out accuracy and phone cost are known.
- Decide whether Supercombo is suitable for the first supported conditions. If
  it remains unreliable with correct inputs, keep it experimental and choose a
  better-performing baseline. Retraining or adding a model requires a defined
  dataset, benefit, runtime budget and redistribution terms.

Deliverables: root-cause report, golden fixtures, calibration workflow, cadence
tests and a model/fusion comparison with a recorded keep/change decision.

Exit gate: perception improves event-level guidance on held-out routes and
supports its claimed operating conditions. Running Supercombo is not the gate.

### P4 — Build trustworthy scene reasoning and guidance episodes

Suggested effort: 2 engineering weeks once perception interfaces are stable.
C++ engineer plus instructor review.

- Track persistent objects and stabilize classes. Distinguish parked, lead,
  oncoming, adjacent, crossing and departing traffic. Resolve overlapping class
  labels rather than announcing one object twice.
- Represent road-user/path uncertainty, range uncertainty and temporal consistency.
  Box size, class-width distance and one-frame lane overlap are not collision proof.
- Break the current confirmation dependency on failed lane geometry. Evaluate
  alternative optical/trajectory support independently and measure the recall cost
  of each gate. Neural, detector and optical outputs share camera failure modes;
  agreement must not be treated as statistically independent certainty.
- Keep bounded C++ reflexes and deterministic action selection. Establish positive
  tests where a real supported threat is detected, in addition to tests that reject
  unsupported leads. Use controlled/synthetic scenarios for braking escalation.
- Build event episodes: one object can enter, persist, intensify and clear. A class
  label change should not create a new emergency. Test multiple simultaneous hazards,
  increasing urgency, driver response and return to ordinary coaching.
- Retain the current requested 3.5-second non-emergency spacing and 10-second
  duplicate lock as the baseline. Measure missed second encounters and stale
  queued cues caused by these rules; propose any changes explicitly with evidence.
- Prefer short attention/action cues, ideally 2–6 words where clear. Explain the
  reason after the drive. Reserve urgent commands for validated circumstances and
  avoid steering/swerve instructions from an incomplete forward view.
- Recheck conditions at speech onset, drop expired cues and measure time until the
  action words are audible. A fast native decision can still produce a late sentence.
- Preserve current driver-action thresholds as the baseline; measure mount error,
  braking/turning/shock confusion and stale-sensor behaviour on controlled drives.
  Do not infer phone IMU from rounded speed-overlay readings.

Deliverables: scene-quality contract, event-state policy, curated phrase mapping,
positive/negative scenario tests and a before/after event-quality report.

Exit gate: warnings are relevant, timely and stable across distinct hazards,
including actual true-threat cases. Silence alone never passes this gate.

### P5 — Build a learner experience people can use repeatedly

Suggested effort: 1–2 engineering weeks, overlapping late P4. Android engineer,
product owner and instructors.

- Provide three clear stages: prepare, drive and review. Mount/visibility/calibration
  checks happen before movement; a drive needs minimal interaction.
- Make real driving the ordinary mode. Keep filmed-screen tests, simulated speed,
  slope and Desk controls in a clearly separated test area so test settings cannot
  silently masquerade as measured vehicle data.
- Use a simple status vocabulary such as ready, limited view and paused. Explain
  unsupported conditions briefly without repeating spoken diagnostics.
- Build a deterministic post-drive timeline with a few evidence-backed learning
  points, selected examples and a practice goal. Show uncertainty and gaps. An
  optional SLM can phrase verified structured observations after stopping; it
  cannot invent mistakes, operate live speech or create an unsupported safety score.
- Add user-controlled local session history, export/delete actions and concise
  instructor review. Default to local processing and minimal retention; recording
  and diagnostic sharing require clear user choices. Redact sensitive exports.
- Test cue comprehension, voice intelligibility, language, stress and whether
  guidance distracts from an instructor. Make audio mute/pause easy before driving.
- Provide guided practice lessons that remain useful without perfect vision,
  with instructor-approved explanations rather than automatic claims of mastery.

Deliverables: learner flows, readiness checks, local review timeline, language/voice
tests and evidence from observed usability sessions.

Exit gate: learners can set up correctly, understand the cue and identify a useful
post-drive learning point without operating a complex interface while moving.

### P6 — Qualify supported Android phones and release operation

Suggested effort: 1–2 weeks initially, with regression runs throughout. Android/C++
engineers and QA with physical devices.

- Select 3–5 actual ARM64 phone models across the intended price/performance range;
  identify them with pilot participants rather than assuming all Android phones work.
- Profile capture, preprocessing, each model, tracking, decisions, audio acceptance
  and audible onset separately. Record p50/p95/p99/max, drops, capture age, memory,
  battery use, provider/operator placement and thermal status.
- Retain the requested XNNPACK/two-thread baseline, then benchmark provider and
  thread-pool variants. XNNPACK and ORT use separate thread pools; official guidance
  recommends benchmarking their interaction, including ORT intra-op one-thread
  configurations when XNNPACK handles most compute.
  [ORT XNNPACK configuration](https://onnxruntime.ai/docs/execution-providers/Xnnpack-ExecutionProvider.html).
- Benchmark QNN only where the provider, backend and operators actually support
  the model. Compare numerical and event-level results and sustained performance;
  reject acceleration that changes guidance quality or silently partitions badly.
- Run 45–60 minute sessions, including warm ambient conditions and the intended
  mount/charging setup. Use thermal status/headroom where supported and maintain
  thermal protection. A phone that cannot sustain the advertised capability is
  unsupported or clearly receives a separately validated reduced mode.
  [Android Thermal API](https://developer.android.com/games/optimize/adpf/thermal).
- Test incoming calls, navigation audio, Bluetooth routing, denied audio focus,
  lost GPS/IMU, dirty/blocked camera, permission changes, low storage, interruptions,
  seek/EOF, background/resume and recovery. Test actual recorded PCM output timing.
- Produce signed release builds, a model/dependency inventory, reproducible hashes,
  regression evidence and rollback instructions. Verify installed-app behaviour.
- Resolve the bundled YOLO AGPL/commercial distribution path before choosing a
  proprietary release strategy. Review model, voice and dataset terms as well.
  The vendor documents distinct AGPL and Enterprise paths; obtain a qualified
  project-specific assessment rather than assuming ONNX export removes obligations.
  [Ultralytics licensing](https://www.ultralytics.com/license).

Deliverables: physical-device qualification matrix, sustainable profiles, release
candidate, installation/recovery evidence and resolved distribution decisions.

Exit gate: every advertised phone/feature combination meets measured budgets;
degraded operation is visible and is not counted as successful full-capability use.

### P7 — Validate usefulness with instructors and learners

Suggested effort: 3–4 weeks for an initial pilot, following prior gates. Product
owner, instructors, QA and 20–30 consenting learners as a feasibility cohort.

1. Run shadow mode first: record candidate guidance without speaking it. Compare
   it with instructor labels and investigate nuisance/missed/late events.
2. Validate speaking guidance in appropriate controlled/supervised settings.
   Begin with proven attention cues. Do not switch on experimental emergency
   commands merely because ordinary coaching performs well.
3. Compare matched lessons with and without guidance, balancing order/route where
   feasible. Use instructor-rated observations and predeclared learning outcomes.
4. Measure cue understanding, perceived distraction/stress, nuisance warnings,
   setup success, learning-point recall, repeated mistakes and voluntary reuse.
5. Ask instructors to review incidents without knowing the model/version where
   practical. Examine overreliance, not only satisfaction or app-open counts.
6. Fix the most frequent harmful failures, repeat the locked evaluation and expand
   through a small staged release with an incident-report/rollback route.

Deliverables: instructor-reviewed pilot report, learner interviews, changes in
specific practice behaviours, usability results and a go/no-go record.

Exit gate: the app demonstrates useful coaching without unacceptable distraction,
unresolved harmful failure patterns or misleading capability claims. A small pilot
cannot establish crash reduction or general automotive safety performance.

## 4. Proposed evaluation scorecard

The following are initial engineering/pilot targets to refine with instructors
before test-set tuning. They are neither industry standards nor achieved values.
Report confidence intervals and raw event/exposure counts, including per-class,
route, camera and supported-condition results. Consecutive frames from one
encounter are not independent statistical samples.

| Dimension | Initial target or decision rule |
| --- | --- |
| Ordinary coaching relevance | At least 95% of spoken coaching cues judged appropriate by the agreed rubric |
| Supported encounter coverage | At least 90% recall for each enabled coaching category, with no hidden weak category behind an aggregate |
| Warning timing | At least 90% of appropriate coaching cues begin within the instructor-labelled useful window; report late/incomplete phrases separately |
| Nuisance burden | Investigate a target of no more than one instructor-judged nuisance cue per 30 minutes; use realistic route exposure |
| Unknown conditions | Predeclare unsupported conditions; log duration/reason. Abstention during an in-scope true hazard still counts as a miss |
| Harmful instructions | No wrong-direction, fabricated-gear or unsupported manoeuvre instruction in the test suite; any observed case blocks release pending investigation |
| Emergency prompts | Separate gate: controlled true-threat coverage, nuisance exposure, response timing, failure analysis and independent safety/human-factors review; zero false alerts alone is insufficient |
| Native decisions | Preserve zero heap allocations and demonstrate the requested combined <1 ms bound on each supported ARM64 phone under representative load |
| Vision performance | Measure 30 FPS capture separately from model cadence and useful scene-update rate; prove original sustained >=20 FPS requirement or explicitly revise the advertised capability |
| End-to-end latency | Measure capture to first actionable audible words; keep the existing <70 ms trigger target distinct from audible onset and semantic lead time |
| Sustained use | Pass >30-minute original acceptance; target 45–60-minute qualification runs with no silent loss of advertised capability |
| Learner understanding | Initial target: >=90% correct interpretation of enabled phrases in observed tasks |
| Repeat value | Predeclare pilot measures for useful learning points and voluntary reuse; do not present arbitrary app retention as proof of safer driving |

For uncertainty, show precision/recall versus availability and warning lead time.
Never improve a headline accuracy number by retrospectively excluding failed
conditions or by suppressing all difficult hazards. Review alert burden per hour
and misses per encounter together.

## 5. Delivery sequence and resourcing

| Period, indicative only | Focus | Required result |
| --- | --- | --- |
| Weeks 1–2 | P0 + P1; begin P2 recruitment/annotation | Scope, common pipeline, recorder and initial labelled set |
| Weeks 3–5 | P2 + P3 | Calibration/cadence root cause and held-out model comparison |
| Weeks 5–7 | P4 | Measurable positive/negative guidance performance and event policy |
| Weeks 6–8 | P5, alongside stable core interfaces | Minimal driving UI and useful local post-drive review |
| Weeks 8–10 | P6 | Qualified phone profiles and release candidate |
| Weeks 10–14+ | P7 and failure-driven iteration | Pilot evidence and a staged-release decision |

Plan approximately **12–16 weeks for a focused initial release effort**, conditional
on dedicated Android and perception/C++ work, QA/data support, instructor access,
recording/annotation time and physical devices. A single implementer or missing
data/hardware makes this longer. These are planning estimates, not a delivery
promise. Failing an exit gate moves the schedule; a date does not waive evidence.

Role responsibilities:

- Product owner: scope, learner interviews, instructor coordination and release decisions.
- Perception/C++ owner: camera/model audit, tracking/fusion, deterministic policies and replay.
- Android owner: common source adapters, capture, audio, learner UI and performance.
- QA/data owner: annotations, held-out integrity, metrics, device matrix and regressions.
- Instructors: event/phrase rubric, failure severity, supervised evaluation and learning outcomes.

Budget explicitly for mounts/phones, annotation/instructor time, recorded test
sessions and any chosen model/voice licensing path. Select purchases after the
supported-phone requirements and pilot resources are known.

## 6. First ten working days

1. Days 1–2: finalize the first operating conditions and feature matrix; preserve
   0.3.1 as a benchmark and identify all experimental UI/marketing claims.
2. Days 2–4: define event labels and trace schema; annotate the Kochi development
   clip plus contrasting clips, including quiet roads and controlled true threats.
3. Days 3–6: unify Camera/MP4/replay orchestration and add recorded telemetry,
   calibration provenance and per-prompt suppression/source diagnostics.
4. Days 5–8: capture exact warped inputs and execute the upstream calibration,
   YUV/crop and 20 Hz temporal-contract audit on a calibrated recording.
5. Days 7–9: produce the first event-level baseline for Supercombo, detector/tracker
   and current fusion, including missed hazards and time unavailable.
6. Day 10: review evidence with an instructor and choose the next perception fix.
   Build a new APK only with a specific measured improvement and regression evidence.

The first milestone is a trustworthy evaluation loop and a diagnosed perception
failure. A larger feature list or another APK without that evidence does not
complete it.

## 7. Expansion after the first release

Add languages, phone models, weather/night conditions, higher speeds and iOS only
through new evidence and clearly defined support. iOS requires its missing semantic
producer, macOS builds and physical-device validation. Rear monitoring, potholes,
speed-sign interpretation and exact drivetrain coaching need separate sensing/data
work and should not delay a dependable core learning experience.

Learners choosing the app is a product hypothesis to test. Build trust through
predictable guidance, instructor-reviewed explanations, useful practice feedback,
honest limits, offline operation and transparent privacy. Measure preference and
learning outcomes before committing to a pricing or growth strategy.
