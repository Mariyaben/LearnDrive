# What 0.8.5 fixes from the Nov 8 drive

The Nov 8 drive (0.8.1, Kanhangad towards Odayanchal, 16.6 minutes) showed nine groups of problems.
0.8.2 to 0.8.4 dealt with hills from the map, seeing road users in time, and phone heat. This is
the rest, with the cause found for each.

Nothing here has been tried on the phone yet. "Checked" below means native tests and replays of the
recorded drives.

## 1. The speed reading froze, and with it most of the coaching

**Seen:** 27 km/h shown right through a turn; 25–30 km/h for minutes while Google's distance
countdown shows the car did 33–49; "Speed unavailable" for a second at a time; "waiting for GPS
speed" on the coach card for 208 s.

**Cause:** when the phone's level reference was lost (the mount tilted at 2:36), the motion
processing stopped altogether. The speed filter then never grew its uncertainty, so each GPS fix
counted for less than the one before: the speed became a running average. Fixes that disagreed with
it by more than 8 m/s were thrown away as glitches, and the reading dropped out.

**Fixed:** without a level reference the phone keeps tracking gravity and the camera's direction,
and keeps the speed, the turn rate, the camera's pitch and bump sensing alive. Only slope from the
phone's tilt is lost (with a Google route, hills come from the map anyway).

**Checked:** three new fusion tests (an unlevelled phone follows GPS speed; keeps camera pose and
turn rate; a mount moved mid-drive loses only slope).

## 2. Turn instructions cut off and said again

**Seen:** at two junctions the turn preparation was cut about 3 s in and said again in other
words, and "Right turn here…" was said twice, 4 s apart. Six instructions in 25 s at one junction.

**Cause:** Google's turn and distance were steady throughout (read back from the recording). The
speed tile shows "Speed unavailable" at 3:59, exactly when "Right turn here" was cut. A started
turn, bend or hill sentence was cancelled the moment the speed reading dropped out; cut short, it
counted as not said, so it was started again when the reading returned.

**Fixed, in four places:**
- the speed reading no longer drops out (above);
- a started sentence finishes through a moment without a speed reading; only a measured stop ends it;
- a step cut off after two seconds or more has said what matters and is not started over;
- the same Google turn survives a camera restart, a "rerouting" blink of Google's feed, and a route
  refresh (new route number, same turn at the distance the old one had reached).

**Checked:** seven new tests, including the whole chain through the app's own interface: the turn
is prepared once, survives a route refresh, gets "In 50 metres… Brake gently. Change down to
second.", and a real reroute is announced.

## 3. "The route has changed" was never shown

**Cause:** the sentence was valid only while Google's feed said "rerouting", which lasts under a
second. By the time it could be spoken it was no longer valid.

**Fixed:** the fact stands for 8 s once the new route is there, and only when the next turn really
is a different one (Google's feed also blinks to "rerouting" when it merely refreshes the route).

## 4. "Person ahead. Brake now" for people at the roadside

**Cause 1: the camera did not point where the car was going.** The phone was turned towards the
driver, so the camera looked a few degrees left of the car's direction. People at the left road
edge sat in the middle of the image and were taken to be in the path.

**Fixed:** the coach learns, during the drive, which column of the image the car is heading for:
- from how road users move as they approach (a box drifts sideways, as it grows, in proportion to
  its distance from the heading point);
- from where far road users appear (they stand on the road the car is following).

It uses the optical centre until both kinds of evidence are there and agree: 12 approaching road
users and 24 far ones, seen while driving straight above 15 km/h, pointing to the same column
within 4 % of the image width. If the two drift more than 6 % apart it goes back to the optical
centre. It corrects at most about 10 degrees and starts again if the mount moves. The detector's
zoomed views are aimed at the same column.

In the replays it settled on 0.53 of the image width on Nov 8 (from 7:30 into the drive) and 0.42
on Nov 6 (from 4:00). On Nov 6 a group at the right-hand roadside then got "Person on the right.
Ease off… Give them space" where the uncorrected coach said "Person ahead. Brake gently".

**Cause 2: a person in the lane waited behind a sentence about someone else.** At 2:42 "Oncoming
two-wheeler. Keep left…" was being said; the person walking in the lane 22 m ahead was held back
for 3 s and then got "Brake now" at 10 m.

**Fixed:** braking for a person or two-wheeler in the path waits only 1.5 s behind what was just
said about someone passing by, takes over from that sentence, and is ranked ahead of a passer-by
who has already been spoken about.

**Cause 3: braking was not seen.** See "You first" ([YOU_FIRST.md](YOU_FIRST.md)): the speed-trend
measure never reported "decreasing", so a learner who did brake was still escalated to "Brake now".

## 5. Wrong names

| Seen | Cause | Fixed |
|---|---|---|
| A scooter rider called "Person ahead" | From behind only the rider's body shows | A "person" in the lane moving the learner's way at 16 km/h or more is a two-wheeler |
| A pedestrian called "Oncoming two-wheeler" | A motorcycle far up the road stood behind their legs in the image | A rider's bike must reach the person's feet and be at least a third of their height |
| An autorickshaw called "Oncoming bus" | The detector has no class for it | **Not fixed.** See below |

A "bus" that reads under 2 m tall was tried as "vehicle" (as is done for "truck"). The height
comes from the distance along the ground, which is wrong wherever the horizon is off (a slope, a
dip, a guessed horizon in a replay). In the replays three real buses lost their name on Nov 5,
and on Nov 4 a bus coming the other way 40 m off became "Vehicle ahead. Brake now". So a bus
keeps its name, and an autorickshaw is sometimes called a bus.

## 6. Captions wiped

**Cause:** every caption was cleared while camera frames ran late, including turn instructions.

**Fixed:** a late camera clears only what the camera said. Turn, hill and gear captions stay.

## 7. Repeated sentences

"Straighten the wheel…" twice in 4 s, "Uphill. Steady power" twice in 14 s and "Sharp bend
ahead…" twice in 6 s have the same cause as the repeated turn instructions (a sentence cut by a
gap in the speed reading, then started again) or came from the phone's tilt being read as a hill
after the mount moved (now ignored once the level reference is lost).

The same instruction was also said once for each of a group: "Person ahead. Brake gently. Be
ready to stop." three times in ten seconds for people standing by a parked motorcycle (8:29–8:38).

**Fixed:**
- the next one of a group (the same kind of road user in the same kind of conflict, no worse)
  is not told about again within 8 s. A person and a rider count as the same kind here, because
  people beside a motorcycle are "person" in one frame and "rider" in the next;
- "Brake now" is said once per road user, and not again for someone else during the three
  seconds of the sentence (Nov 5, 5:15: two "Brake now" sentences 0.4 s apart, the second cutting
  off the first);
- nothing is started about a pedestrian the car will pass within about a second who is not on a
  collision course (8:38: the sentence began 5 m from a man on the verge);
- a warning sentence that has started is finished. It used to stop 3.5 s after its road user was
  last seen, which only ever cut the last word of a sentence about a vehicle that had just passed
  ("…Ease off the accel–": five times in the 4.7 minutes of the Nov 6 replay).

## 8. "Vehicle crossing. Brake now" for vehicles that were not crossing

Found replaying the drive with everything above in place. Three causes:

| When | What was there | Cause | Fixed |
|---|---|---|---|
| 8:45 | Two vehicles parked on the left verge | The tracker went from the nearer one to the one beyond; the jump looked like movement into the road | Movement toward the path must show in both halves of the last second, in the image itself |
| 11:26 | A car overtaking close on the right and pulling in | Its box grew as it came into the frame from beside the car | A vehicle whose box was cut off by the side of the frame at the very bottom of the image in the last 1.5 s came from alongside: it is overtaking, not crossing. A box cut off at the side and the bottom says nothing about approach |
| 11:35, 12:07 | Cars coming the other way round a left-hand bend | Left of where the car points and sliding right, as crossing traffic would | While the learner's own car is turning left (measured turn rate), such a vehicle is traffic on the far side of the road ahead: "Keep left" once it is clearly approaching |

Vehicles that really cross are still "Vehicle crossing": the two trucks that came out of side
roads a few metres ahead on Nov 4 (0:40 and 3:54) and the autorickshaw at 4:46. A first, broader
version of these rules lost the two trucks in the replay; the rules above are the narrower ones
that keep them, and two tests now hold a crossing vehicle in place.

## 9. The level-ground step at the start

With a Google route the hills come from Google's elevation data along the route, so the start
dialog no longer asks for level ground: it has one button, "Start". Free practice (no route) still
offers "Level ground · start" and "Start without hills", because there the phone's tilt is the
only source of slope.

## 10. Older tests

Sixteen native tests had been failing since 0.7.8–0.7.9. Two were small inconsistencies in the
code (a pothole's rejection reason; clearing an empty route line). Fourteen described junctions the
app finds itself but were set up as Google turns, whose timing changed deliberately in 0.7.9. All
23 native test programs now pass.

## What to look at on the next drive

- **Speed tile:** it should follow the car through turns and stops, with no "Speed unavailable".
- **Each Google turn:** one preparation ("In 100 metres, turn left. Check your mirrors. Signal
  left."), one "Brake gently. Change down…" when faster than 12 km/h, one "Left turn here…".
- **People at the road edge:** "Person on the left. Ease off… Give them space", not "Brake now".
- **Aim of the phone:** pointing it straight ahead is still best; the coach now copes with a few
  degrees. The camera status line still warns when it points too high.
- **Heat:** the status line names what was shed ("Basic detection · phone warm, distance model off").
