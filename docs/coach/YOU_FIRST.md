# You first (0.8.5)

A driving instructor says less as the learner needs less. Until 0.8.4 the coach said the same thing
to everyone, every time. From 0.8.5 it keeps a record of what the learner's **measured speed** did
about road users, and leaves out what that speed shows they have already done.

On the first drives nothing changes: a new learner is told everything, in full.

## What is measured

Speed only. The app cannot see where the learner looked, what their feet did, or why they slowed.

For each road user the coach has something to say about, one outcome is recorded:

| Outcome | Meaning |
|---|---|
| Slowed before being told | Their speed was falling when the instruction became due, and stayed down |
| Slowed after being told | Their speed fell within 5 s of the instruction |
| No slowing seen | The speed was measured throughout and did not fall |

- **When the coach starts watching.** It notices a road user in or beside the path up to about 8 s
  ahead, well before any instruction is due. Slowing before that is not credited to that road user.
- **Easing off and speeding up again is not slowing.** Picking the speed back up by more than
  2 km/h cancels it.
- **What counts as a situation.** Only a road user the coach said something about (in full or
  shortened), one whose instruction was left unsaid, or one the learner stopped short of. Road
  users merely near the path while the speed happened to fall are not counted. Counting those gave
  22 "slowed first" in the 4.7 minutes of the Nov 6 drive.
- **No measured speed, no record.** Nothing is recorded or changed without a speed reading.
- **Only the learner's own driving.** A test video or a simulation in the app neither adds to the
  record nor uses it.

Outcomes are kept per kind of situation: people ahead, two-wheelers ahead, vehicles ahead, oncoming
traffic. The last 30 of each are kept, with the number of drives they came from.

## What changes in what the coach says

An instruction is never delayed. It is due at the same moment as before; what changes is what is
said at that moment once the learner's speed shows the slowing is already done.

| The record for this kind of situation | Has not slowed | Has already slowed |
|---|---|---|
| New learner, or a thin or mixed record | In full | In full |
| Slows when told (level 1) | In full | The instruction without its slowing part |
| Often slows before being told (level 2) | In full | Nothing said; the screen shows the road user was seen |

Examples of the shortened instruction (what speed cannot show is still said):

| In full | Once the learner has slowed |
|---|---|
| Oncoming bus. Keep left. Ease off the accelerator. | Oncoming bus. Keep left. |
| Person on the left. Ease off the accelerator. Give them space. Sound the horn. | Person on the left. Give them space. Sound the horn. |
| Person ahead. Brake gently. Be ready to stop. | Person ahead. Be ready to stop. |

"Has already slowed" means:
- for "ease off" instructions and for oncoming traffic: the speed has fallen since the coach
  noticed the road user, and has stayed down;
- for "brake gently, be ready to stop": the speed is falling now, fast enough that this braking
  alone stops the car 5 m short of the road user, or the car is crawling and not speeding up. If
  the braking eases off while it still matters, the instruction is said then, in full.

### What never changes

- **"Brake now"** and instructions about crossing road users are never shortened or left unsaid.
- **A bus or truck coming the other way** is always spoken about (shortened at most).
- **A learner who has not slowed** is told exactly as a new learner is, whatever the record.
- **Silence is not an all-clear.** When an instruction is left unsaid the coach card shows, for
  example, "Person ahead · seen. You slowed first."

## Earning a level

| Level | Needs, for that kind of situation |
|---|---|
| 1 | 12 or more recorded situations over at least 2 drives, slowing seen in 70 % or more |
| 2 | 24 or more over at least 4 drives, slowing seen in 80 % or more, 8 or more of them before being told |

- A level is fixed when a drive begins. This drive's outcomes count from the next drive, so the
  coach never changes how it speaks in the middle of one.
- A mixed record drops back to "in full" by the same rule.

## In the app

- **Setup › More setup options › "Say less when I have already slowed"** switches it off or on
  (on by default; it has no effect until a level is earned).
- **Session review** adds "Road users and your speed": how often the learner slowed before being
  told, after being told, and not at all, with the note that this is an observation of speed, not a
  driving grade.
- **Practice records** shows what is on record, and "Clear saved records" removes it.
- **Storage.** The record is one small file in the app's private storage
  (`learner-record.json`). It is not part of the evidence export and is removed with the app's
  data. The drive's evidence log does show, tick by tick, when an instruction was left unsaid
  (`held_kind`) or shortened (`brief`).

## A fault this work exposed: the coach could not see slowing at all

Replaying the Nov 6 drive, the learner braked from 23 to 9 km/h for an oncoming bus and the record
said "no slowing seen". The cause was in the measure every "is the learner slowing?" rule uses.

- Measured speed is stepped: the phone corrects it at each GPS fix, about once a second; a replay's
  readings are whole km/h twice a second.
- The trend estimator treated any change faster than 40 km/h per second between two consecutive
  readings as a glitch. Read 30 to 100 times a second, an ordinary 1–3 km/h step looked like
  100 km/h per second or more.
- Every such step restarted the estimator. Over the whole Nov 6 replay it reported "unknown" or
  "steady" and never once "decreasing".

Consequences beyond "you first": gear-down advice never came in replays, and a learner who braked
after "Brake gently" was still escalated to "Brake now" 2.5 s later, because their slowing was not
seen.

Fixed in 0.8.5: the rate is taken over how long the previous value had stood (0.25–1 s). A real
glitch, such as 30 km/h jumping to 75 in one reading, is still rejected.

## Tests

Native (`drive_coach_tests`, `core_tests`, `temporal_support_tests`):
- a new learner is told at once and in full, even when already slowing;
- a learner who slows when told hears the instruction without the slowing already done;
- a learner who usually slows before being told is left alone once they have slowed, and the
  screen is told;
- any learner who has not slowed is told exactly as a new learner is;
- easing off and then speeding up again is not acting first;
- "Brake now" is never held or shortened, whatever the record;
- braking for someone in the lane early, late, and not enough;
- oncoming traffic is judged by easing off, and a bus is always spoken about;
- without a measured speed nothing is held and nothing is recorded;
- the record earns its levels slowly, survives storage, can be forgotten, and never changes level
  mid-drive;
- a test video or simulation neither uses nor changes the record;
- stepped speed readings still show a learner slowing down or speeding up.

App (`LearnerReviewTest`): the after-drive words never claim more than measured speed supports.

## Limits

- **Slowing is not seeing.** A learner may slow for another reason and not have noticed the road
  user. That is why the first reduction only drops the slowing part and still names the road user,
  and why full silence needs a strong record and is shown on screen.
- **Not built:** choosing the route as a lesson (the third part of the original idea).
- **Not yet tried on the phone or the road.**
