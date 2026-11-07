# 0.8.1: the brain replayed on the Nov 4, 5 and 6 drives

Each drive's own camera view and measured speeds were replayed through the native brain, with the
app's default Balanced coaching and the detector mosaic. The Nov 5 replay uses the drive's seven
turns as a synthetic route; the Nov 6 replay uses Google's turns as shown on that drive's own turn
card (read by OCR, turn times fitted to Google's distances within 1.5-3.7 m). Every instruction was
checked against the camera frame at that moment, and Nov 5 against the instructor's captions.

## Issues found

### Speech (from the Nov 6 recording)

| Issue | Cause | Fix |
|---|---|---|
| Instructions cut off mid-word, then repeated | Braking was one 100 Hz sample below -0.8 m/s^2 (0.08 g): mount vibration flickered "braking" all drive, and every flicker cut speech; a late camera frame (> 0.35 s) also cut routine speech | Braking is a sustained deceleration (filtered ~0.3 s, at least ~0.15 g); started instructions finish their sentence; a stop that is not urgent ends at the next natural pause |
| Notification sounds cut instructions | Any audio focus loss, even "duck", cancelled speech | Only a real loss (a call, another app taking audio) stops speech |
| Robotic, choppy voice | Old Windows voice; ~0.38 s of dead air between phrases | The phone's own natural offline voice (Indian English first), rendered once and cached; whole composed sentences rendered for natural intonation; phrases joined with a short breath at a comma and a sentence pause at a full stop |
| Clicks and crackles | Stops cut the waveform; small buffer on a busy, warm phone | ~25 ms fade on stops; half a second queued; audio-priority thread |

### Road users

| Issue (drive, time) | Cause | Fix |
|---|---|---|
| "Vehicle/Person crossing. Stop and wait" for parked vehicles and people walking along the road on bends (Nov 4 40 s; Nov 6 194 s) | Turning slides every static object across the image, read as crossing | The camera's turn rate (gyroscope on the phone, video in replays) is removed before judging a road user's own movement |
| "Person crossing. Stop and wait" at 28 km/h for a roadside billboard (Nov 4 347 s, 350 s) | A printed figure looked like a person at 10 m | A road user must stand on the road: its box bottom must move down the image as it grows, and its foot distance must agree with its size (now for every box, not only small ones) |
| "Person ahead. Brake now" for people walking along the road edge 15-25 m ahead (Nov 6 71 s, 109 s, 144 s, 262 s) | The path corridor was ~1.8 m wide at 16 m; a small far box's noisy growth gave a 2 s "time to collision" | The path in metres (car half-width plus margin, widening slowly for bends); "Brake now" needs the road user itself in the path and a box big enough to time; a person at the path edge gets "Person on the right. Give them space" |
| "Brake now" for far vehicles (Nov 4 300 s) and a car parked at the roadside (Nov 4 333 s) | Noisy growth of small boxes; the box's inner edge touching the path counted as in the path | As above; not while the car is drifting out of the path as the learner passes |
| "Person ahead. Brake now" for a man walking toward the car 1.4 m right of the centre line (Nov 6 144 s) | Anything inside the widened path could get "Brake now" | "Brake now" needs a collision course: a person's centre within ~1 m of the path centre, a vehicle's near edge within ~0.95 m |
| "Two-wheeler ahead. Brake now" for a rider coming head-on (Nov 6 83 s); "Oncoming vehicle" for a slower car being caught up (Nov 4 313 s) | Oncoming was "right of centre and growing" | The road user's own approach speed (from its size and growth, beyond the learner's) decides when it is clear (beyond +-1.5 m/s), else the image rule; its own lane counts up to 3 m beyond the path; head-on and closing within stopping time is urgent |
| A van passing at the image edge lost its "Oncoming" (Nov 4 270 s, during the fixes) | A box cut off at the side stops widening, so its growth was underestimated | Growth from height alone when cut off at the side (width alone when cut off at the bottom) |
| A billboard "crossing" while the detector alternated between two printed figures (Nov 4 347 s) | One jump of the box between the figures read as movement toward the path | Movement toward the path must be sustained over both halves of the window, and above 12 km/h the box must grow as something on the road does while the learner approaches (at least ~15 % of speed / distance) |
| "Oncoming vehicle. Keep left" for nearly every car on a busy road (Nov 4: seven in 75 s, during the fixes) | Each passing car was a new road user | A routine "keep left" for a car waits 20 s after the last one; a bus, a truck or an urgent one still speaks |
| A warning cut off 0.1 s after it began by the same words, now urgent (Nov 5 234 s) | Rising urgency re-issued an identical instruction | The same words about the same road user are not re-issued within 3 s |
| "Stop and wait. Let them pass." without saying who (Nov 4 350 s) | The composer lost the road user's track | A warning whose road user cannot be found speaks its recorded sentence, which names it |
| "Truck ahead", "Oncoming truck" for autorickshaws (Nov 6 166 s, 183 s) | The detector has no autorickshaw class | A "truck" under ~2.3 m tall for its distance is treated as a small vehicle |

### Turns

| Issue | Cause | Fix |
|---|---|---|
| "Look both ways" 0.5-2.5 s before the turn at 12-15 km/h; the instructor says it 5-9 s before (Nov 5) | Entry at 3 s and "change down" at 5 s crowded each other | Entry about 4 s (at least 14 m) before the turn, "change down" about 7 s (at least 28 m) |
| "Left turn ahead" after "Left turn here" was said (Nov 5 53 s) | The junction was still named in other instructions | Not after the entry step or within 2 s of the turn |
| "Straighten the wheel" after a missed turn (Nov 6 81 s; Google rerouted) | The exit cue followed Google's step change | Only after a measured turn (about 30 degrees from the junction until the cue is due), or without a turn rate, only if Google is not rerouting |
| The same planning said twice (Nov 6 68 s, 74 s; 114 s, 124 s) | A false "Brake now" interrupted it; another warning cut its last syllable | Fixed with the false "Brake now"; an instruction 85 % heard counts as delivered |
| "Clutch down. Look both ways. Sound the horn." at a fork or a merge | The junction entry words were used for every maneuver | Fork or ramp: "Keep left/right. Check your mirrors."; merge: "Merge carefully. Check your mirrors." |

## Results: first replay (0.8.0 + cut-off fix) against the final 0.8.1 brain

Every changed instruction was checked against the camera frame at that moment.

- **Nov 4**: 22 instructions became 18. Gone: "Brake now" for far vehicles (299.5 s) and for a car
  parked at the roadside (332.7 s), a bare "Stop and wait. Let them pass." (349.7 s), and two
  doubtful "crossing" calls (262.5 s, 380.4 s). The billboard now gets "Person on the left. Give them
  space" instead of "Person crossing. Stop and wait": milder but still wrong in the replay (its
  horizon is a fixed guess, so the foot-versus-size check cannot catch it; the phone measures its
  horizon). Kept: the mini-truck pulling out of a side road (40 s), the oncoming van (270.3 s) and the
  person on the right (367.3 s).
- **Nov 6**: 32 instructions became 28. All four "Person ahead. Brake now" for people walking along
  the road edge became "Brake gently" or nothing; the head-on rider (83.2 s) now gets "Oncoming
  two-wheeler. Keep left. Ease off"; the duplicate planning (74.4 s) and the "Straighten the wheel"
  after the missed turn (81.5 s) are gone; each "turn here" comes about 1.5 s earlier;
  autorickshaws are "vehicle" where the geometry allows. Lost: "Vehicle ahead. Clutch down. Hold the
  brake" for an SUV pulling out of a gate at 4 km/h (16 s): its box centre was outside the path.
- **Nov 5** (against the instructor's 17 captioned actions): 37 instructions became 40, and the
  instructor actions the app also covers went from 5 to 7. "Change down to second" now comes before
  each turn and "turn here" 2-2.5 s earlier; "Straighten the wheel" follows the real turns; the bus
  the instructor named ("There is a bus on the right", 149 s) is called at 148 s and 152 s; the
  stale "Left turn ahead" and the doubled oncoming warning (234 s) are gone. Lost: the entry step of
  the fifth turn (an urgent oncoming car took its moment, 239.6 s), "Person on the left. Give them
  space" at 257 s (held back by a warning about an autorickshaw just before), and "Straighten the
  wheel" after the sixth, blind turn (its measured heading change stayed under the threshold).

## A safety check on the fixes

The two "standing on the road" tests read the bottom of a box as the road user's feet. A pedestrian
half hidden behind a parked car (only the part above its roof visible) or by the learner's own
bonnet has no visible feet, so the tests would have dropped exactly the person who may step out.
Neither test applies when a vehicle's box top meets the person's box bottom or the box bottom is
in the bonnet area; such a person's distance is taken as just beyond the vehicle. A test checks
it, and fails with the exception removed.

## Not fixed

- **Crawling off at 1 km/h measured speed** (Nov 5, first turn 21 s after moving off): junction
  instructions are held below 3 km/h so they are never said while parked; at a crawl the measured
  speed cannot tell the two apart.
- **Camera lag and phone heat** on Nov 6 ("Camera delayed", "phone warm"): needs measuring on the phone.
- **Replay geometry**: the Nov 4-6 videos are screen recordings of the app's camera preview with a
  fixed horizon guess, so replayed distances are rougher than the phone's own (measured horizon).
- **Older test groups**: 18 groups written for earlier timing still fail (none new in 0.8.1).
- **Not road-tested**, and the natural voice has not been heard on the phone yet.
