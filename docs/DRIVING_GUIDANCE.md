# Camera driving guidance

Android 0.4.1 uses the unified map/camera dashboard. Ordinary road coaching uses
measured telemetry directly: unavailable speed stays unknown, and mock locations
cannot qualify road speed or route evidence. Location is requested during road
setup; declining it still permits basic camera attention. Manual speed belongs
only to explicit screen-video, MP4 and Desk tests. Detailed diagnostics are in Lab
settings. See [the product/build checkpoints](product/UNIFIED_DRIVING_COACH.md).

For Android 0.3.4, [SETUP_FLOW.md](SETUP_FLOW.md) supersedes the startup/default
instructions below: road coaching requires explicit Start, level-ground reference
confirmation is optional and separate, and screen testing must be selected in lab
settings each launch. The earlier versions described here remain historical.

Install `build/LearnDrive-0.1.8-debug.apk`, open Camera and allow camera access. Startup speech, road-user/obstacle guidance, bend cues, slope cues and conditional gear guidance all run in Camera mode. GPS remains optional.

If the preview is live but boxes disappear, read the **Vision** status. **Vision active: 0 road users** means inference is running but the current frame has no accepted person/vehicle detection. **Vision delayed** shows the number detected and the original capture age. Slow completed results remain visible for up to two seconds after completion, with a three-second capture-age limit; they are not treated as fresh hazard evidence. The shared advice rules still reject camera evidence older than 350 ms.

The normal Camera inference cap is four frames per second. Warm phones use longer intervals; severe thermal status permits one analysis every 1.5 seconds instead of silently disabling detection. Critical or higher status pauses detection, displays a cooling message and resumes after the status falls. These are scheduling caps, not measured phone performance. See [Android thermal status](https://developer.android.com/reference/android/os/PowerManager) for the platform states.

**Vision unavailable** preserves the camera/model error instead of hiding it behind an old result. **No camera frames received** identifies a stalled analyzer separately from loading models. Tap **Restart detection** to reopen Camera and reload the offline models; this also clears a cached model-initialization failure. Camera and model failures are logged under `LearnDriveVision`. The reported phone failure has not been reproduced with device logs, so heat, delay or initialization failure remains a possible cause rather than a confirmed diagnosis.

For the current test with the camera pointed at a road video, leave **Camera pointed at a road video (test)** ON and set **Video / fallback driving speed** to the speed depicted in the video. For a crawling vehicle, select its low speed rather than leaving the default 30 km/h. The phone's own GPS speed is ignored in this test mode. This speed is a manual setting, not an estimate from the video.

Enable **Manual vehicle · gear guidance** for conditional gear advice. It defaults to ON for an unset preference; an existing OFF preference remains OFF. Leave it off for an automatic. Bend detection still uses the actual camera image. A yellow line shows the fitted road shape, and **Road** status distinguishes a left curve, right curve, straight road or unclear shape. Fill the camera view with the road video, minimizing the player's borders and controls.

**Use normalized phone slope** defaults to ON. Point the rear camera forward in the intended fixed mount, park on level ground, tap **Normalize phone plane · level ground · hold still 5 s**, and keep the phone still until **Phone plane normalized** appears. Calibration creates a level right/up/backward coordinate frame, removes the initial mount pitch and landscape rotation, and estimates steady gyroscope/linear-acceleration offsets. The camera must face along the road; a phone facing the ground or sky cannot establish the forward direction.

Camera shows estimated uphill/downhill angle in degrees and grade percentage, using `grade percent = 100 × tan(angle)`. For example, a measured +5-degree incline is approximately +8.7% grade. The value is relative to the level-ground reference, not a survey measurement. A normalized, rigid mount makes phone inclination an estimate of the vehicle's current grade; hand movement, vehicle body pitch and sensor error affect it. Re-normalize after moving the mount or resuming interrupted motion data. Calibration on a hill would zero that hill rather than recover its absolute grade.

In Camera video-test mode, speed still uses the video setting while phone slope uses the actual sensors and retains their calibration, freshness, braking and shock state. When filming a road video, this measures the phone in your hand, not the vehicle in the video. You can tilt the normalized phone gently to exercise hill cues. For explicit simulated hill inputs, turn **Use normalized phone slope** OFF to reveal the **Video slope** dropdown. The simulated slope resets to level when the app starts. Keep **Slope guidance** ON for either source.

For a phone mounted in a real vehicle, keep video test OFF. Mount the phone firmly and point the rear camera along the road. An optional level reference must be confirmed and set while parked on level ground. Ordinary road coaching uses fresh measured speed; there is no manual road-speed fallback in Android 0.4.1. Basic camera attention remains available when speed is unknown. Simulated inputs remain isolated in the explicit test modes.

| Condition | Spoken guidance | Evidence |
| --- | --- | --- |
| Left/right bend, manual enabled | Set speed and gear before the bend; stay in your lane | Sustained curvature of a bounded road corridor or paired lane markings |
| Left/right bend, manual disabled | Ease off early at higher speed; lane/look-through wording at low or unknown speed | Same visual geometry; speed selects the wording |
| Uphill/downhill | Short cue for the angle band below | Fresh normalized pitch sustained for 1.5 seconds, or an explicit video test slope |
| Shift up/down one gear | Shift when safe | Fresh current gear/RPM, engine under load, and RPM outside vehicle-specific bands |
| Image-only obstacle | Keep a safe gap and be ready to stop; conditional gear wording when enabled | Central image occupancy; does not establish distance or approach |
| Obstacle at low or unknown speed | Keep space; no routine command to slow down | Advice speed at or below 10 km/h, or unavailable |
| Reliable imminent collision | Brake now while moving; stay alert if stationary | Fresh, confident approach evidence and TTI below 1.2 seconds |

The following are app coaching bands awaiting phone and vehicle validation, not official road classifications. Positive pitch means uphill and negative pitch means downhill relative to the normalized level plane. Ranges use absolute angle; 30 degrees is the supported limit.

| Angle magnitude | Uphill cue | Downhill cue |
| --- | --- | --- |
| Below 2° | Quiet | Quiet |
| 2° to below 5° | Gentle uphill. Steady power. | Gentle downhill. Watch speed. |
| 5° to below 8° | Uphill. Lower gear if needed. | Downhill. Use engine braking. |
| 8° to below 12° | Steep uphill. Avoid engine strain. | Steep downhill. Suitable low gear. |
| 12° through 30° | Very steep uphill. Controlled power. | Very steep downhill. Brake as needed. |

The gearing phrases are for **Manual vehicle · gear guidance** ON. With it OFF, the 5–8° cues are **Uphill. Steady power.** and **Downhill. Control speed.**, and the 8–12° downhill cue is **Steep downhill. Control speed.** No angle selects an exact gear number.

The coach no longer selects **Maintain speed**. Empty detections cannot establish a safe speed. Measured steady motion above 0.5 and at most 10 km/h, with longitudinal acceleration within ±0.2 m/s², keeps additional slope action speech quiet. Known stationary speed also suppresses slope action cues. Changed or unknown motion leaves normal slope guidance available; camera hazard guidance evaluates independently. These thresholds suppress unnecessary speech, rather than defining a safe driving speed.

**Slope reading unavailable.** speaks once after 1.5 seconds of unsupported or lost slope data following a valid reading, or an explicitly invalid calibrated reading. It stays quiet while initial normalization is pending. A recovered valid reading allows a later loss to be reported again. Braking, shock, audio readiness and the slope toggle apply to this cue too.

Bend and slope cues speak once per event. A confirmed straight/level interval of two seconds re-arms that event; missing sensor data does not. Slope bands use a 0.5-degree release margin to prevent boundary chatter, and a pending cue cannot speak below its entry angle. A steeper band or new direction can speak; returning to an already announced lesser band stays quiet until level is confirmed. Terrain guidance is spaced by at least six seconds. Routine obstacle and traffic cues also speak once until the scene clears for two seconds; missing frames do not re-arm them. Increasing hazard severity can trigger another warning. Bend, slope and gear cues can interrupt an image-only obstacle caution, so that caution does not monopolize playback. Reliable approaching-obstacle, braking and imminent-collision prompts retain higher priority. Maintain speed and unavailable-reading messages cannot interrupt obstacle cautions. Braking and shock suppress action guidance.

Bend detection fits a quadratic across five road-mask bands, removing the straight-road perspective trend. When near road edges are cropped, it looks farther up the image for a bounded corridor. Small central segmentation holes are tolerated; paired lane markings offer a fallback when road boundaries are unavailable. Split corridors, large gaps, weak probabilities and irregular fits are rejected. At least three separate frames over 0.4 seconds are required, and the latest frame must be younger than 350 ms. This is a conservative image heuristic: junctions, occlusion, an off-center mount and unmarked roads can prevent or confuse detection. It does not estimate a surveyed bend radius, safe cornering speed or an ego lane. An unclear **Road** status explains why no bend cue is emitted.

The phone cannot see upcoming slope grade or know a vehicle's current gear/RPM from object boxes. Physical IMU slope cues describe the **current** inclination relative to level calibration. Android uses fresh gravity-separated acceleration when available and gyroscope orientation; raw acceleration alone is prevented from changing the gravity estimate during significant motion. [Android's motion-sensor documentation](https://developer.android.com/develop/sensors-and-location/sensors/sensors_motion) describes the gravity/linear-acceleration relationship and device coordinate axes. This filtering reduces false slopes; it does not establish road-grade accuracy on a phone.

MP4 and Desk telemetry are simulated unless physical IMU is enabled. Camera video-test speed is an explicit setting; slope is measured by default or explicitly simulated when selected. Neither selection changes the measured sensor-fusion state.

The APK has **no OBD/CAN connection**. Camera-mode gear guidance is therefore conditional. Direct shift prompts can be exercised in Android Desk: select Manual vehicle, enable **Simulate engine under load**, set current gear to 3, pitch to zero and TTI to 20 seconds. RPM 3500 gives an upshift cue; RPM 900 gives a downshift cue; RPM 2000 gives neither. These demo bands (1200–3000 RPM) are not a vehicle recommendation. Exact real-vehicle shift timing requires an adapter feeding `ld_engine_sample` with fresh gear/RPM, a load indicator and bands appropriate to that vehicle. Automatic mode, neutral, first/top gear limits, stale samples and normal RPM prevent inappropriate shift commands. Upshift prompts are suppressed on a measured descent or visible bend. A shift cue is not repeated until the gear changes or RPM has stayed within its band for two seconds.

The wording follows the principles of [DVSA vehicle-control guidance](https://www.gov.uk/guidance/learning-to-drive-a-car-syllabus/unit-2-guide-and-control-a-vehicle) (gear choice depends on speed, conditions and the vehicle) and [Highway Code rule 160](https://www.gov.uk/guidance/the-highway-code/using-the-road-159-to-203) (lower gearing helps control speed on long descents). These are wording references, not evidence that the app has passed road testing.

Both platforms use the shared terrain policy. Android bundles 35 offline PCM voice clips and provides the Camera video-test controls, including simulated ±3°, ±6°, ±10° and ±15° slopes. iOS source calls the shared policy and warms matching phrases through its platform voice; it still needs a macOS build and device validation. Automated synthetic-mask/policy tests do not establish physical-road accuracy.
