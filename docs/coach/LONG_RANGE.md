# Seeing road users in time (0.8.3)

## What went wrong on the Nov 8 drive

On the Nov 8 drive (0.8.1), warnings about road users came after the moment to act had passed.

- **LEADER bus, 12:54–12:58.** The bus was plainly visible on the preview from 12:54, about 100 m away. The first box appeared at 12:57.5, about 12 m away. "Oncoming vehicle. Keep left." came at 13:00, after it had passed.
- **Two people on the left edge, 13:29–13:35.** They were visible from 13:29.5 (about 70 m). The detector first boxed them at 13:32.5 (about 28 m). The coach said "Person ahead. Brake now" at 13:35, close up.
- **People in the lane at 2:34.** The detector boxed them at about 30 m, but no instruction followed.

There were two separate causes.

### 1. The coach read the camera image with the wrong geometry (introduced in 0.8.1)

0.8.1 made the road camera fill its wide panel (`PreviewView` FILL_END). CameraX then crops the analysis frames to the same view: the bottom 72 % of the 4:3 frame. The risk coach still assumed an uncropped 4:3 frame:
- it assumed the level horizon sits mid-image;
- it assumed a radian spans 0.96 image heights.

In the crop, a level horizon sits about 0.31 of the way down, and a radian spans about 1.23 image heights (S24 Ultra main camera).

The phone was pitched up about 18° (the measured horizon sat at 0.70 of the preview all drive). The app computed the horizon at about 0.81. Beyond about 13 m, a road user's feet then appeared to be above the horizon. The "is it standing on the road?" check (feet versus apparent size) dropped them as billboards. Only road users whose feet were in the bottom fifth of the image skipped that check, about 10–14 m away.

`tests/drive_coach_tests.cpp` reproduces this. A person stands in the lane and the learner approaches at 40 km/h. With 0.8.1's reading of the crop the first instruction comes at 12.8 m; with the image's real optics, at 45 m.

### 2. The detector could not see far: the camera gave it 640×480

CameraX's default analysis stream is 640×480. The 0.8.0 "zoom" (the detector mosaic) was running: its 2× band of the far road is what boxed the people at 28–30 m. But a 2× view of a 640-wide frame has no more detail than the camera gave. A person 60 m ahead is about 12 px tall in that frame. The detector needs about 20–25 px: in the Nov 8 frames it boxed people at 23–27 px and missed them at 18 px. The preview you watch is full HD, which is why people were visible to you long before the detector found them.

## How far ahead the coach must see

The time from first sight to the learner acting:
- confirming over a few frames: ~0.4 s;
- starting and saying the key words: ~1.4 s;
- the learner's reaction: ~1.2 s.

Then braking: comfortable at 3 m/s², or hard at 6 m/s². The distance needed to stop for someone standing in the lane:

| Speed | Comfortable stop (3 m/s²) | Hard stop (6 m/s²) |
|---|---|---|
| 20 km/h | 22 m | 19 m |
| 30 km/h | 37 m | 31 m |
| 40 km/h | 54 m | 44 m |
| 50 km/h | 74 m | 58 m |
| 60 km/h | 96 m | 73 m |

An oncoming bus on a narrow road needs about 4 s before it reaches the car to "keep left, ease off": about 80–90 m at 40 km/h each.

## What 0.8.3 changes

1. **The coach reads boxes through the image's own optics.**
   - The app sends the analysed crop's focal lengths and optical centre with every frame (`ld_camera_geometry`), from the camera's calibration, the stream size and the crop rect.
   - The horizon is `cy + fy·tan(pitch)`.
   - The risk coach maps every box to the 4:3 frame its thresholds were tuned on before judging distance, path and motion.
   - Edge, bonnet and mask tests stay in raw image rows.
2. **The camera gives the detector 1280×960 frames**, so the far view has the camera's own pixels.
3. **One detector pass sees the road at three scales.** The 320×320 detector image holds:
   - the whole road at ¼ scale, starting 35 % of the frame height above the horizon (it was 50 %; sky is not a road user);
   - a 2× view of the middle half of the frame;
   - a 4× view of the middle quarter, at native resolution.

   A person 60 m ahead is ~23 px tall in the 4× view, against ~6 px in the road band. Inference time is unchanged; preparing the image samples about 100k pixels whatever the frame size.
4. **The zoomed views follow the road.**
   - The 2× view is aimed where the road will be in 2.5 s and the 4× view where it will be in 5 s.
   - Navigating, the aim comes from Google's route line (`RouteLine::aim`), which knows a bend before the car turns into it.
   - Otherwise it comes from the turn rate: a constant curve puts the road `yaw·t/2` off-centre.
5. **Downscaled views are box-filtered** (2×2 at the footprint centre). Nearest-neighbour sampling of a 1280-wide frame at ¼ scale turns wires, railings and stripes into noise.

How far the detector can now see (S24 Ultra main camera, ~850 px per radian at 1280 wide; 20–25 px needed):

| Road user | 0.8.1 (640 wide, 2× band) | 0.8.3 (1280 wide, 4× view) |
|---|---|---|
| Person or rider (1.6 m) | ~28 m | ~55–68 m |
| Car (1.5 m) | ~30 m | ~60–70 m |
| Bus or truck (3 m) | ~55 m | ~100 m+ |

## Results on the Nov 8 drive

**How the replay was set up.**
- **Source:** the app's own preview from your screen recording, at two sizes:
  - 640×346, what the phone's 0.8.1 detector got;
  - 1064×574, the closest the recording has to the phone's new 1280-wide frames.
- **Horizon:** it moved during the drive: ~0.60 until 2:30, ~0.80 at 2:40–3:00 as the mount tilted back, then ~0.70. Both runs follow it over time. The 0.8.1 run uses the horizon 0.8.1 computed from the same tilt.
- **Tool:** `tools/evaluate_long_range.py`. The detector sees 8 frames a second around each moment and 2 a second over the rest of the drive.

**Road users boxed beyond 30 / 50 / 70 m over the whole drive** (distance from apparent height; no ground truth):

| | People | Two-wheelers | Vehicles |
|---|---|---|---|
| 0.8.1 | 18 / 1 / 0 | 0 / 0 / 0 | 316 / 83 / 11 |
| 0.8.3 | 130 / 29 / 8 | 70 / 13 / 0 | 511 / 235 / 103 |

**First steady detection** (boxed in at least 3 of 5 consecutive frames, as the coach confirms):

| Moment | 0.8.1 | 0.8.3 |
|---|---|---|
| People in the lane (2:30–2:36) | 2:35.1, ~9 m | 2:29.6, ~41 m (5.5 s earlier) |
| Man on the right verge (2:46 false "Brake now") | 2:45.5, ~11 m | 2:40.9, ~45 m |
| Scooter rider (6:34) | 6:27.4, ~13 m | 6:26.3, ~22 m |
| LEADER bus on the bend (12:54) | 12:54.8 | 12:54.5 |
| Two people at the left edge (13:30) | 13:31.5, ~30 m | 13:31.1, ~30 m |
| Truck (15:24) | 15:22.0 | 15:20.3, ~87 m |

The bus and the two people on the curve barely change in this replay:
- **The recording, not the phone, sets the limit.** It is H.264-compressed and about 1064 px wide. Around 13:30 the people at the left edge of the curve are ~12 px tall in it; the phone's own 1280-wide frames would make them about 20% taller.
- **The bus was first boxed at 12:54.5–12:55 in the replay. On the phone it was 12:57.5.** The rest was lost on the phone itself: it was hot ("Basic detection", "Camera delayed") and its frames ran late. That is the next fix.

**What the coach said** (the whole brain replayed on the drive):
- the app's own displayed speeds;
- the horizon over time;
- 0.8.1 with the 640-wide frames and the horizon it computed;
- 0.8.3 with the 1064-wide recording and the preview crop's optics.

| Moment | Phone (0.8.1, on the road) | 0.8.1 replay | 0.8.3 replay |
|---|---|---|---|
| People in the lane (2:34) | "Uphill. Steady power" | nothing | 2:36.2 "Person on the left. Ease off… Give them space. Sound the horn." |
| Scooter rider (6:34) | 6:34 "Person ahead. Brake gently" | 6:32.0 "Two-wheeler ahead. Brake gently" | 6:29.9 "Oncoming two-wheeler. Keep left. Ease off" |
| Oncoming vehicle (11:36) | 11:36 | nothing | 11:34.9 "Oncoming vehicle. Keep left. Ease off" |
| LEADER bus (12:54–12:58) | 13:00 (after it passed) | 12:57.6 | 12:56.4 "Oncoming truck. Keep left. Ease off" |
| Two people at the left edge (13:30) | 13:35 "Brake now" | 13:34.8 "Brake now" | 13:34.8 "Brake now" |
| Truck (15:21; the phone was paused by heat) | – | nothing | 15:21.6 "Vehicle ahead. Brake gently" |

Over the whole drive:

| | 0.8.1 replay | 0.8.3 replay |
|---|---|---|
| Instructions | 23 | 59 |
| About road users | 20 | 56 |
| "Brake now" | 3 | 7 |

Of the seven "Brake now" in the 0.8.3 replay:
- **About right:**
  - a man walking in the road (8:34);
  - a car pulling out (8:45);
  - a two-wheeler ahead (9:04).
- **Too strong:**
  - the man on the right verge (2:44; the phone said it too);
  - a car overtaking on the right (11:26);
  - a pickup at the left (12:15);
  - the two people at the left edge (13:34.8; the phone said it too).

**Regression check on the Nov 5 instructor drive** (`tools/instructor_benchmark.py`):
- It still matches 7 of the instructor's 17 actions (0.8.2: 7), with 40 instructions (0.8.2: 40).
- One fewer instruction agrees with the instructor (7 against 8): the gear-down before the 238 s turn lost its slot to a road-user warning.
- The median lag behind the instructor rose from 0.7 s to 1.2 s.
- The head-on motorcycle at 315 s now gets "Two-wheeler ahead. Brake now" and then "Person ahead. Brake now" 0.4 s later: the same rider, seen as two road users.

## Warnings for road users seen far away

Seeing further exposed rules that were tuned while far road users were (wrongly) invisible.

**"Brake now" for a crossing vehicle**
- Up to 0.8.2 a crossing road user got "Brake now" anywhere within twice the stopping distance plus 10 m (~78 m at 40 km/h). In the Nov 8 replay with the geometry fixed, cars crossing junctions 50–80 m ahead got "Vehicle crossing. Brake now".
- 0.8.3 keeps "Brake now" for crossings within one stopping distance plus a margin (1.2× + 5 m, by the farther of the two distance cues). Farther away it says "Vehicle crossing. Brake gently. Be ready to stop. Let them pass."

**The optical-flow collision time for a far road user**
- The detector's frame-to-frame flow gives a time to collision. It is trusted only for road users close to the car: a tall box, or feet well below the horizon. 0.8.3 measures "well below" from the horizon, not from the image bottom.
- With the phone pitched up, a person 28 m ahead stands low in the image, so their noisy flow time (2.0 s) made "Person ahead. Brake now" (Nov 8, 7:07).

Together these two changes removed three "Brake now" for distant roadside people (6:21, 7:07, 16:04) and two for far crossings in the replay.

**Still open**
- **Where the car is heading in the image.** The path rules assume the camera looks straight along the car. On Nov 8 the two estimates of the car's heading disagree: the scenery's flow on straight stretches says ~0.42 of the width, and where the road vanishes says ~0.59. Several degrees off-centre would make a person walking at the road edge look "in the path" close up. That fits the "Person ahead. Brake now" at 2:46, which 0.8.1 also gave on the phone. Fix: learn the heading while driving straight, from the gyroscope and the scene's flow.
- **Head-on riders.** On Nov 5 (315 s) a motorcycle coming head-on down a narrow lane gets "Two-wheeler ahead. Brake now" rather than "Oncoming two-wheeler. Keep left. Ease off".

## Limits

- **Not yet tried on the phone.** The replays use your screen recording: compressed, 1064 px wide (the phone's frames will be 1280 wide and sharper), and the app's own boxes are drawn on it.
- **Distances in replays assume the S24 Ultra main camera** (23 mm equivalent): fx = 0.665, fy = 1.228, cx = 0.5, cy = 0.306 of the preview crop. The phone uses its own calibration.
- **The 4× view covers about 21° of the view.** On a bend it relies on the route or the turn rate to point it. On a hill crest the road ahead leaves the horizon band; the map grade could aim it vertically too (not done).
- **Heat.** Under heat 0.8.1 shed the distance model at Android "moderate" and stopped coaching at "critical", and frames ran late. 0.8.3 does not change that; it is the next fix.
- **Speed.** Wrong speeds (Nov 8) shrink how far ahead the coach looks (6 s at the current speed); also a separate fix.
