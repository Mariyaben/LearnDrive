# LearnDrive: complete driving coach

2026-11-02. Branch `driving-coach-foundation`, base `9df5912`.

Purpose: guide learners through the full driving task during supervised practice.
Perception, maps and telemetry are inputs. The product unit is a teaching episode:
goal → preparation → timely instruction → observable response → adaptation →
quiet execution → review and next practice goal. Object presence is not a lesson.

## Curriculum and capability audit

| Domain | Target teaching | Current evidence / missing capability |
|---|---|---|
| Preparation | seating, restraints, mirrors, vehicle checks | parked lesson; completion unobserved |
| Moving off / stopping | observation, coordination, controlled stop | speed trend only; controls, blind spots and intent unobserved |
| Steering / positioning | appropriate line, clearance and lane discipline | image geometry; metric lateral placement unqualified |
| Observation / anticipation | mirrors, blind spots, developing conflicts | forward road users; learner gaze and side/rear views unavailable |
| Signals / communication | appropriate timing and cancellation | intended route; indicators and other-driver intent unavailable |
| Speed / following | appropriate approach and space | measured speed, optical hypotheses; safe speed/gap not established |
| Junctions / turns | observation, signal/position planning, approach, give-way preparation | first task-flow pilot; no right-of-way or manoeuvre-success claim |
| Roundabouts | approach lane, entry observation, exit preparation | route UI; lane/right-of-way/exit coaching not implemented |
| Bends / hills | preparation, control and anticipation | supported curvature/inclination; controls and suitable gear unavailable |
| Overtaking / lane changes | observation, legality, space and completion | insufficient surrounding coverage; no permission-to-proceed command |
| Reversing / parking | observation, low-speed control, manoeuvre sequence | insufficient rear/side geometry; parked teaching only |
| Traffic / vulnerable road users | anticipate interactions, adapt preparation | recognition/tracking; contextual usefulness unqualified |
| Rules / signs / priority | situation-specific application | jurisdiction/rule reasoning and reliable sign/signal recognition missing |
| Difficult conditions | visibility, traction, lighting, road works | not qualified |
| Recovery / emergencies | recognize limits, instructor intervention | no autonomous emergency teaching qualification |
| Vehicle controls | appropriate gears, clutch, accelerator, braking | phone does not measure the needed control states; OBD remains on hold |
| Learning progress | evidence-based goals, instructor corrections, adaptation | first bounded session record; no mastery grading or validated personalization |

All domains belong to the target. Listing them does not imply implementation.
Learner stage (beginner/developing/independent practice) is distinct from speech
frequency. No automatic promotion or mastery claim follows from cue counts.

## Required lesson contract

Every implemented lesson must declare its objective, applicable context, required
signals and freshness, instruction sequence, delivery evidence, observable response,
unknown actions, reasons to wait, cancellation conditions, completion limits,
approved wording, replay cases and instructor-validation status.

Do not interpret silence, absence of a detection, a speed trend, voice submission
or a route transition as proof of safe or correct driving. Reminders about an action
do not establish that the action happened. Unknown remains unknown.

## First complete software episode: junction preparation

Automatically activated in a normal supervised session when route context applies;
the learner does not select a lesson. With a fresh route,
qualified GPS and measured speed, offer an early observation/signal/position-planning
lesson. After completed or intentional visual delivery, observe speed trend. Offer
one approach-speed/give-way preparation reminder only if there is still sufficient
lead time and measured slowing is not already observed. Do not cram missed steps
into the final approach. Native urgent cues retain priority. Execution is quiet.
Route progression ends the opportunity with driving outcome unassessed.

Mirror checks, actual signalling, road position, chosen gear, clutch/pedal actions,
priority, entering a gap and successful turn completion are not measured. No
instruction to enter, turn now, select a numbered gear or release a control is added.
These missing capabilities are explicit backlog items, not replacements for the
complete product target. The pilot is not instructor-qualified.

## Execution and gates

1. Implement task state and evidence contracts, native integration and replay tests.
2. Connect parked lesson selection, live phase display and bounded learning records.
3. Verify build, delivery/cancellation/source transitions and missing-data behavior.
4. Independently review full intervals for necessity, timing, omissions and silence.
5. Validate sustained phone performance, interruptions and supervised use.
6. Expand one curriculum domain at a time using the same episode contract.

No dataset-generation work is restored. No new generative model or OBD integration
is part of this increment. Human teaching quality and road readiness remain gates.
