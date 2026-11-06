# User screen recording and learner-coaching feedback

Reviewed 19 October 2026. This is an evidence review, not a release approval or
an implementation of the instructions embedded in the pasted conversation.

## Materials and method

- User notes: `C:\Users\mariy\.codex\attachments\e418f012-ee18-4262-998b-e75cdb23f3dd\Pasted text.txt`.
- Screen recording: `D:\Downloads\Screen_Recording_20261018_070657.mp4`.
- Duration 292.838 seconds; 1080 x 2340; variable-rate H.264 with stereo AAC.
- Inspected frames every 10 seconds across the recording, plus one-second samples
  at 01:05–01:16, 01:54–02:05 and 04:04–04:15. Times are approximate media times,
  not native sensor timestamps. This is sampled visual review, not continuous
  listening or an exhaustive annotation of every encounter.
- Extracted evidence is under `build/device-review/2026-10-18-screen-recording/`.
  Crops remove the recording's black margins; they do not repair or change detections.
- Compared the display and wording with current Android/C++ source, learner
  coaching documentation and the master plan. No matching diagnostic JSONL for
  this recording was supplied. The earlier setup-only JSONL is a different capture.

## Main assessment

The user's central product criticism is valid: noticing objects and repeatedly
announcing traffic is insufficient evidence of useful learner coaching. Shorter
attention phrases improve restraint, but do not by themselves teach preparation,
judgment or vehicle technique. The product needs a small number of well-evidenced
lessons that arrive early enough, account for what the driver is already doing,
and can be explained after the drive.

## What is visible

| Approximate time | Observation | Interpretation and limit |
| --- | --- | --- |
| 00:00 and throughout sampled frames | `Advice 30.0 km/h (manual fallback)` | This capture does not demonstrate measured speed. An assumed speed must not become evidence for a speed-specific instruction. The recording cannot establish why measured speed was unavailable. |
| Throughout sampled frames | `Level reference not set. Inclination coaching is off` and `Waiting for fresh phone motion; semantic detections only` | The screen reports limited operation. Current code uses the latter message when a usable capture-aligned gravity vector is missing; this does not identify the underlying sensor fault. No calibrated road geometry or inclination success can be credited to this recording. |
| 00:40, 01:20, 02:10 and other samples | Obstacle text remains while the UI reports zero road users or no current road user | Warning history is presented like a current instruction. Current MainActivity also retains the last accepted phrase until a later accepted phrase/source change. Screen persistence is not proof of repeated audio. |
| 01:58–02:00 | A large box covering the left building/frontage becomes `Truck 73%` at 02:00 | A clear semantic false positive. At 02:00 the prominent phrase is `Obstacle in your path. Slow down and be ready to stop`. Their association is suspicious, but this screen capture cannot identify the exact accepted decision's actor. |
| 04:07–04:10 | Right-side roadside structure/roofline receives bus/truck boxes; `Truck 61%` at 04:10 | Another false detection. The prominent text changes from traffic/gap wording at 04:06 to obstacle/path wording at 04:07. Exact causal attribution still needs decision/frame evidence. |
| 01:58 and 01:59 | The status explicitly mentions `global audio cooldown or duplicate lock` and `camera guidance cooldown` | The pasted claim that this app has no cooldown is contradicted by the recording itself. Existing suppression can still be insufficient or poorly tuned. |
| Across the reviewed samples | `Traffic ahead`, `Road user ahead`, and long obstacle phrases dominate; road status remains unclear | No observed demonstration of hump recognition, crossing recognition, sign subtype recognition, intended-turn anticipation, measured gear, or a useful maneuver lesson. Absence of a demonstration is not an exhaustive recall measurement. |

The preview is very small inside the monitoring layout and surrounded by unused
space. The display also mixes the last instruction with current diagnostic
status. A learner should be able to distinguish current advice, past advice and
limited/unavailable capabilities immediately.

## Build and audio uncertainty

The recording uses legacy camera wording. Current 0.3.6 CameraCoach selects the
new person/two-wheeler/vehicle attention phrases, separately gated approach/gap
advice and research escalation; ordinary camera detections do not select the old
obstacle/path phrases. This suggests the recording predates the current camera
policy or comes from a different binary. Its installed version is not visible in
the inspected frames. Do not classify it as a verified 0.3.6 regression, and do
not claim 0.3.6 would fix these particular new scenes without testing them.

The user reports hearing the voice physically. Accept that report; missing voice
in a screen recording is not evidence that speaker output failed. The file has
non-silent AAC audio (FFmpeg PCM16 volumedetect mean -19.6 dB, peak approximately
0 dB), but those statistics do not identify app speech or establish audible onset.

Current AudioWorker labels output as `USAGE_ASSISTANCE_NAVIGATION_GUIDANCE`.
Android's ordinary AudioPlaybackCapture API restricts eligible playback usages to
MEDIA, GAME or UNKNOWN. This is a plausible explanation for uncaptured app voice,
not a verified diagnosis of Samsung's built-in recorder or this installed binary.
See [Android's playback capture documentation](https://developer.android.com/media/platform/av-capture).
Do not change production audio usage solely to make a screen recorder capture it.

## Assessment of the pasted implementation advice

The direction toward road features, relevant trajectories and anticipatory
coaching is useful. Several proposed details should not be implemented as written:

- Speed and slope do not uniquely identify the engaged gear. A speed-band table
  is a recommendation heuristic, not gear measurement. The existing master plan
  already requires car-specific RPM/speed evidence, independent labels and unknown
  states for the proposed phone-only gear research.
- A fixed `gear 2`, `15 km/h` or clutch sequence for every hump/turn is unsupported.
  Knowing the feature alone does not establish the appropriate vehicle action.
  Current gear recognition and shift recommendation require separate validation.
- A road bend is not the same as the driver's intended junction turn. Intended
  maneuver coaching needs route/instructor/user intent in addition to geometry.
- Current semantic classes do not supply speed humps, zebra crossings or the
  proposed sign subtypes. Adding an enum or an audio file does not add perception.
- Bounding boxes plus a pinhole formula do not automatically supply reliable
  metric distance. Camera calibration, pose, geometry assumptions and errors need
  explicit evidence. A constant curvature threshold without units is not a policy.
- GPS/IMU fusion does not guarantee instantaneous, accurate speed through outages.
  Freshness, drift, uncertainty and provenance remain necessary.
- The pasted file names/pseudocode do not consistently match this repository.
  Tracking and global cooldowns already exist. A verified drivable corridor and
  robust parked/oncoming distinction remain unresolved.
- A model architecture cannot confer certification. No instructor endorsement,
  independent learner benefit or certification is established by this recording.

## Recommended next execution order

1. Establish the installed build and obtain a matching active-session diagnostic
   capture. Investigate missing measured speed and capture-aligned motion from
   actual telemetry rather than guessing from this screen.
2. Make monitoring status honest: distinguish last advice from active speech,
   expire obsolete instructions, expose limited operation and remove distracting
   diagnostic clutter from the learner view. Keep full evidence in the lab/export.
3. Add these building/roofline false detections as review cases. Evaluate current
   policy against original camera footage if available; do not feed the entire
   screen recording, including labels/UI, into the road perception benchmark.
   A policy filter is not a replacement for detector/domain evaluation.
4. Define one instructor-reviewed preparation lesson and its measurable inputs,
   timing window, action wording and silence conditions. Bend preparation is an
   existing experimental starting point, but its poor prior geometry availability
   must be resolved before enabling it as a learner feature. If choosing humps
   instead, annotated hump perception is a prerequisite rather than an assumed
   existing capability.
5. Collect separate training/development/held-out sessions for the chosen feature.
   Include lookalikes and misses, driver response, useful lead time and nuisance
   cues. Benchmark perception, policy and learner comprehension separately.
6. Build the short live cue plus an evidence-linked post-drive explanation, then
   assess it with instructors and supervised learners. Continue phone-only gear
   work as research; the main coaching feature must remain useful with gear unknown.

No app code or installed APK was changed during this review. The existing replay
results remain bounded software evidence, not proof that these new failures are
fixed or that learner coaching is effective.
