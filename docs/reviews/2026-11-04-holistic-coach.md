# 0.7.4: one situation planner, junctions without a destination, lanes, instructor benchmark, detector zoom evaluated

Date: 2026-11-04. Branch: `driving-coach-foundation`. Status: host-tested only. It has not been tested on a phone or validated by an instructor.

## What changed

### One situation planner

Every turn of speech now comes from a single place: the native `SituationPlanner`. It **composes** what to say from everything currently relevant, instead of picking one fixed sentence:

- the measured slope (calibrated pitch);
- the junction lesson step;
- the most dangerous road user, as reasoned by `RiskCoach`, and the second one;
- the control advice from `ControlCoach`.

The result is spoken from recorded fragments. In the native test of an uphill road with an oncoming truck and a pedestrian at the left edge, the coach says "Uphill. Oncoming truck. Person on the left. Keep left. Ease off the accelerator. Give them space."

- Conflicting actions are removed. An urgent situation keeps at most one context phrase and two actions, and "brake gently" becomes "brake now".
- An identical instruction is not repeated within 4 s.
- The planner never invents gear, pedal, gaze or indicator state, and never gives permission to go.

### Junction ahead without a destination

`RoadNetwork` (Android) fetches OpenStreetMap roads for a coarse ~1.8 km square from the public Overpass API, with a generic client name. The exact position is never sent.

The native `JunctionScout` follows the road the car is on, using GPS bearing within ±40° and continuations under 45°. It walks up to 400 m ahead to the next junction an instructor would prepare for: a road end, a more important road joining, or a crossroads. That junction feeds the same junction lesson as a navigation route, so the brake/gear/entry steps run without a destination.

Attribution "© OpenStreetMap contributors (ODbL)" is shown in the app. The Overpass public instance has a fair-use policy, so heavy commercial use needs its own Overpass server or an extract.

### Lane keeping

The bundled TwinLiteNet lane output runs at up to 4 Hz while heat allows. `ld_lane_map` finds the nearest lane line on each side just ahead of the bonnet. `ControlCoach` advises only when all of these hold:

- the lines are seen consistently;
- the drift exceeds 28% of the lane width for 1.5 s;
- the speed is above 15 km/h;
- no junction is within 10 s;
- no lane advice was given in the last 20 s.

The advice is "You are drifting right. Keep left" or "…drifting left. Move back to the centre of your lane". There is no lane advice on unmarked roads, which includes much of the 2026-11-04 drive.

### Detector zoom pass: built, evaluated, and kept OFF in coaching

The zoom runs the detector a second time on a 2x crop of the road ahead and merges the candidates natively (`ld_preprocess_zoom`, `ld_zoom_merge`) before the usual decoding and NMS. It is gated to no thermal throttling and a detector run of 45 ms or less. The evaluation below shows it finds real distant road users. It also makes the coaching worse in its current form, so `ZOOM_IN_COACHING` is false. See [models/README.md](../../models/README.md).

### Instructor benchmark

An opt-in recording of the instructor's voice is aligned to the diagnostics journal. A host tool classifies both sides into instruction intents and scores agreement within ±3 s. See [INSTRUCTOR_BENCHMARK.md](../coach/INSTRUCTOR_BENCHMARK.md).

Journal `coaching_accepted` records now carry the spoken text.

## Evidence

All of this is host evidence. None of it validates teaching quality.

| Check | Result |
|---|---|
| Native CTest | 21/21 groups pass (incl. 32 drive-coach groups, 5 junction-scout groups, zoom mapping) |
| Python unit tests | all pass (incl. instructor benchmark and zoom-cache tests) |
| Camera-health/journal Java tests | pass (incl. zoom heat gate) |
| Android Kotlin compile | passes |

### Real drive (2026-11-04, replayed)

The guided, automatic replay with task scheduling on, using the user's real speeds, gives:

- without zoom: 25 instructions, 3 interrupted;
- with zoom: 43 instructions, 5 interrupted. The extra instructions are reviewed below.

Earlier, 0.7.1 gave 7 cues, all truncated.

### Simulator (MetaDrive long drive, seed 5)

This is a 180 s drive with a synthetic route: a right turn at an intersection and a left turn at a T-junction. The driver is an expert IDM, not a learner, and the rendering is synthetic. The coach gave 28 instructions (9.3 per minute), 3 of them interrupted. Both junctions got the full sequence:

| Step | Right turn (intersection) | Left turn (T-junction) |
|---|---|---|
| Plan: "Right/Left turn ahead. Check your mirrors and signal…" | 33.4 s before | 34.5 s before |
| Approach: "Ease off the accelerator and brake gently. Change down to second as you slow" | 18.4 s before | 15.5 s before |
| Entry: "Clutch down if you need to stop. Look both ways before you turn" | 3.1 s before | 1.8 s before |
| "Straighten the wheel and add gentle accelerator" | 7.8 s after entering | 13.0 s after entering |

Road-user instructions spoken inside a turn were urgent risk cues: "Truck ahead. Brake now", "Vehicle crossing. Stop and wait". Only close-range urgent risk may cut into a turn.

### Detector zoom, unlabelled (5 fps over 396 s)

| Group | Full-frame detections | Zoom-only, confirmed (≥3 of 5 frames) | …of which ahead (centre 30%) |
|---|---:|---:|---:|
| Person | 108 | 34 | 20 |
| Two-wheeler | 1 | 5 | 5 |
| Vehicle | 509 | 173 | 148 |

- Of 102 objects first seen by the full frame, 24 had already been seen with the zoom: a median of 0.7 s earlier, and 1 s or more earlier for 10 of them.
- Visual review of 18 sampled zoom-only detections:
  - 8 of 8 vehicles were real;
  - 6 of 10 people or riders were real, including the rider at 370 s the full frame missed;
  - the 4 false ones were people printed on roadside billboards.
- Distant small riders (52 s, and 370 s in some frames) and two pedestrians at 272 s are still missed even with the zoom. Host CPU time was about 27 ms per pass; phone time is unmeasured.

### Zoom in the coaching replay (why it is off)

25 instructions appear only with zoom (same text not within ±3 s of the baseline). Of these:

- 4 are oncoming "keep left" cues at 276.8, 294.5, 334.5 and 358.8 s, close to similar baseline cues and not reviewed individually;
- 1 is a gear change re-timed at 253.3 s;
- the other 20 were checked against their video frames:

| Verdict | Count | Times (s) and reason |
|---|---:|---|
| Plausible | 4 | 208.8 oncoming scooter rider, close<br>266.3 truck ahead in the lane<br>290.7 vehicle ahead, keep a gap<br>312.7 truck close ahead on the left |
| Wrong | 4 | 58.9, 286.7, 346.1 and 359.9: people printed on billboards, spoken as "Person crossing/ahead/on the left" |
| Over-urgent | 6 | 69.3, 309.5, 344.9, 361.9 ("Vehicle/Truck ahead. Brake now")<br>366.0, 370.3 ("Person ahead. Brake now")<br>All for road users still far away or parked |
| Unclear | 6 | 208.9, 250.0, 261.5, 269.2, 298.8, 321.5 |

Small, distant boxes give noisy expansion, which reads as short time-to-contact. Billboards are static, but they grow like an approaching object. Before the zoom can feed speech it needs:

- rejection of elevated objects, whose feet are not on the road surface;
- urgency that requires a minimum box size or distance-consistent motion;

then a replay that shows no new false instructions.

The tooling stays: `tools/evaluate_detector_zoom.py` and `tools/review_video.py --detector-zoom`. The zoom cache keeps only decodable candidates, giving the same boxes at about a twelfth of the size.

## Not done, and why

**Mirror checks (front camera).** These need the front camera to run alongside the road camera. Concurrent cameras are device-specific (CameraX concurrent camera, Android 11+) and unverified on the user's phone. They also need a face or head-pose model, which is a new download and a privacy decision about the driver's face, and they add heat to a phone that already throttled on 2026-11-04. Deferred until the phone is checked and the model is approved.

**Indicator (microphone).** An indicator detector for the relay tick, roughly 1–2 Hz, has to be tuned and validated on real audio from the user's car, with road noise, the app's voice and the instructor's voice present. An untested detector would effectively invent indicator state. The instructor recording now captures exactly that audio. A detector built on it and validated against it is the next step.

**Instructor validation.** The benchmark exists; it needs real lessons.

**Phone validation.** Still needed for heat with zoom and lanes, audio, and the composed fragments' timing.

## Actions for the owner

- **Google Places error 9011.** The Google Cloud key must allow the Places API (New) for the app's package name and SHA-1. Keys stay in ignored local configuration; nothing here touched them.
- **Detector licence.** YOLOv8n is AGPL-3.0. Decide on AGPL compliance or a commercial licence, or approve evaluating a permissively licensed detector (download needed).
- **OpenStreetMap.** Keep the attribution visible. For scale, plan your own Overpass instance or a road extract.
