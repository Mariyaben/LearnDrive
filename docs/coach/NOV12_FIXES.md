# What 0.8.6 changes after the Nov 12 drive

The Nov 12 drive (Odayanchal to Kanhangad and on along NH 66, 14 minutes, a 0.8.5 build from
before that morning) was read back second by second from the app's own screen in the recording.

| | Nov 12 on the phone |
|---|---|
| Sentences spoken | 84 in 847 s: one every 10 s; 28 came less than 6 s after the one before |
| About traffic coming the other way | 27 ("Keep left" 29 times in all) |
| "Vehicle ahead. Brake gently…" | 16 |
| "Brake now" | 11; two or three were justified |
| Google turn steps | 5, each said once, at the right time |
| Over 12 words | 13 sentences; the longest 18 words |
| "Speed unavailable" | 54 times, a second each |
| "Camera points too high" on the status line | 781 of 847 s |

An outside review of the same recording reached the same main conclusion: the coach talks too
much, and warns about things that are not dangerous. It is right. The causes are below; the last
column of each table says how the fix was checked. Nothing here has been tried on the phone yet.

## 1. Distances were two to five times too short with the phone leaning back

**Seen:** "Vehicle ahead. Brake now" for buses and trucks 30–60 m away.

**Cause:** the phone leaned back about 22 degrees on its mount, which puts the horizon four fifths
of the way down the picture (0.82). The code held the horizon no lower than 0.754, a limit kept
from the days of the uncropped frame. Below that limit every distance read from where a road user
stands on the road came out too short, and nothing could read as farther than 23 m. A bus 30 m
ahead read as 12 m.

**Fixed:** the limit is now 0.97 in both places it was applied (the phone's horizon and the
coach's geometry). The line below which a box counts as "on the learner's own bonnet" follows the
horizon as well: it was fixed at 0.8 of the picture, which with this mount is above the horizon.

**Checked:** replaying the drive, the bus the learner followed for a minute reads 20–25 m instead
of 11–14 m.

## 2. The horizon was lost whenever motion readings arrived late

**Cause:** the phone's motion samples arrive in bursts when it is busy or warm. Any pause over
0.2 s threw away the camera's pose and the level reference; for the next 0.2 s the coach was given
the optical centre as its horizon. With the phone leaning back that is half a picture above the
real one, and against it a road user 60 m away stands "3 m ahead". The same pauses are why the
speed tile read "Speed unavailable" for a second at a time. GPS fixes were handled on the motion
thread, which made such pauses more likely.

**Fixed:**
- a pause under 2 s keeps the pose and the level reference;
- when no pose is available the last measured horizon stands; the optical centre is never put in
  its place, and until a horizon has been measured nothing is said about road users;
- the speed stays valid through a late burst of motion samples and one missed GPS fix;
- GPS fixes have a thread of their own.

**Checked:** four new tests (late samples keep speed, level reference and pose; a long pause still
loses the level reference). The effect on the phone cannot be replayed: it needs a drive.

## 3. "Brake now" used the wrong time, and a single frame was enough

| Cause | Fixed |
|---|---|
| The limit on time to contact was the whole time to a standstill (4.9 s at 40 km/h). Braking must begin when the time to contact is the reaction time plus *half* the braking time (3.1 s) | "Brake now" at that time plus 0.6 s (1.0 s for a person or rider) |
| One frame in which a box was a little larger than in the last counted as closing. A bus followed at a steady 20–25 m got "Brake now" five times in a minute | The growth must show on two looks running |
| The range for "Brake now" was one and a half stopping distances | 1.2 stopping distances plus 4 m: 45 m at 40 km/h, 18 m at 20 |
| A vehicle coming the other way at the image centre (on a bend) counted as a vehicle ahead | One whose closing speed is well above the learner's own is coming toward them: it is never "Vehicle ahead. Brake…" |
| A vehicle moving the learner's way got "Brake gently. Be ready to stop" seven seconds from contact, like something standing in the road | For a vehicle moving the same way, only when the gap is closing fast |

What stands in the road, and people and riders in the path, are told about as early as before.
"Brake now" itself is never held back by the judgement of how fast the gap closes: on a collision
course, in range and that close in time, it is said whatever is coming. That judgement uses the
growth seen on two looks running, and calls a vehicle "coming toward the learner" only when the gap
closes at least 60 % faster than their own speed, so that a distance off by a quarter cannot turn a
vehicle standing in the road into one coming the other way.

"Brake now" after "Brake gently" about the same road user is not blocked as "the same words said
again" (they share an instruction id): it is the warning that the gentle braking is not enough.

**Checked:** on the Nov 12 replay "Brake now" went from 6 to 1 (the bus ahead really was slowing).
A person standing in the car's line still gets "Person ahead…", then "Brake gently…", then "Brake
now" (a test).

## 4. Traffic coming the other way

**Cause:** "Keep left. Ease off the accelerator." was said for any vehicle coming the other way
whose near edge was within about 4.5 m of the learner's line: every vehicle in the other lane of
an ordinary road. Buses and trucks were exempt from the pacing altogether.

**Fixed:**
- a car or two-wheeler is spoken about only if it will pass close: its near edge within 2 m of the
  learner's line, judged within 50 m, where that can be measured;
- a bus or truck is still named early, as far off as it is seen;
- at most one such sentence in 30 s (20 s for buses and trucks);
- nothing is started about a vehicle that will have passed before the sentence could be heard;
- the urgent form ("head-on, close and closing") only within 35 m, where a vehicle in the other
  lane and one in the learner's own can be told apart.

## 5. A budget, and shorter sentences

- "Keep left" waits the ordinary interval after other speech (12 s); "give them space" 8 s;
  braking for a vehicle, and the first word about a person or rider in the path, 6 s; something
  crossing the path, 3 s as before. "Keep left" and braking for a vehicle used to follow anything
  after 3 s.
- At most three sentences a minute about what passes by (traffic coming the other way, people at
  the roadside, a vehicle ahead to keep a gap from), each about a different road user. Whatever is
  in the path or crossing it is never held back by this budget, however far ahead it is seen;
  urgent warnings and turn steps are outside it too.
- Another road user joins a sentence only if it calls for an action of its own. "Vehicle ahead.
  Ease off the accelerator. Keep a safe gap." no longer rides along with a turn instruction.
- A turn step keeps what it asks for itself: "Signal left" is not pushed out by something else.

## 6. Bends from Google's route

Until now only sharp bends (55 degrees or more within 35 m) were named, without their direction
or anything about speed, and only with the horn rule on.

- **Found on the route line:** also bends that are not sharp but want the right speed (35 degrees
  or more within 40 m), each with the speed its tightest 20 m is comfortable at (about 1.5 m/s²
  sideways; a sharp bend at most 22 km/h). A turn Google announces is that turn, not a bend.
- **A sharp bend is always named,** about 7 s ahead: "Sharp left bend ahead. Keep left. Sound the
  horn." Too fast for it: "Sharp right bend ahead. Brake gently. Change down to second. Sound the
  horn." (9 s ahead when braking is needed.)
- **Any other bend only when the learner is too fast for it:** "Left bend ahead. Ease off the
  accelerator." or "Right bend ahead. Brake gently. Change down to third." At the right speed
  nothing is said, and at most one such sentence in 20 s on a winding road.
- Four new voice phrases: "Left bend ahead.", "Right bend ahead.", "Sharp left bend ahead.",
  "Sharp right bend ahead."

## 7. On the screen

- Boxes on the learner's own dashboard, bonnet or phone mount are not drawn.
- "Camera points too high" starts at 17 degrees of lean (it was 6, a limit from the uncropped
  frame: leaning back 10–15 degrees is in fact the ideal aim now). It is shown only when stopped or
  crawling, when it can be acted on; on the move the line says what the camera is doing.

## Long-range detection

The detector, its 1280×960 frames and its zoomed views are untouched. What stands in the road, and
people and riders in the path, are named as early as before; buses and trucks coming the other way
are still named as far off as they are seen. What changed is which situations get a sentence at
all, and how often.

A new test holds this with the Nov 12 mount (leaning back about 22 degrees, the 0.8.1 preview crop)
at 40 km/h. The first sentence about each hazard came at:

| Hazard | First sentence | At |
|---|---|---|
| Bus coming the other way, in its own lane, seen from 100 m | "Oncoming bus. Keep left. Ease off the accelerator." | 77 m |
| Person standing in the lane, seen from 90 m | "Person ahead. Ease off the accelerator. Cover the brake." | 59 m |
| Lorry stopped in the lane, seen from 100 m | "Truck ahead. Brake gently. Be ready to stop." | 61 m |
| Motorcycle stopped in the lane, seen from 80 m | "Two-wheeler ahead. Ease off the accelerator. Cover the brake." | 59 m |
| Person in the lane right after three buses were spoken about | "Person ahead. Ease off the accelerator. Cover the brake." | 56 m |

The drive replays cannot show this range: they detect road users in a 472×256 crop of the screen
recording, not in the phone's own camera frames, and see buses only from about 20–30 m. In the
Nov 12 replay the buses and trucks coming the other way were recognised at a median of 18 m (up to
29 m) with 0.8.6 against 10 m (up to 13 m) with the code before it: the distance fix makes them
recognised sooner, not later.

## Not done

- **Mount.** The phone has leaned back about 20 degrees on both recorded drives. The coach now
  measures correctly at that angle, but the road still fills only the bottom fifth of the picture.
  A mount that holds the phone more upright, or using the phone's wide camera, would see more.
- **Bends and the path.** Whether a road user is "in the path" is judged along the line the car
  points, not along the curve it follows. On a bend someone on the inside edge can be "Person
  ahead. Brake gently." (Nov 8, 15:56). Only "keep left" toward them is now prevented.
- **Detector flicker.** A rider seen head-on often shows as "person" one frame and "motorcycle" the
  next; then nothing may be said about them until close (Nov 8, 7:07).
- **Automatic cars.** Gear and clutch advice assumes a manual car; there is no switch for it yet.
- **Speed from the phone only.** "Change up to second" comes from the speed, never from the gear.

## Results

Native tests: all 23 programs pass (82 groups of driving-coach tests, 12 of map knowledge, 145 core
checks). App unit tests: 16 pass. Replays of the five recorded drives:

| Drive | Sentences before → 0.8.6 | "Brake now" before → 0.8.6 | Notes |
|---|---|---|---|
| Nov 12 | 84 on the phone; 41 replayed with the previous code → 18 | 11 on the phone; 6 replayed → 1 | The one left: the bus ahead really was slowing. Every Google turn step said as before |
| Nov 8 | 58 → 41 | 0 → 1 | The one: a pedestrian walking in the lane after "Brake gently" (2:45). The left turn's three steps now come at Google's 90 m, 50 m and 30 m |
| Nov 6 | 33 → 30 | 0 → 0 | |
| Nov 5 | 39 → 39 | 2 → 2 | Both real: a car stopping ahead, two men in a narrow lane. "Left turn here" at 4:01 is back |
| Nov 4 | 30 → 26 | 3 → 3 | A crossing autorickshaw at a junction, a lorry coming close, a vehicle ahead |

The replays see much less of the road than the phone does (see above), so on the phone expect more
sentences about road users far ahead than the replays show, and none of the ones removed here.

## Also in 0.8.6

- **Nothing coming toward the learner on their left is told "keep left".** More than half a metre
  left of the car's own path (allowing for the curve it is following), a road user is judged as
  what it is, in the path or beside it: a motorcycle coming 2 m to the learner's left gets
  "Two-wheeler ahead. Ease off the accelerator. Cover the brake." A "person" is taken for a rider
  coming toward the learner only when its growth says so on two looks running.
- **A warning sentence about a road user that has gone gives way to a turn step that is due**
  (after 1.5 s). On Nov 5 "Oncoming vehicle. Keep left…" was said to its end and "Left turn here"
  never came.
