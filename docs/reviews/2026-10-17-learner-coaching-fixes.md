# Learner camera coaching fixes — Android 0.3.6

The reproduced false obstacle and opposing-traffic cues are removed in the
development replays. Positive road-user attention remains. Ordinary camera speech
now identifies one visible actor and gives one short action, with no box-only
slowing or gear instruction. **This is an engineering improvement, not completed
instructor/learner or road acceptance.**

## Artifacts

- [LearnDrive 0.3.6 debug APK](../../build/LearnDrive-0.3.6-debug.apk), code 19.
- [Updated Indian junction replay with speech](../../build/learner-coaching/runs/jodhpur-clear-semantic-hz8-delay224-r3/guidance-review.mp4).
- [Updated Bangalore replay with speech](../../build/learner-coaching/runs/bangalore-semantic-hz8-delay224-r3/guidance-review.mp4).
- [Selected-actor capture frames](../../build/learner-coaching/accepted-actor-contact.jpg).
- [Audited results](../../build/learner-coaching/aggregate.json) and
  [build/verification identity](../../build/learner-coaching/verification.json).
- [Design, provisional thresholds and learner acceptance requirements](../LEARNER_COACHING.md).

APK: 135,196,409 bytes; SHA-256
`b7dd3a8250f609dbffc55993110a68b924a4f7d879a1d359873115bcbcd6899e`.
Existing debug signing certificate retained. Earlier versioned APKs remain.
Native replay DLL SHA-256:
`144c2b5eca8294c7187c5b5447e017d95ceccbecbd688e2f0be2abf866ab2b1f`.

## What changed and why

- A large first-frame box no longer becomes an obstacle/slowdown instruction.
  Distinct observations, plausible class geometry and a relevant image position
  are required for ordinary attention. Raw detections remain available for review.
  Credible optical approach is separate from image size.
- Far-left/side traffic does not automatically trigger generic traffic advice.
  Sustained or inward-entering actors can receive an attention lesson; this is an
  image-position heuristic, not a general opposing-lane classifier.
- New camera lessons are **Person ahead. Give space**, **Two-wheeler ahead. Give
  space**, and **Vehicle ahead. Keep a gap**. A person detection is not assumed to
  be walking. Overlapping body/bike detections prefer the bike, and weaker bike
  evidence after a rider cue prevents repeating that rider as a new person.
- Camera-only lessons never append engine/gear advice. Ordinary spacing normally
  leaves eight seconds between lessons; a new entering or more vulnerable actor
  can be offered after 3.5 seconds. Global acceptance/duplicate limits and optical
  escalation remain. Equal-priority lessons do not interrupt one another.
- The three new PCM clips last **2.64–2.78 seconds**. Excess digital-zero padding
  was removed while preserving every nonzero sample and internal pause; the
  speaking rate was not increased. Old prompt IDs/assets remain for compatibility.
- Android publishes semantic results before spatial inference and raises the
  maximum semantic cadence from 4 to 8 Hz. Original capture timestamps and the
  350 ms freshness cap remain. Frame records distinguish semantic and combined
  completion. A camera decision records its selected attention-track ID, class,
  evidence kind and capture-frame box. Exact eligible decisions are retained
  before accepted-submission records even when audio becomes free between samples.

No model weights, preprocessing math, research-emergency default or sensor/setup
gates changed. Android is the qualified build target for this increment; iOS
integration/runtime acceptance has not been performed.

## Replay results

Five full source clips, eight final configurations, **7,503 frame steps**. Same
sources as the preceding review; no unseen-test or accuracy claim. Primary inputs
are assumed 30 km/h, six-second startup pre-roll and 224 ms fixed result delay.
These are desktop shared-core replays, not Android hardware measurements.

| Clip, semantic mode | Prior cues | New cues | Prior scheduled speech | New scheduled speech |
| --- | ---: | ---: | ---: | ---: |
| Jodhpur clear | 2 | 2 | 97.70% | 54.39% |
| Jodhpur rain | 2 | 2 | 80.43% | 26.50%* |
| Udacity highway | 1 | 0 | 22.29% | 0% |
| Udacity curves | 6 | 0 | 48.78% | 0% |
| Bangalore | 4 | 3 | 45.27% | 16.44% |

*Rain's second cue starts at 9.99 s of a ten-second source. Its continuation is
not reviewable, and occupancy is clipped at the video end. Do not credit this
number as proof of better timing or reduced real-driver distraction.*

The known road/guardrail false-car cases and across-divider cue do not recur.
Silence on the US winding road is not a bend-recall pass: this semantic source
has no original road geometry, and the clip is outside the initial Indian-road
domain. Missed/late hazards still require independent encounter labels.

Positive preservation is checked explicitly:

- At **1.99 s**, Jodhpur's selected actor is the actual motorcycle/rider, with
  **Two-wheeler ahead. Give space**. Its old body-as-pedestrian cue is corrected.
- At **5.51 s**, the selected capture-frame box matches the visible crossing white
  car, with **Vehicle ahead. Keep a gap**. It replaces the unhelpful repeat of the
  same rider as a person. This is observed actor association in a development
  scene, not a validated crossing-intent, collision-risk or optimal-timing claim.
- Bangalore retains sustained vehicle attention at **1.30 s** and **19.83 s**, plus
  generic person attention at **32.76 s**. The latter actor appears to be a rider;
  the fallback wording correctly avoids claiming they are walking. Whether these
  low-urgency lessons justify interrupting a learner remains an instructor question.

No ordinary cue interrupts another in these eight final runs. Camera cues retain
selected-actor evidence. Intermediate candidate runs are preserved as `r1`/untagged
and `r2`; only **r3** is the final native-policy evidence. This iteration caught
and corrected false wording and a duplicate physical-actor lesson; quieter output
alone was not the acceptance condition.

At the same 224 ms delay and same final policy, Bangalore semantic availability
rises from **46.72% at the old 4 Hz maximum to 93.44% at 8 Hz**. Across the semantic
8 Hz primary runs it is 77.42–94.05%, measured on decision ticks after the first
350 ms and excluding startup. At 500 ms delay it remains zero, with zero video
cues. Increased cadence has not bypassed stale-result rejection.

The 20 Hz spatial sensitivity run uses assumed 110-degree optics/level pose and
has 82.55% semantic availability, three cues and zero road-valid outputs. Its
different sample rate and unknown calibration prevent an isolated model-ablation
or bend-quality conclusion. Actual phone gains, power cost and thermal behavior
remain unmeasured. Cached raw model inference is reused where matching; final
native policy and acknowledgements execute in every replay.

Bangalore's final render reuses an earlier render only after checking identical
event/observation inputs and source identity. Its WAV mix was recomputed from
the current bundled PCM and compared byte-for-byte. Jodhpur's final timeline was
rendered anew. These tracks synthesize scheduled acceptance, not physical audio.

## Verification and reproduction

- **9/9 native CTest targets pass**, including **131 core cases** and 25 dual-brain
  cases. Changed legacy assertions no longer demand first-frame size-based
  obstacle speech; all prior core test cases remain, with new negative and positive
  attention, crossing-entry, identity, rider-association and escalation checks.
- All eight replay/acknowledgement/cooldown/provenance audits and explicit positive
  development regressions pass. Native/tool/policy hashes are checked against
  current source before the checker accepts a saved report.
- Existing camera-health, spatial-readiness, 13 setup cases, journal checks and
  nine session-inspector tests pass. Two PCM-padding tests verify sample retention,
  internal pauses, idempotence and preservation of rejected silent input.
- Final ARM64/x86-64 Android build succeeds. All pinned model/license assets,
  **46 PCM clips**, packaged native runtimes and APK signature verify.
- No ADB device is attached; the installed phone build and active-session
  performance have not been inspected for 0.3.6.

```powershell
ctest --test-dir build/native-spatial --output-on-failure
python tools/check_learner_coaching.py --tag r3
# Repeat the saved matrix with the downloaded clips and rebuilt native DLL:
python tools/check_learner_coaching.py --run --tag r3
python -m unittest discover -s tests -p test_pcm_padding.py -v
```

## Remaining acceptance

The next device check is an explicitly started, parked 20-second session on the
S24 Ultra / Android 16, followed by diagnostic export. Verify early semantic
completion, active decisions, selected actors and actual availability; measure
audio onset and sustained thermal cost separately. Preserve freshness gates.

Instructor labels and supervised learner observation must establish whether cues
arrive usefully, identify the right situation, are understood, and avoid needless
corrections. Image-zone/aspect/temporal heuristics can miss partial actors, curved
road encounters and ambiguous traffic. No precision/recall percentage, learner
benefit, market validation or independent-driving readiness is established here.
