# Indian traffic evaluation of the 0.4.5 shared engine

The current engine has concrete learner-coaching problems. Its trace contracts
pass, but useful actor selection, timely attention and encounter-specific wording
are not qualified. This evaluation did not change the app or install a new APK.

## What was actually tested

- Android reference: **0.4.5 / version code 25**. Detector and Supercombo asset
  hashes match the latest APK; native/core source hashes remained unchanged.
- **28 Indian excerpts, 750.1516 seconds (12.50 minutes), 22,508 primary frame
  steps**. Twenty-four are newly prepared IDD-PeD raw-video excerpts from six
  recording sets. Four are existing Indian development examples: IDS-JODHA
  clear/rain, Bangalore C V Raman Road and a Kochi-labelled local edited clip.
- **62 configurations, 46,286 frame steps including repeats**: 28 primary
  semantic runs, 24 additional semantic controls and 10 actual Supercombo
  inference runs with explicitly assumed optics. Repeated configurations are
  not additional footage or independent drives.
- All **62 trace/acknowledgement, cooldown, source and missing-input contract
  audits pass**. The full current CTest suite also passed **10/10 groups**,
  including real-model inference. Contract success is not coaching acceptance.
- Fifteen excerpts include pedestrian targets selected from human annotations
  before inference. Independent sampled visual review covered 12 clips and
  capture/speech/context frames for 30 accepted cues.

Primary replay assumes 30 km/h, a fixed 224 ms inference delay, 30 Hz frame
sampling, a maximum 8 Hz semantic schedule and six seconds of startup pre-roll.
The detector actually updates at 7.5 Hz on that regular 30 Hz timeline. Neither
speed nor delay is a measurement of the recording or the latest phone runtime.
Unknown-speed and measured-zero test controls are reported separately.

This is a desktop execution of the current shared production perception/policy,
not a test of the entire APK. Android UI, CameraX/MediaCodec, audio focus, real
audible onset, thermal scheduling and Google services were not exercised. No
phone was connected when ADB was checked. Ordinary MP4 intentionally withholds
original camera calibration, IMU and live navigation; silence for bends, slopes
or exact gear shifts is therefore not evidence of a corresponding video miss.

The 24 new video files are correlated within six recording sets. Several are
night scenes; two share the same junction. One source has only a 4.5-second usable
excerpt. This is a larger exploratory development corpus, not a representative
Indian-road safety benchmark. Night/rain stress evidence is separate from the
proposed first daylight/dry support envelope.

## Problems found

| ID | Evidence status | Problem and learner consequence |
|---|---|---|
| F01 | Confirmed policy defect, real-video correlation | A new pedestrian's cue can expire while the global repeated-phrase lock blocks it. The camera's actor-entry policy and speech deduplication policy disagree. |
| F02 | Visually confirmed actor; relevance requires instructor review | Optical approach cues select roadside/side-facing vehicles without establishing a same-path lead or useful clearance action. |
| F03 | Visually confirmed wording weakness | A laterally crossing car receives following-style gap wording. The road user is real, but the lesson does not express the situation. |
| F04 | Reproduced with unknown speed; known-zero control correctly quiet | Apparently unchanged queued rider receives the same person lesson twice, with different track IDs. Identity instability can create repeated teaching. |
| F05 | Confirmed selective recognition-coverage limitation | Small/distant, occluded and poorly lit pedestrians are not consistently represented in retained detections. It is not valid to treat quiet output as an all-clear signal. |
| F06 | Confirmed sampled classification/association limitation | Rider bodies sometimes receive generic person wording and consume the same person-cue category used for pedestrians. |
| F07 | Measured availability limitation, correct stale rejection | At 500 ms fixed processing delay, every tested clip has zero fresh semantic availability and zero video cues. Useful service needs a latency/availability budget; loosening freshness is not a fix. |
| F08 | Unqualified geometry, not measured bend accuracy | The same footage's road-shape validity changes drastically with assumed optics. Generic public MP4 cannot qualify metric bend timing. |

### F01: different pedestrians compete for the same phrase lock

A deterministic current-DLL test accepts a person cue at 3.50 seconds. A different
person enters the attention zone at 7.27 seconds. Camera policy permits an
actor-specific new entry after 3.5 seconds, but global audio rejects the repeated
phrase for ten seconds. At 8.77 seconds the entry's 1.5-second eligibility window
expires, so this particular pending lesson disappears even though the person
remains. Moving the equivalent entry past the lock allows it to speak.

There is related real-video evidence in **gp_set_0003_vid_0005**: target
**gp_6945**, detected before its annotated crossing onset, is selected as actor
39 at **6.10–6.21 seconds** and blocked by cooldown/duplicate lock. The selected
box has IoU 0.83 with the human target. Another person was announced at 0.36
seconds. This confirms target-associated blocking, but not that every quiet
tick in that encounter has the same cause or requires another lesson.

Source mechanism: `core/src/decision.cpp` actor entry/spacing and
`core/src/c_api.cpp` global duplicate lock. A future fix should coordinate actor
episodes and pending audio, revalidate fresh relevance before any delayed cue,
and retain calmness and the 350 ms evidence limit.

[Synthetic reproduction](../../build/traffic-review-2026-10-22/audit-ui/synthetic-policy-results.json),
[real-video actor trace](../../build/traffic-review-2026-10-22/audit-ui/gp3v5-target-arbitration.json).

### F02/F03: real objects, weak encounter understanding

- **gp_set_0005_vid_0001, 4.76 and 16.36 seconds:** "Keep a safe gap" selects
  roadside vehicles, actors 9 and 56, as the recording vehicle passes them.
  Their optical expansion is not proof of a following or collision relation.
- **gp_set_0001_vid_0002, 15.03 seconds:** "Vehicle ahead. Keep a gap" selects
  a blue car moving laterally across the view, actor 144. A crossing-specific
  lesson and acknowledgement that the driver may already be waiting are absent.

These are not phantom detections or proved dangerous commands. They demonstrate
that selecting a real box is insufficient to teach the right situation. The
current forward image corridor does not reliably distinguish parked, opposing,
crossing, sidewalk and ego-path traffic. Clearance, driver response and instructor
labels are needed before prescribing a stronger action.

### F04/F06: physical identity and rider association

**gp_set_0001_vid_0004, 4.23 and 14.23 seconds:** the apparently same seated rider
receives "Person ahead. Give space" twice, as actors 41 and 133. The unknown-speed
control reproduces both cues. **The zero-speed control correctly produces no
ordinary cues**, so this is not proof that known-stationary road mode nags.
It is an identity/repetition regression candidate under missing speed.

Rider-body person cues also occur in gp_set_0001_vid_0002 at 18.63 seconds and
gp_set_0008_vid_0001 at 28.90 seconds. The word "person" is literal, but weaker
than identifying the rider and can occupy a deduplication category needed by a
different pedestrian. Improve physical-actor association rather than relabeling
every person as a rider or announcing more boxes.

### F05: selective human-label coverage

At person score >=0.5 and one-to-one IoU >=0.5:

| Fifteen preselected concordant pedestrian targets | Count |
|---|---:|
| Included targets | 15 |
| Ever matched by retained person detections | 11 |
| Matched on two observations at least 120 ms apart | 11 |
| Individually linked "Person ahead" cues | 2 |

The two linked person cues start 2.37 and 1.23 seconds after annotated crossing
behaviour begins. **This is not a safety-recall or unsafe-lateness score.**
Crossing onset is not lane-conflict time, seven targets have no pre-onset
annotated lead-in, other actors' lessons can cover a group, and the annotations
are selective. Four targets have no qualifying retained person match even with
IoU relaxed to 0.3. Their median letterboxed model-input heights are approximately
18.5, 9.9, 4.7 and 8 pixels; some are distant, briefly present or occluded.

Independent visual sampling also finds a light-colored lead car in
**gp_set_0008_vid_0003 at 2 and 7 seconds** with no retained decoded box at those
times. That establishes nighttime recognition weakness, not an imminent missed
hazard or a requirement to speak for every visible vehicle.

[Human-label audit and definitions](../../build/traffic-review-2026-10-22/audit-ui/AUDIT_FINDINGS.md),
[coverage data](../../build/traffic-review-2026-10-22/audit-ui/pedestrian-coverage.json),
[GT/prediction examples](../../build/traffic-review-2026-10-22/audit-ui/pedestrian-examples-2.jpg).

### F07/F08: availability and calibration

Primary fresh-semantic availability is roughly 93–94% of decision ticks after
the first 350 ms under the assumed 224 ms delay. All five 500 ms delay controls
correctly withhold video cues. Boundary-adjacent 350 ms runs have only 0.9–1.9%
fresh ticks due to floating-point/tick alignment; this is not useful availability.

Ten spatial sensitivity runs execute actual Supercombo inference. On the same
Bangalore footage at 20 Hz, 70-degree assumed HFOV gives 431/967 valid road-shape
frames; 110 degrees gives 0/967. Neither FOV is measured, and neither result is
bend accuracy. Spatial tests also use a different analysis cadence from the
primary semantic runs, so a semantic/spatial cue difference is not an isolated
model ablation.

## Useful behavior preserved

- Known-zero-speed controls are quiet in all seven tested clips.
- All five 500 ms stale-evidence controls are quiet.
- No confidently phantom selected object was found among the 30 visually sampled
  accepted cues. This is not a false-positive rate.
- The native synthetic check gives one ordinary lesson for a steady central
  car over 60 seconds; it rejects one-frame detections and far-left vehicle
  attention. Not every silent actor is a defect.
- The primary corpus has 69 video cues, occupying 163.62/750.15 seconds (21.81%)
  of scheduled playback. The ten-second clear Jodhpur excerpt reaches 54.39%.
  Speech occupancy is not a distraction/comprehension score and needs instructor
  and learner review together with missed useful opportunities.

## Capability gaps identified separately

The current camera decoder retains person, bicycle, car, motorcycle, bus and
truck. Cattle/dogs, potholes, speed breakers, traffic signs and traffic-light
state do not have qualified coaching producers in this tested path. This is a
source-level capability gap, not a newly counted video miss.

The Google turn feed currently does not provide qualified bend voice or exact
distance/numbered-gear instructions. Real road-mode gear/RPM estimation is not
implemented. Video without original telemetry cannot prove safe gear choice,
road grade, precise turn distance or the driver's response.

## Better testing without live road driving

1. **Create an instructor-labelled encounter benchmark.** Label actor identity,
   ego relevance, what the learner should do, earliest/latest useful cue time,
   and when silence is preferable before reviewing model output. Separate
   recordings by whole journey/session to prevent leakage. Include positives,
   ordinary traffic, parked/oncoming vehicles, unmarked roads, groups, occlusion,
   small actors and difficult lighting. Report useful-encounter misses, irrelevant
   cues/hour, lead time after the full phrase, repeated lessons and availability.
   These current discovery clips become regressions, not future held-out data.
2. **Add synchronized recording replay.** Replay original frame timestamps,
   intrinsics/crop, GPS speed/accuracy, IMU, brake information where available,
   and navigation events through a strictly labelled test adapter. Keep semantic
   and spatial completion times separate and reproduce frame-drop bursts. The
   current ordinary MP4 path cannot supply these inputs.
3. **Run the actual APK on a stationary phone.** Recorded-input tests can check
   MediaCodec, JNI/ORT, UI, voice focus, Bluetooth, background/resume and a
   sustained workload without driving. Ordinary MP4 currently skips metric
   Supercombo, so that alone cannot qualify the full live thermal workload.
   Use a calibrated bundle adapter before claiming full-pipeline equivalence.
   [Android Macrobenchmark](https://developer.android.com/topic/performance/benchmarking/macrobenchmark-overview)
   supports repeatable app/UI timing and trace capture.
4. **Use Google's navigation simulator for map integration.** Test route
   progress, maneuver changes, arrival, reroutes, connectivity loss and camera
   speech contention. The SDK simulator moves the navigation feed; it does not
   automatically supply the app's independent GPS/IMU gates or road video.
   Supply explicit test fixtures for those and test stale/unavailable states.
   [Official simulator](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/Simulator).
5. **Use deterministic simulation for rare/conflicting cases.** CARLA fixed-step
   synchronous camera/actor data and ScenarioRunner can exercise lead braking,
   crossings, red-light conflicts, curves and visibility changes with known
   geometry. Save sensor outputs alongside recorder logs and build suitable
   keep-left/Indian scenarios. Default simulated towns are not evidence of
   Indian-road performance. [Synchronous timing](https://carla.readthedocs.io/en/latest/adv_synchrony_timestep/),
   [supported scenarios](https://scenario-runner.readthedocs.io/en/latest/list_of_scenarios/).

The highest-value next work is the coordinated actor/audio regression and a
labelled, synchronized production replay path. Increasing the number of ordinary
MP4s alone cannot establish integrated human-coach usefulness.

## Evidence, provenance and reproduction

- [Roadside-vehicle gap cues with scheduled app speech](../../build/traffic-review-2026-10-22/runs/gp_set_0005_vid_0001-semantic-manual30-delay224/guidance-review.mp4)
- [Crossing-car wording with scheduled app speech](../../build/traffic-review-2026-10-22/runs/gp_set_0001_vid_0002-semantic-manual30-delay224/guidance-review.mp4)
- [Repeated rider lesson with speed unknown](../../build/traffic-review-2026-10-22/runs/gp_set_0001_vid_0004-semantic-unknown-delay224/guidance-review.mp4)
- [Pedestrian arbitration example with scheduled app speech](../../build/traffic-review-2026-10-22/runs/gp_set_0003_vid_0005-semantic-manual30-delay224/guidance-review.mp4)
- [Full decode, audio/video presence and media hashes](../../build/traffic-review-2026-10-22/media-verification.json)
- [Aggregate of all 62 configurations](../../build/traffic-review-2026-10-22/aggregate.json)
- [Version/model/source protocol](../../build/traffic-review-2026-10-22/evaluation-protocol.json)
- [Source stability and APK asset match](../../build/traffic-review-2026-10-22/source-stability.json)
- [Visual findings, exact actors and frames](../../build/traffic-review-2026-10-22/visual-audit/VISUAL_REVIEW.md)
- [Replay fidelity and capability audit](../../build/traffic-review-2026-10-22/HARNESS_AUDIT.md)
- [Prepared-source windows, transformations and hashes](../../build/traffic-review-2026-10-22/prepared-iddped.json)

Each run has its exact command, log, observations, decisions and accepted audio
timeline. The latest APK was not rebuilt or modified for this task. Four of the
28 attempted new raw-video preparations failed/time-limited and are excluded;
the resulting valid corpus has 24 new excerpts, not 28. The selected tar members
would total tens of gigabytes in full; bounded HTTP ranges fetched about 1.8 GB
instead. Failed partial MP4 files are not test results. The edited local
Kochi-labelled 35.3-second clip differs from the earlier 40-second regression;
old timestamps are not transplanted. Earlier aspect-stretched preparation
experiments are archived under `initial-aspect-sensitivity` and excluded.

Sources: [IDD-PeD author repository](https://github.com/Ruthvik9/IDD-PeD),
[author dataset card](https://huggingface.co/datasets/Ruthvik9/IDD-PeD),
[IDS-JODHA](https://shaaashvat.github.io/ids-jodha/),
[Bangalore source and CC0 attribution](https://commons.wikimedia.org/wiki/File:Bangalore_20220814_154815.webm).
The IDD-PeD card has provisional non-commercial/placeholder-license wording;
this is local diagnostic evidence, not commercial training or redistribution
approval. No footage or models were added to the APK.

```powershell
python tools/prepare_indian_corpus.py --count 28 --seconds 24 --workers 4
python tools/run_indian_corpus_review.py --workers 2
python tools/run_indian_corpus_review.py --extra --workers 2
python tools/run_indian_corpus_review.py --control-clips gp_set_0001_vid_0004 gp_set_0005_vid_0001 --workers 2
python build/traffic-review-2026-10-22/audit-ui/evaluate_pedestrian_coverage.py
python build/traffic-review-2026-10-22/audit-ui/synthetic_policy_audit.py
```

The preparation helper needs the retained archive-index manifest and frozen
annotation files in the evidence directory. The first runner's watch target
can optionally wait up to its configured timeout while preparation is running.
The documented commands run preparation and evaluation sequentially; to reproduce
only the retained results, omit preparation. Use a fresh output
parent for a genuinely new-code qualification run; the old generic inference
cache key does not encode every binary/runtime dependency. Frozen current
inference is reused only for identical-input controls in this evaluation.
The new batch runner rejects changed code/model/binary fingerprints and changed
video hashes when resuming existing results.
