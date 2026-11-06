# Learner guidance changes — Android 0.3.6 candidate

Scope: fix reproduced false obstacle/opposing-traffic cues, long ordinary camera
speech, and semantic freshness gaps. Preserve original timestamps, braking/shock
suppression, source isolation and the 350 ms freshness cap. No claim of learner
benefit or road acceptance is made from software tests alone.

## Intended learner behavior

One relevant actor and one comprehensible action per ordinary cue:

- Person ahead. Give space.
- Two-wheeler ahead. Give space.
- Vehicle ahead. Keep a gap.

These are attention lessons, not claims of collision risk, lane identity or a
measured safe following distance. Camera-only cues never append gear/engine
instructions or request slowing merely because a box looks large. Strong,
credible optical approach remains a separate gap action; research emergency
commands remain off by default. Existing slope/engine/bend policies are outside
this camera wording change and retain their separate input gates. A COCO person
can also be a rider, so the generic person cue does not claim they are walking.
When a body overlaps a detected bicycle/motorcycle, the bike actor is preferred.
After a rider cue, a weaker overlapping bike detection also prevents the same
body being reintroduced as a new person cue. A first ambiguous person is still
eligible for the generic person lesson.
New attention recordings retain every nonzero PCM sample while removing excess
digital-zero end padding, leaving normal start/end margins and internal pauses.

## Implementation and provisional settings

The camera coach matches same-class boxes by overlap across distinct captured
observations. Repeated 100 Hz rule evaluations cannot mature one detection.
Vulnerable road users need at least two observations spanning 120 ms; routine
vehicles need at least three spanning one second. Missing/stale frames cannot
rearm an announced encounter. Two seconds of fresh observations with no relevant
actor rearm ordinary attention. These settings require instructor/device review.

Class score and broad pixel-aspect plausibility checks exclude weak/implausible
camera advice while preserving raw detector boxes for diagnosis. A car's allowed
pixel aspect is 0.6–3.3; other supported classes have broader category-specific
ranges. This can withhold useful advice for partial, occluded or unusual actors;
it is not a detector accuracy fix or permission to claim all false boxes solved.

The normal vehicle attention zone uses image center 0.32–0.68 and foot below
half-frame. A vulnerable actor moving inward may receive attention earlier using
a short image-motion projection. This is a forward-camera relevance heuristic;
it cannot generally distinguish opposing lanes, road edges or sidewalks.
Actor ID, category, evidence kind and capture-frame box accompany camera decisions
so each cue can be inspected rather than credited to a convenient nearby event.
IDs identify source-local camera attention tracks, not independently labelled actors.

Routine camera cues normally have eight-second spacing; a newly relevant more
vulnerable class or a sustained actor entering the attention zone may be offered
after 3.5 seconds. New-entry vehicle attention does not wait for a one-second
general traffic lesson. Equal-priority attention clips
do not interrupt one another. Global audio acceptance/duplicate limits remain.
Verified approach may escalate independently; learner attention must not hide a
credible timely action. Stationary measured speed suppresses routine camera action
cues; unknown speed is not treated as stationary or as evidence for slowing.

Android publishes semantic detections before spatial inference, at up to 8 Hz
instead of 4 Hz. Heavy inference remains outside the fusion/rules lock. All
timestamps remain capture times. Final sampled frame records add semantic
completion time and processing duration, distinct from combined frame completion.
The diagnostic header declares the new schedule/policy. Every accepted submission
now retains its exact eligible decision even when audio becomes free between
ordinary sampled decision records. Actual phone speed, overhead and thermal cost
remain measurements to obtain.

## Acceptance evidence required

1. Reproduce the known false road/guardrail and far-left vehicle cases; confirm
   they no longer produce false driving instructions, keeping raw detections.
2. Preserve positive vehicle, two-wheeler and crossing-pedestrian attention, optical
   escalation, rejected/future/stale input, braking and source-switch behavior.
3. Rerun full public clips. Report surviving/missed/late cues, attribution,
   occupancy and availability together; lower cue count is not an accuracy score.
4. Compare 4/8 Hz schedules with the same fixed delay, plus clearly stale input.
   Fixed-delay desktop results cannot establish real Android timing.
5. Build/verify the APK and all 46 voice clips, then obtain an active-session
   diagnostic export on the Samsung S24 Ultra / Android 16. Measure semantic and
   combined completion, scheduling, audible onset and sustained thermal behavior.
6. Have an independent instructor label encounters and review wording/timing, then
   observe supervised learners: can they identify the situation and act without
   distraction, ambiguity or unnecessary corrections? Reserve unseen sessions
   before further tuning. No usefulness/accuracy percentage without those labels.

Results are appended to the execution log after the checks finish. This document
records the candidate design and its limits, not a declaration that acceptance passes.
