# 2026-11-04: First real drive review and the risk-reasoning coach (0.7.3, code 34)

Input: the user's screen recording `drivetest1_nov4.mp4` (6.6 min, Kerala village roads
and a town main road; phone app running, no destination). Evaluation used the
recording's own road-camera view (cropped from the screen recording, so it includes the
app's overlay boxes, at a lower resolution than the phone camera). Speed came from the
app's own speedometer, read from the recording (`--speeds-as-live`). All judgements of
"needed assistance" below are the engineer's review of the video. They are not
instructor labels.

## Why the app almost said nothing

| Cause | Evidence | Fix |
|---|---|---|
| Heat throttled ALL camera analysis to one frame per 1.5 s, but coaching needs evidence < 0.35 s old | Status "Camera delayed · no current road assessment" for most of the drive; "phone too hot" at 12:27. Screen recording + charging at ~11 % battery added load | `CameraVisionHealth`: severe heat → detector at 5 Hz, Supercombo spatial model shed first (`spatialAllowed`), critical → pause |
| No destination: Places search failed (code 9011) | Screen shows the error | Not code: API key restriction/billing. Route-free junction awareness is the next milestone |
| False "Very steep uphill" repeated | Phone knocked/re-seated at ~70 s; level reference kept | `SensorFusion`: a sustained >6° pitch step with fast (>30°/s) rotation invalidates calibration |
| Dashboard detected as "Car 54%", mount/ornament as "Person" | App overlays in the recording | Bonnet filter; risk coach ignores single-frame flickers (temporal confirmation) |
| Road-user cues were announcements and cut off | 0.7.1 replay: 7 cues, all truncated at 4–24 % | Risk coach (below) and continuation rules |

## What changed: `RiskCoach` (core/src/coaching/RiskCoach.cpp)

The camera path is no longer "object seen → phrase". Each tick the risk coach:
1. **Keeps its own memory of each road user.** Detections are associated frame to frame
   by class, size and predicted position, so identity and motion survive detector
   re-IDs. Motion history coverage on this drive went from 34 % to 60 %.
2. **Assesses each road user from that motion:**
   - ego-motion-compensated lateral motion (parked objects drift *outward*; crossing ones move *inward*);
   - looming time-to-contact;
   - a path corridor, oncoming behaviour on the right (keep-left traffic), roadside vulnerable users, receding vs approaching;
   - vulnerability (pedestrian > two-wheeler > vehicle).
3. **Ranks all road users** and decides one instruction for the most dangerous:
   - "Vehicle crossing ahead. Brake and wait"
   - "Person crossing ahead. Stop and let them cross"
   - "Oncoming vehicle. Keep left and ease off"
   - "Person on the left/right. Ease off and give them space"
   - "Brake gently and be ready to stop" (urgent when imminent)
   - "Clutch down and hold the brake" (crawling up to a vulnerable road user)
   - "Ease off and cover the brake"
   - With unknown own speed: awareness only, never a pedal action.
4. **Uses temporal memory to decide when to speak:**
   - speaks when risk *rises*;
   - escalates once if no slowing is measured 2.5 s later, and stays quiet while the learner is slowing;
   - at most one instruction per 3 s unless risk rises;
   - only an urgent risk may cut in within 1.5 s;
   - "keep a gap" at most every 15 s;
   - a road user must be seen consistently (≥3 detections over ≥0.3 s; urgent: ≥2) before it can drive an instruction.
5. **Junction waiting:** when stopped and a crossing road user clears, it says "It has
   passed. Look both ways again before you move off". It never says "go" or "clear". The
   forward phone camera cannot see cross traffic beyond its ~60–70° view, so silence is
   not evidence that the junction is clear.
6. **Ignores the learner's own car:**
   - boxes spanning the bottom (bonnet);
   - objects fixed in the frame while moving;
   - a learned per-mount mask;
   - edge-clipped boxes beside the car.

It replaces the camera coach's presence announcements in task-scheduling mode. The
0.7.1 comparison mode is unchanged. Urgent instructions finish their sentence while
perception is live and the road user was seen within 3.5 s; perception loss, shock,
source or settings change still stop them.

## Evidence on this drive (same inputs, both policies)

| | 0.7.1 policy (fresh camera assumed) | 0.7.3 |
|---|---|---|
| Instructions | 7, all "Road user ahead. Give them space" or "Person ahead. Give space" | 26 (control, gear, risk) |
| Cut off mid-sentence | 7 of 7 (4–24 % spoken) | 3 ("When it is clear: first gear…" stopped as the car moved off) |
| Engineer-annotated road-user situations (14) | 3 touched, none useful | 11 addressed; 3 missed (detector never saw the road users: 208 s scooter, 272 s pedestrians, 370 s scooters) |
| Junction approaches without a route (4) | 0 | 0 (no map or route knowledge) |
| Questionable instructions | n/a | about 4 of 26 (212 s likely the mount; 330/332 s parked cars; 262 s ambiguous) |

Examples:
- **40 s:** "Brake gently and be ready to stop" (goods carrier crossing).
- **234–245 s:** truck crossing → "Vehicle crossing ahead. Brake and wait" → waiting → "When it is clear: first gear, check mirrors and signal…".
- **287.6 s:** "Vehicle crossing ahead" (town crossroad).
- **313–349 s:** "Oncoming vehicle. Keep left and ease off" in narrow two-way traffic.
- **380 s:** "Vehicle crossing ahead. Brake and wait" (truck from the left at the T-junction).

Tests: CTest 20/20, including new groups for:
- motion-history risk reasoning;
- escalation with and without measured slowing;
- the urgent-continuation bound;
- the C-API capture-race regression;
- remount detection.

The Python suite passes, the camera-health policy tests pass, and all 86 voice assets are verified.

## Not established

- No phone run of 0.7.3: heat, latency and audible behaviour are unmeasured.
- The thresholds (corridor, 0.10 inward rate, TTC bands, confirmation) are engineering
  choices tuned on one drive. They need a held-out set of drives and instructor review.
- Image geometry only: no metric distance; corridor assumes a roughly centred camera.
- Instruction wording has not been instructor-reviewed.

## Decision on models

- **No trained policy model and no LLM in the live loop.** The decision layer must be
  fast, explainable, testable and bounded. We have no instructor-labelled data to learn
  a policy from, and a language model cannot add missing observations.
- **Perception is where a model IS needed.** The 320 px detector misses small or distant
  pedestrians and scooters, and has no tractor or autorickshaw classes. Next model step:
  evaluate a larger-input or Indian-traffic-tuned detector (licence-clean data only).
- A small local LLM may later help **after** the drive, writing the session review from
  structured events.

## Next steps (in order)

1. Phone run of 0.7.3 on the same route **without** screen recording or charging; capture
   the session journal (decision ages, thermal state, spoken cues).
2. Route-free junction awareness from the offline Kerala map: junction ahead on the
   current road from GPS and heading, giving "Junction ahead: check mirrors, ease off,
   change down…" without a destination.
3. Detector upgrade evaluation (recall on small and distant road users; Indian classes).
4. Held-out drive set plus instructor labelling of full intervals, to tune thresholds honestly.
