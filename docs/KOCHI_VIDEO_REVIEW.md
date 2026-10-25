# Kochi/Munnar dashcam replay: findings and corrected build

The supplied clip was decoded and processed in full: **40.033 seconds,
1,201 frames, 480 × 360, 30 FPS**. This segment shows an urban road, pedestrians,
a junction, oncoming trucks/cars/motorcycles, a cyclist and auto-rickshaws.
The filename does not establish a hill or road grade in this segment.

The 0.3.2 shared-coordinator regression repeats all 1,201 frames at 110-degree
assumed FOV with six-second startup pre-roll and 180 ms simulated latency. Its
five-cue timeline and candidate tick counts match the corrected baseline exactly;
no emergency candidates occur. Evidence and the new decision trace are under
`build/video-review/shared-pipeline-warm-180ms`. This verifies the orchestration
refactor; it does not resolve the missed cues or establish accuracy. See
[the execution record](EXECUTION.md).

## Result

The previous collision-guidance path failed this replay. Uncorroborated neural
lead estimates produced repeated collision speech even in visibly clear areas.
Alternating emergency categories also bypassed the intended repeat interval.
Those findings required a code correction, not a different assumed lens angle.

| Assumed horizontal FOV | Before: all spoken events | Before: collision events | After: all spoken events | After: collision events |
| --- | ---: | ---: | ---: | ---: |
| 90° | 76 | 75 | 6 | 0 |
| 110° | 86 | 84 | 6 | 0 |
| 130° | 83 | 79 | 6 | 0 |

These comparisons start the app's startup announcement at the beginning of the
clip. The six corrected events include that announcement and five contextual
cues. All three corrected timelines agree. Their non-emergency prompts satisfy
the 3.5-second spacing and 10-second duplicate lock.

A second scenario lets startup speech finish six seconds before the clip and
adds **180 ms of simulated frame-processing delay**. Its five accepted cues are:

| Video time | Actual app phrase |
| --- | --- |
| 1.52 s | Road user ahead. Slow down and give them space |
| 6.05 s | Traffic ahead. Keep a safe gap |
| 19.12 s | Traffic ahead. Keep a safe gap |
| 24.45 s | Road user ahead. Slow down and give them space |
| 29.13 s | Traffic ahead. Keep a safe gap |

This is a **more conservative result, not demonstrated optimum guidance**.
Zero collision prompts does not prove there were no hazards or that the system
would detect a real collision. Detection/optical failure can inhibit a true alert.

## Changes made

- Neural leads remain available as diagnostic spatial estimates. Before entering
  collision policy, they now require a fresh road-user detection at the projected
  lead position with separately computed optical closing evidence. A matching
  detection alone does not establish time to impact.
- An urgency downgrade no longer restarts emergency speech. A stronger reflex,
  including braking-insufficient escalation, still interrupts immediately.
- Brief gaps no longer reset the standalone arbiter's emergency repeat timer.
- New native tests cover unsupported neural leads, matching boxes without closing
  evidence, urgency downgrades and genuine braking escalation.
- Added a reproducible headless native replay bridge and video-review tool.

The original camera models and their outputs were retained. Cached outputs were
reused for before/after policy comparisons; there is no clip-specific hazard
table in the driving engine and no suppression based on this filename.

## Visual review and remaining coverage gaps

| Interval | Useful attention cue from visual review | What the replay establishes |
| --- | --- | --- |
| 0–3 s | Watch the pedestrians near the left road edge | Warm-start road-user cue at 1.52 s is useful; cold startup speech masks this opportunity |
| 3–6 s | Leave space for the oncoming truck while holding the lane | The 6.05 s generic traffic cue is late for that truck; it is not proof of timely truck recognition |
| 8–12 s | Watch the junction, nearby pedestrians and passing motorcycle | No distinct junction cue; generic traffic speech continues through part of this interval |
| 19–26 s | Watch oncoming traffic and give the cyclist room | Traffic cue at 19.12 s and road-user cue at 24.45 s precede the relevant encounters |
| 29–32 s | Leave clearance for the oncoming truck and cars | Traffic cue at 29.13 s is relevant; direction-specific oncoming guidance is absent |
| 34–40 s | Watch the pedestrian near the left edge and the rickshaws ahead | No additional spoken cue; this remains a coverage gap |

The confidence gates accepted road geometry in only **1 of 1,201 frames** in
each FOV experiment. Reliable bend guidance was therefore not demonstrated.
The junction turn must not be presented as a successful bend-detection test.
Some object classes are ambiguous: the detector produces overlapping truck/bus
labels and labels a rickshaw as a bus in parts of this low-resolution clip.

There is no calibrated camera matrix, mounting orientation or recorded IMU.
The three FOV values and level camera pose are assumptions, not measurements.
Source video contains lens distortion that the replay cannot calibrate from
metadata. Speed values were manually read once per second from the visible
integer km/h overlay and held between samples; they range from 22 to 35 km/h.
Those rounded GPS readings were not differentiated to fabricate braking IMU.
Slope and exact gear/RPM guidance cannot be validated with this recording.

This replay executes the shared C++ inference/preprocessing and guidance code.
It does not execute Android CameraX, MediaCodec, AudioTrack, audio focus or a
phone's thermal scheduler. The voice track uses the actual cached WAV clips
and simulates their durations/interruption policy, assuming audio is available.
The Android MP4 screen still uses a separate playback/telemetry route; this
report must not be treated as verification of that screen or of automatic
speed-overlay extraction in the APK.

## Artifacts

- Corrected APK: `build/LearnDrive-0.3.1-debug.apk`, version code 14.
- APK SHA-256: `5096de70d99d258ae62f7523f157b297ea2d12c3f81806b2670bf4864b2011ff`.
- Warm-start annotated video and cached speech:
  `build/video-review/updated-warm-180ms/guidance-review.mp4`.
- Detailed report and timeline:
  `build/video-review/updated-warm-180ms/report.json` and `guidance.csv`.
- Before/after FOV experiments: `build/video-review/baseline-{90,110,130}`
  and `build/video-review/updated-{90,110,130}`.
- Input speed observations: `scenarios/kochi-munnar-speeds.csv`.

All eight CTest targets pass, including 118 core tests and 25 dual-brain cases.
The Android build passes for ARM64 and x86-64; its signature, all model/license
assets, 43 voice clips and matching ORT libraries verify successfully. Phone
latency, thermal behaviour, true-hazard recall and physical audio timing remain
unverified. iOS lacks the independent semantic producer needed by the new
neural-lead gate; its neural collision alerts remain inhibited pending that work.

## Reproduce

Build `learndrive_replay` with spatial ORT enabled using the host setup in
[INTEGRATION.md](INTEGRATION.md), then run:

```powershell
python tools/review_video.py "d:\Downloads\Kochi to Munnar Full Dashcam Drive _ Scenic Kerala Road Trip (GPS Coordinates Included)Uncut Dashcam_1605_1645.mp4" --speeds scenarios/kochi-munnar-speeds.csv --hfov 110 --fps 30 --pre-roll-seconds 6 --latency-ms 180 --output build/video-review/updated-warm-180ms --render
ctest --test-dir build/native-spatial --output-on-failure
```

The replay writes the native-library hash, policy source hashes, video hash,
assumptions, observations, accepted audio events and host inference timings.
Host inference timings are not substituted for mobile performance measurements.
