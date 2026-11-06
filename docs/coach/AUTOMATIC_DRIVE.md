# Automatic coaching during a drive

Android 0.7.1, version code 32; 0.7.2 (code 33) adds task-aware scheduling, described in
[../reviews/2026-11-04-task-aware-scheduling.md](../reviews/2026-11-04-task-aware-scheduling.md). 0.7.1 replaced the required junction lesson selector
with one automatic coordinator for the supported driving situations. It is a
development candidate. The change does not establish human-instructor equivalence,
full curriculum coverage or paid-launch readiness.

## User flow

Mount/setup the phone and start the existing supervised session. Destination
selection is optional: a route adds early junction preparation. Without a route,
supported road-user, camera-bend and measured hill cues remain available. The app
does not infer unobserved junctions from the absence of a navigation feed.

The learner does not choose which situation the app should handle. Voice and
Minimal/Balanced/Guided remain independent settings. Local practice records include
delivered road-user, bend, hill and junction guidance across the whole session.

## Coordination and evidence

`DriveCoach` runs inside the native coordinator after evidence validation, with
the same temporal memory, event ledger and audio gates. It adds no image model,
network inference, vehicle hardware or cloud service.

Priority is: independently supported urgent warnings; measured developing closing
conflicts; junction teaching steps; then bend, space and hill preparation. A
routine closing-conflict cue does not acquire warning authority or bypass pacing.
Each candidate is checked independently so an already-informed or low-value cue
cannot hide a different useful task. A repeated semantic variant for the same
continuous encounter stays suppressed until the encounter resolves. Urgent
escalation retains the existing worsening-evidence and warning pacing rules.

The junction sequence remains preparation → delivery → measured response →
optional approach reminder → quiet execution. Unrelated routine tasks are quiet
in the final approach; supported urgent warnings still take priority. After the
junction context ends, other supported tasks resume automatically. Lost route
data pauses route teaching without disabling independent road-user/terrain inputs.

Fresh camera evidence is required for routine automatic teaching. Hill/bend/route
instructions also require valid measured motion above 3 km/h. Stopped or unknown
speed does not trigger power or bend-control instructions. Critical commands stay
subject to their existing explicit research opt-in and optical evidence checks.

## Speech lifecycle

Original evidence clocks govern selection, acceptance and playback onset. They
are never refreshed to rescue an old queued instruction.

Once speech starts, a separate continuation gate checks current support for the
same event, source and settings revision. Route changes, lost pitch/bend evidence,
expired actor evidence and other invalid context can interrupt the recording.
The worker checks at most ten times per second, outside its audio monitor, both
while writing PCM and while awaiting playback completion. Cancellation flushes
pending PCM. Brief actor dropouts may reuse an independently captured observation
only within its original 350 ms evidence budget; another actor cannot replace it.

Completed PCM and intentional visual-only acceptance are counted separately from
failed/interrupted delivery. Neither proves that a learner heard, understood or
followed an instruction. No pedal/gear/gaze/indicator state or driving mastery is
inferred. Bend/hill/space guidance still uses existing recorded phrase IDs; only
junction preparation currently has the richer multi-step task sequence.

## Verification and remaining gates

The native tests include 90 profile/feature/fault combinations and three continuous
synthetic drives combining road users, bends, hills, junctions, measured slowing,
urgent interruptions, route loss, camera loss/recovery and source changes.
`drive_coach_tests build/automatic-drive/mixed-drive.jsonl` writes their timelines.
These supplied measurements are synthetic; they are not perception ground truth.

`tools/evaluate_automatic_drive.py` compares the prior ordinary policy and automatic
mode on unchanged cached recorded-perception inputs. The local run used three
captures, 18 configurations and 4,734 decisions. Outputs were identical on those
inputs: regression evidence, not evidence of improved coaching. No new pixel
inference, instructor labels, audible playback or commercial training export is
claimed by this replay.

Phone runtime/audio/lifecycle checks, sustained performance and independent review
of teaching usefulness and omissions remain open. A successful host benchmark or
APK build cannot close those gates. The full curriculum plan remains in
[EXECUTION_PLAN.md](EXECUTION_PLAN.md); OBD and the undone dataset work stay on hold.
