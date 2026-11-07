# Detector mosaic: seeing further ahead at no extra cost (0.8.0)

## The problem

The road-user detector (YOLOv8n) sees each camera frame shrunk to 320×320. The phone's camera
frame is 640×480. Two things are wasted:

- **Grey padding.** Letterboxing a 4:3 frame into a square leaves 80 rows grey (25%); a 16:9 frame leaves 140 rows grey (44%).
- **Sky.** The rows well above the horizon (sky, tree canopy, building tops) never hold a road
  user. With the phone pitched up, as on the Nov 5 and Nov 6 drives, that's a third of the frame.

A person 50 m ahead is then only a few pixels tall, below what the detector can see. Road users
were found late, and coaching came late. A second "zoom" detector pass (tried in 0.7.4) doubles
the work on a phone that is already warm and lagging, so it stayed off.

## The design

One detector image, two views, one inference (`ld_preprocess_mosaic`, `preprocessMosaic` in `core/src/vision.cpp`):

- **Road band.** The full frame width at the usual scale, from half the frame height above the
  horizon down to the bottom. A road user's head is that high only within about 1 m on a level
  road (about 3 m for someone on a raised verandah). The band always keeps at least half the image.
- **Zoom band.** The rows left over, which used to be grey. It shows 2× the road band's scale over
  the middle half of the frame, around the horizon, where road users far ahead are. On the
  phone's 640×480 camera this is native resolution.
- **The horizon follows the phone.** It comes from the camera's measured pitch, so the zoom
  stays on the road even with a badly tilted mount.

**Distances stay exact.** Before decoding, every zoom-band box is mapped back to frame pixels
(`mosaicToFull`):
- distance from the foot position and from the box height uses the same frame geometry as before,
  and is sharper for far objects;
- a box cut by a band edge is dropped, because the other view covers that object;
- a box that only overshoots the frame bottom (a passing bus) is kept and clipped to the frame;
- a box cut off at the top of the road band uses only its foot position for distance.

Tested in `tests/mosaic_tests.cpp`: a far car found in the zoom band decodes to within a pixel of
its true frame position and size.

## What it changes on real drives

Measured with `tools/evaluate_detector_zoom.py --mosaic`, against the plain letterbox, with the same model and one inference each.
- **Kerala clips:** screen recordings of the app's camera view, so already downscaled and blurrier than the phone's own frames.
- **Bangalore clip:** a dashcam clip, for local research only.

| Clip | Extra road users found in the car's path (seen in ≥ 3 of 5 frames) | Seen earlier | Median head start | Time per frame |
|---|---|---|---|---|
| Nov 5, Kerala (camera pitched up) | +119 vehicles, +19 people, +5 two-wheelers | 16 of 74 objects | 0.4 s | 18.6 vs 20.2 ms |
| Nov 4, Kerala | +67 vehicles, +7 people | 15 of 102 | 0.4 s | 20.7 vs 21.2 ms |
| Bangalore traffic | +173 vehicles, +21 people | 9 of 15 | 1.4 s | 22.5 vs 23.7 ms |

Inside the road band the two images are equivalent: borderline detections near the 0.40
confidence threshold flip both ways (Nov 5: 67 lost, 78 gained; Bangalore: 16 lost, 47 gained).

The mosaic also stops "seeing" the phone's own suction-cup mount as a truck or person (frame
after frame on Nov 4) and the dashboard as a vehicle.

One real loss remains: a truck right beside the car, half hidden behind the mount (Nov 4, 41 s).

## Making earlier detection mean earlier coaching

Seeing further only helps if the coach is allowed to act on it. The risk coach changes in the same release:

- A road user in the path counts from about **6 s ahead at the current speed** (at least 25 m),
  not a fixed 25 m. Awareness cues reach about 5 s ahead, instead of the old 15 m.
- Far away, the distance is the geometric mean of the two cues (foot position and size) when they agree.
- A small box whose foot position and size disagree by more than 2.2× isn't standing on the road
  (a person on a billboard, a raised object), so it's ignored.
- "Brake now" still needs both cues inside stopping distance plus a margin.
- A box fixed at the side edge of the frame while the car moves (the phone mount, a pillar) is ignored at any height.

## Replay of the Nov 5 drive against the instructor

The full brain was replayed on the Nov 5 camera recording (Guided profile, automatic coaching, the
drive's own speeds, camera horizon 0.72, the drive's seven turns as a synthetic route), then
compared with the instructor's 18 captioned instructions (`tools/instructor_benchmark.py`, 3 s window):

| Brain | Instructor instructions with a matching app instruction |
|---|---|
| 0.7.5 | 4 of 18 |
| 0.8.0 without the mosaic | 5 of 18 |
| 0.8.0 with the mosaic | 8 of 18 |

- **New with the mosaic:** "There is a bus on the right" (149 s) is now matched by "Oncoming bus.
  Keep left. Ease off" (152 s). "Go slow" (298 s) is matched by "Oncoming truck. Keep left. Ease off" (299.5 s).
- **Riders seen head-on.** The detector often finds only the rider's body. A "person" approaching
  faster than anyone walks or jogs (own speed over 3.5 m/s after removing ours), or one sitting on a
  detected bike, is now judged as a two-wheeler. At 218 s that turned "Person ahead. Brake now" into
  "Oncoming two-wheeler. Keep left. Ease off". "Brake now" on this drive went from 4 to 2; one of
  the two is an autorickshaw about 5 m ahead.
- **Still behind the instructor.** On matched road-user moments the app speaks about 1–3 s after
  the instructor, who sees a bus coming before the app has confirmed it over several frames.

## Limits

- **Not measured on the phone yet.** The Kerala clips are downscaled recordings; the phone's own frames should do better.
- **No ground-truth labels.** "Extra road users found" is unlabelled; the example frames in `build/detector-eval/` were reviewed by eye.
- **The horizon must be right.** It comes from the phone's tilt sensor, and a wrong horizon puts the zoom band in the wrong place.
