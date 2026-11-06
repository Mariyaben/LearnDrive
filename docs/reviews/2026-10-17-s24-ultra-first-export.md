# First physical diagnostic export — 17 October 2026

Result: a structurally complete camera diagnostic capture was exported from the
Samsung S24 Ultra on Android 16. Active coaching is not demonstrated: every
sampled decision remained in setup and was blocked as `source inactive`.

## Evidence identity and method

- User-supplied file: `D:\Downloads\LearnDrive-session.jsonl`, 160,430 bytes.
- SHA-256: `8feb083f346d01fba7f28009288544cb7caa6bdd862820ea5be42e447e610b62`.
- Session: `77bcc9d9-ffb7-4948-90ec-6d39865d80aa`.
- Header start: `2026-10-17T16:22:27.448457Z` (21:52:27 IST).
- App metadata: 0.3.5 / code 18. This does not verify the installed APK hash.
- Device metadata: Samsung `SM-S928B`, Android 16 / API 36,
  OS build `BP4A.251205.006.S928BXXS6DZG1`.

Run `python tools/inspect_session.py 'D:\Downloads\LearnDrive-session.jsonl'`.
The inspector passes without warnings. Additional calculations below use the
JSONL's monotonic timestamps and finite numeric fields; p95 uses nearest rank.
The export contains model estimates and sampled decisions, not ground truth.

## Capture integrity and session coverage

| Check | Observation |
| --- | --- |
| Body records | 183: one source record, 42 decisions, 140 frame observations |
| Footer | `complete=true`, `user_stop`, 183 written, zero dropped, no storage limit |
| Observed interval | Last frame completed 41.449 seconds after header start; footer has no end timestamp |
| Source | CAMERA, source ID 5 throughout |
| Setup state | All 42 decisions: 0 / SETUP; none: 1 / MONITORING |
| Guidance | All 42 blocked as `source inactive`; zero eligible decisions, zero accepted submissions |
| Level reference | ID 0 throughout; no confirmed level reference demonstrated |
| Calibration | Automatic native calibration ID 1; this is separate from a confirmed level reference |
| Alignment readiness | Code 4 / READY in all frame and decision samples; this does not prove correct forward mounting or model accuracy |
| Speed | All decisions use manual fallback 30 km/h; measured-phone speed is null/invalid throughout |

The file verifies the normal capture/stop/export path produced a readable,
consistent artifact. It does not establish every UI step, deletion, chooser
cancellation, interruption recovery or failure handling. The source record at
capture start denotes the existing camera generation; it does not prove a new
camera start at that instant.

No sampled evidence of an active session appears. Setup state participates in
change-triggered tracing, so a sustained Start/End session should produce a
MONITORING decision. The user subsequently confirmed that they stayed in setup /
Lab settings. The inactive trace matches those actions; there is no evidence of
a failed Start.

## Sampled camera measurements

| Quantity | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Reported processing time, 140 observations | 141.870 ms | 149.398 ms | 159.826 ms |
| Capture to completion, 140 observations | 224.316 ms | 247.794 ms | 254.457 ms |
| Semantic capture to completion, 140 observations | 224.346 ms | 249.521 ms | 286.395 ms |
| Scene age at sampled decisions, 42 observations | 368.908 ms | 489.832 ms | 525.371 ms |

There are approximately 3.35 logged semantic updates per second over their
timestamp span. This is **not camera FPS or spatial inference FPS**: frame
logging follows semantic updates and omits intervening results. Frame sequence
increases strictly from 547 to 959 across the 140 samples; no negative
capture-to-completion intervals occur. One in-flight observation uses a semantic
capture timestamp about 245 ms before diagnostics began and completes after the
header; that is consistent with beginning diagnostics on an already running camera.

24 of 42 sampled decisions have scene age at least 350 ms. Since this run is
inactive, their recorded blocking reason is inactivity; do not relabel them as
observed stale-guidance blocks. The age distribution warrants an active-session
trace to measure availability and suppression before changing freshness gates.
It does not establish acceptable response time, thermal stability, diagnostic
overhead or physical audible latency. The duration is about 41 seconds, with no
controlled comparison or charging/temperature/audio-route measurements.

Frames report 640x480, zero-degree rotation, full crop, camera metadata
intrinsics and assumed video pixel range. There are detections in 55/140 samples:
34 car, 20 person, five truck and five bus boxes across samples. These are
repeated model outputs, **not counts of unique or correctly detected objects**.
All sampled road outputs are invalid and TTI is null. Without imagery, scene
labels or an active session, this does not establish a perception fault or road
accuracy result. Decision semantic/spatial availability flags are false while
inactive; they must not be interpreted as proof that the camera produced nothing.

## Next acceptance check

While parked, delete the saved local diagnostic file only after preserving this
export, start a new diagnostic capture, then use **Start supervised session** and
complete **Mount checked · start**. Confirm the monitoring view with **End session**
visible, leave it active about 20 seconds, then End, Stop diagnostics and export a
new filename. A confirmed level reference is optional for this check.

Inspect the next file for the setup-to-MONITORING-to-setup transitions, source
generations, active decision reasons, scene age and any accepted submissions.
Stationary silence is not itself a failure; an active session need not emit a
cue. If Start is disabled or confirmation returns to setup, record that behavior
and the displayed status before drawing conclusions about the cause.

Road accuracy, learner benefit, numbered-gear coaching, sustained performance and
market demand remain separate open gates in the master plan. No app or driving
policy change is justified by this file alone.
