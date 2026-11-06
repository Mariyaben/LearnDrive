# First release brief — Android car learners in India

Status: engineering baseline adopted for implementation; instructor and learner
validation pending. Prepared 16 October 2026. This is not a release approval.

22 October direction: the [unified driving coach](UNIFIED_DRIVING_COACH.md)
defines the dashboard and build checkpoints. Google search/navigation is
integrated inside LearnDrive and needs internet and project configuration;
restricted-key Places and stationary route/feed/recreation checks have passed,
while moving-navigation and sustained-runtime qualification remain open.
Camera inference and coaching policy run locally.
Kerala is the optional offline provider. The app as a whole is no longer promised
to run entirely offline. Follow the [paid-launch execution sequence](PAID_LAUNCH_EXECUTION.md)
for current repair and qualification work.

## Product promise

Help a learner notice relevant situations and understand what to practise after
a supervised drive. Guidance is advisory and warning-only. The driver remains
responsible for operating the car. The app does not control the vehicle.

## Initial proposed support

Android, car, a fixed forward-facing rear camera, validated mount/phone profiles,
daylight and dry urban/suburban roads, initially 0–60 km/h. These conditions need
field evidence. Unsupported conditions must be visible, not silently treated as
successful guidance. Rear threats, blind spots, potholes and exact gear/RPM
coaching are outside first-release claims.

Camera, recorded video and Desk have different measurement provenance. Recorded
video must never inherit the phone's current camera intrinsics, stationary GPS or
live IMU as original driving measurements. Estimated/manual inputs remain labelled.

## Candidate capabilities and activation gates

| Capability | Engineering availability | Ordinary learner-release gate |
| --- | --- | --- |
| Pedestrian/cyclist attention | Experimental camera coaching exists | Instructor labels, relevance, timing, recall and comprehension |
| Traffic/gap attention | Experimental camera coaching exists | Lead/oncoming/parked distinction and useful cue timing |
| Metric gap/TTI | Experimental; calibrated evidence required | Known geometry, speed provenance, reference measurements and true-hazard tests |
| Bend preparation | Experimental; poor on Kochi development clip | Marked/unmarked-road direction and timing evaluation |
| Junction awareness | Not validated | Separate junction evidence and event benchmark |
| Phone inclination | Implemented sensor estimate | Mount/level setup and controlled angle checks; not hill-ahead knowledge |
| Emergency commands | Internal research only | Separate true-threat, nuisance, timing and human-factors gate |
| Post-drive learning review | Temporary deterministic cue/availability review implemented | Instructor usefulness/comprehension; no skill inference from cue counts |

No capability in this table has completed learner-release validation. Current
APKs remain mobile-lab builds. Manual Test voice / audio preemption controls test
the output hardware, not road-hazard recognition.

## Interaction and language rules

- Prepare the mount, visibility and settings before moving. Keep the driving UI simple.
- Use short attention/action wording; put detailed explanations in the drive review.
- Never infer “safe”, “all clear” or “maintain speed” merely from no detected boxes.
- Do not invent exact shifts, a hill ahead, a collision probability or a driving score.
- Preserve local/offline processing. Recording/export is a separate user choice.
- Confirm pilot language with participants; English is the current engineering vocabulary.

## Proposed acceptance evidence

Event relevance, per-category recall, useful warning windows, nuisance burden,
availability, comprehension and physical-device performance must be reported
together. Roadmap percentages are provisional targets, not standards or achieved
results. Abstaining on an in-scope real hazard counts as a miss. A quiet replay
does not establish accuracy.

## Decisions still requiring people or hardware

Instructor approval of phrases/conditions; learner interviews; consented paired
lesson observation; calibrated recordings; supported phone selection; physical
audio/thermal checks; model-distribution decisions. No interviews, approvals or
field checks are claimed by this brief.

See [the roadmap](../RELIABILITY_ROADMAP.md) for work packages and gates.
