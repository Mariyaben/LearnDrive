# Execution toward a dependable learner product

22 October 2026. Target: a supervised driving-practice assistant for Android car
learners in India. Current builds are engineering candidates, not an approved
paid driving coach. Heat optimization is deferred at the user's request.

The latest UI increment is **0.4.8**, with a charcoal map-led dashboard, both
landscape directions, stable Start interaction and automatic level calibration
after a parked/level-ground confirmation. The reproduced Start regression,
bounded waiting/cancellation behavior and 19 stationary Android checks are in
the [Start repair review](../reviews/2026-10-22-start-control-repair.md). These
software checks do not close the road/learner qualification gates below.

## 1. Repair the observed failures — current implementation

Use the frozen [Indian traffic evaluation](../reviews/2026-10-22-indian-traffic-batch.md)
as the development baseline. Preserve the old binary and exact footage for
comparison. Keep the 350 ms evidence bound and 3.5 s ordinary speech spacing.

- Give an independently established new encounter a timely cue without allowing
  tracking-ID churn to defeat repeated-lesson suppression. Recheck freshness and
  actor relevance when audio actually accepts a pending cue.
- Require supported path and motion evidence for following-gap wording. Optical
  growth by itself does not establish a following relationship. Associate rider
  bodies with their visible cycles where evidence supports that association.
- Clear live boxes when current camera evidence expires. Distinguish current
  availability from the caption of a recently delivered cue. Preserve the fixed
  setup controls and destination search.
- Add a concise session review grounded in delivered prompts and measured
  availability. Give topics to discuss with an instructor; do not infer faults,
  driving ability, correct actions, or a safety score from prompt counts.
- Correct replay publication order, timestamp handling, recorded-speed expiry,
  and cache provenance before using replay results as evidence.

Acceptance for this increment: failing-before/passing-after regression cases;
all native groups and Android policy checks; repeated saved-video comparisons;
S24 setup, caption, lifecycle, review and layout checks. These checks establish
specific software behavior, not road safety or learning effectiveness.

## 2. Improve recognition with an event benchmark

The existing 320-pixel detector misses some small/occluded pedestrians. Policy
fixes do not recover people that perception never represents. Retain those
misses in a held-out evaluation set instead of tuning them away.

Build encounter labels with a qualified instructor: visible onset, relevant
actor, actual driving context, useful cue window, acceptable wording and
whether silence is appropriate. Separate daylight/dry proposed support from
night/rain stress cases. Split by source recording/drive, not neighboring frames.
Report event recall, cue relevance, timing, repeats, availability and speech
burden together, including every in-scope missed event and confidence intervals.

Compare legally distributable detector candidates, input resolution and temporal
tracking on the identical frozen split. Select on both recognition and the
device latency/power budget. Do not lower confidence thresholds or increase
resolution globally without measuring nuisance cues and device cost. Keep
public evaluation media outside the APK and training set until rights allow use.

Exit evidence: instructor-reviewed labels and acceptance criteria agreed before
the held-out run, then passing results with enough independent encounters to
support the proposed claim. No numerical safety target is declared achieved.

## 3. Qualify the integrated coach

Use synchronized, consented recordings containing original camera intrinsics,
orientation, capture times, IMU, GPS accuracy and navigation events. Ordinary
internet MP4s cannot qualify metric bends, slope, gear or real warning timing.
Replay those bundles with missing/stale/corrupted inputs, delayed inference,
route cancellation/rerouting, interruptions and lifecycle changes. Never borrow
the replay phone's stationary sensors as original driving measurements.

Run full phone sessions long enough to expose sustained throttling, frame drops,
audio interruptions, charging/sunlight effects, navigation/network loss and
recovery. Compare powered workloads under controlled conditions. Resume heat
work separately; this increment makes no heat/performance improvement claim.

Then run instructor-supervised, consented learner sessions within the proposed
support envelope. Measure cue comprehension, distraction, nuisance, usefulness,
and whether the review changes the next lesson. A successful stationary test
or video replay does not substitute for these observations.

## 4. Prepare the saleable distribution

| Work | Concrete completion evidence |
| --- | --- |
| Model and dependency distribution | Inventory weights/code/data rights; retain notices; resolve the detector distribution model before releasing |
| Privacy and terms | Public policies matching actual camera/location/Google/diagnostic behavior, support contact and retention/deletion; complete store disclosures |
| Maps production access | Production signing identity, separately restricted key, tested failure handling, billing monitoring and verified usage costs |
| Purchase and support | Choose a learning-period offer after pilot evidence; implement store purchase verification, restore, cancellation, entitlement expiry and refund/support flows |
| Release operations | Signed release artifact, integrity/source manifest, supported-device list, staged rollout, incident/rollback procedure and support channel |

The bundled detector is exported from Ultralytics YOLOv8 and ships an AGPL-3.0
notice. Selling software is not prohibited by AGPL. A proprietary release still
needs a resolved licensing path: comply with the applicable source/distribution
obligations, obtain suitable terms, or replace the model and requalify it. Do not
assume the Maps bill is the only commercial dependency cost.
[Ultralytics licensing](https://www.ultralytics.com/license).

Google Places requires public terms/privacy and appropriate attribution. Google
Navigation also restricts persisting road names and speed limits for use outside
the user session. Keep runtime navigation context separate from retained
diagnostics; do not retain restricted provider content merely because a user
enabled diagnostics.
[Places policies](https://developers.google.com/maps/documentation/places/android-sdk/policies),
[Navigation policies](https://developers.google.com/maps/documentation/navigation/android-sdk/policies).

Do not market exact gear recognition, blind-spot coverage, crash prevention,
independent driving competence or validated emergency commands. Activate broader
claims only after their own evidence passes. The next commercial decision is a
bounded supervised pilot with a defined support scope, followed by paid rollout
when reliability, learner benefit and distribution work are demonstrated.
