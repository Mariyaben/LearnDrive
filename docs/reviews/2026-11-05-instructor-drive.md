# 2026-11-05: instructor-labelled drive (0.7.4 on the phone)

This was a 6 min 14 s supervised drive on narrow Kerala lanes, a main road, junctions and a speed bump. The source is a screen recording of the app.

The owner subtitled every moment that needed an instruction with what the instructor said, and added notes such as "app was right but late" and "it's a junction". There is no GPS or diagnostics journal for this drive. The soundtrack does not contain usable instructor or app speech (no reliable match with the app's recorded voice, no detectable indicator ticks), so subtitle start times are the instructor timing.

Analysis artefacts are in `build/drive-tests/nov5/` (not committed):

| File | Contents |
|---|---|
| `instructor.csv` | 18 instructor instructions |
| `phone_cues.csv` | the 10 app cues read from the screen |
| `camera.mp4` | the camera view cropped from the recording |
| `speeds.csv` | the on-screen speed |
| `compare.py` | the per-event timing comparison |
| `replay-*/` | replays |

## What the phone did

The app spoke 10 instructions; the instructor gave 18. Within ±3 s, **1 of 13 instructor actions** was matched (`tools/instructor_benchmark.py`). The intents the instructor used and the app never did:

| Intent | Instructor | App |
|---|---:|---:|
| Gear | 6 | 0 at the right moments; once "change up" just before the instructor said "downshift for the junction" |
| Horn | 5 (bend, blind turn, junctions, motorcycle on the left) | 0 |
| Junction ahead | 4 | 0 |
| Speed bump | 1 | 0 |

Road-user cues were right in kind but late: the pedestrian, the bus, and the motorcycle on the left. The owner annotated two of these "right but late".

## Why

1. **The camera sees mostly sky.**
   - The phone points about 12° up, so the horizon is ~72% of the way down the image. The road is the bottom fifth; trees and sky are the rest.
   - The suction-mount arm covers the right quarter of the view, where oncoming traffic appears.
   - Distant road users are a few pixels tall, and the speed bump was never in view.
2. **Distance was judged as if the camera were level.**
   - The risk coach places road users by where they touch the road in the image, assuming the horizon at mid-image.
   - With the horizon at 72%, everything on the road looked "near". That is why a zoomed detector pass (which sees further) produced "Brake now" for road users still far away.
3. **The phone was not the bottleneck for road users.**
   - Replaying the same video on the computer, with no phone lag, gives the same lateness: pedestrian 40.5 s vs 39.2 s on the phone, bus 152.7 s vs 150.4 s.
   - "Camera delayed" was shown 14% of the time, in short bursts (median 0.2 s), rising to 25–46% in the last two minutes, which looks like heat.
   - The lane model added in 0.7.4 shares the detector's thread.
4. **Junctions, bends and bumps come before they are visible.** The instructor names them from knowledge of the road, and the app's only source for that without a destination is OpenStreetMap.
   - The junction scout never spoke on this drive. The likely causes are a failed Overpass download or lanes missing from the map.
   - The app only logged that status, so it cannot be told from the recording.
5. **Indian road practice was missing.** The instructor sounds the horn at bends, blind turns, junction entry, joining a main road and when passing a motorcycle at the left edge. The app had no horn instruction at all.
6. **The on-screen speed is implausible.**
   - It rises almost monotonically from 0 to 16 km/h over four minutes and then holds 14–15 km/h, through the bump and the "foot on the brake" junction entries.
   - Image motion shows near-stops around 120–130 s and 300 s that the speed never reflects.
   - A port of the fusion filter recovers within ~10 s even with a wrong IMU axis and poor GPS accuracy. So GPS speed updates were probably sparse or rejected under tree cover.
   - This needs the diagnostics journal to confirm. Speed drives gear advice, junction timing and stopping distances.

## Changes (0.7.5)

**Horizon-aware road-user distance, confirmed by size.**
- The camera pitch (gravity in phone axes; the rear camera looks along −z) gives the horizon row: 0.5 + 0.96·tan(pitch).
- The risk coach places ground contact relative to it and estimates distance as about 1.2 / (row − horizon) metres.
- "Near" means within 25 m, or 2.5 stopping distances at speed.
- "Brake now" is only for road users within 1.5 stopping distances plus 6 m; "slow down" within 3 plus 12.
- Both limits must also hold for the distance implied by apparent size: person 1.7 m, rider 1.6 m, car 1.5 m, bus or truck 3 m tall.
- Replays of filmed video set the horizon with `--camera-horizon`.

**Horn rules (Indian practice, on by default, `ld_coaching_horn_rules`).**
- Junction entry: "Clutch down. Look both ways. Sound the horn."
- Passing a person or two-wheeler at the road edge: the horn is added to "give them space".
- A sharp bend ahead: "Sharp bend ahead. Sound the horn and keep left."

**Road ahead from the map.**
- The Overpass query also fetches `traffic_calming` nodes.
- The junction scout reports the first mapped bump and the first sharp bend (≥55° of turning within 35 m) before the junction.
- The coach says "Speed bump ahead. Slow down, clutch down and change down" about 6 s before a bump, once per bump.

**Visible map status.**
- With no destination, the "Free practice" card shows the road-map state: loading, number of roads and bumps, or the failure.
- It also shows the next junction, bump or sharp bend and its distance.
- This makes the map side checkable from the next screen recording.

**Mount advice.**
- The status line asks to tilt the camera down (above 6° up), up (below −20°), or to level it (over 8° of roll), adjusting only when parked.
- The old check only rejected a camera pointing almost straight up or down.

**Less load on the detector thread.** The lane model runs at most at 2 Hz, and only on frames that are still fresh.

**Gear advice.** "Change up to second" now comes above 10 km/h instead of 12, matching the narrow-road instructor practice on this drive.

## Results

Replays of `camera.mp4` used the on-screen speed and the guided automatic coach, scored with `tools/instructor_benchmark.py` against the 17 instructor actions. The horizon is set to 0.72 for this video.

| Run | Instructions | Instructor actions matched (±3 s) | "Brake now" | Horn |
|---|---:|---:|---:|---:|
| Phone, 0.7.4 (as recorded) | 10 | 1 / 17 | 2 | 0 |
| Replay, 0.7.4 policy | 9 | 1 / 17 | 2 | 0 |
| Replay, 0.7.5 | 8 | 1 / 17 | 0 | 2 |
| Replay, 0.7.5 + zoom (still off in the app) | 22 | 4 / 17 | 4 | 3 |

**What improved.**
- The motorcycle on the left at 256.6 s now gets "Person on the left. Ease off the accelerator. Give them space. Sound the horn." 0.9 s after the instructor's "Honk the horn".
- With 0.7.4, the two "Person ahead. Brake now" calls (118.4 s and 257.3 s) were for people at the left edge, not in the path. They are gone.

**What did not improve.** Most instructor actions here are map, speed or knowledge based: four junctions, a bend, a blind turn, the bump, six gear changes and moving off.
- This video has no GPS, so the map-based instructions (junction, bend, bump) and the horn at junctions cannot be replayed.
- Gear advice depends on the speed, which was implausible on this drive.

**Zoom.**
- With the horizon and size cues it matches 4 instructor actions, against 1 without, but only two of them for the instructor's reason:
  - the motorcycle on the left, with the horn (256.6 s);
  - "go slow" on the narrow road, via the oncoming bus at 299 s.
- The other two are a "slow down" said for a different road user near the bump (190 s) and the turn (238 s).
- It still says "Brake now" four times for road users 30–40 m away. Two things inflate urgency in these replays:
  - the on-screen speed never drops, which triggers the "not slowing, escalate" rule;
  - the camera tile's field of view differs from the phone's analysis frames.
- It stays off in the app until a drive with a working speed and diagnostics shows no false "Brake now".

**Regression check on the 2026-11-04 drive** (horizon 0.68): 25 → 23 instructions.
- The two dropped calls were "Brake now" for distant road users; the one at 272.9 s was already judged over-urgent.
- A roadside "give them space" at 212 s was also dropped.
- The person on the right at 367 s now gets the horn.
- Second gear is advised earlier.

## Not done, and why

**Indicator by sound.** The owner's note asks to detect the indicator and anticipate the turn. This recording's soundtrack has no detectable ticks. The opt-in instructor recording captures raw in-car audio, which is what a detector has to be built and validated on.

**Speed.** This needs the diagnostics journal (it records GPS quality) from the next drive. Changing the filter blind could make it worse.

**Zoom in coaching.** It stays off until replays show it adds early, correct instructions without false ones.

**Detector for Indian roads.** Speed breakers, potholes, animals, signs and autorickshaws need a model trained on Indian road images.

## For the next drive

1. Mount the phone lower and tilted down until the status line stops asking. Keep the suction arm out of the camera's view.
2. Start local diagnostics, with instructor audio, before driving. Export both afterwards.
3. In the screen recording, check that the "Free practice" card shows the road map loading and a junction, bend or bump ahead.
