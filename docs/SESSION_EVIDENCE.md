# Local diagnostic evidence — increment 2a

Android 0.3.5 adds opt-in, bounded diagnostic capture and export. It is a first
recording/provenance slice; importing original recording sidecars and full
cross-device replay conformance remain pending. No driving policy/model weights
change in this increment.

Android **0.3.6** adds selected camera-attention actors to decision records,
semantic completion/processing fields to frame observations, and metadata declaring
up to 8 Hz semantic updates published before spatial inference. Exact eligible
decisions are retained before every accepted acknowledgement, including when audio
became free between ordinary sampled ticks. These are additive schema-1 fields;
see [the learner coaching review](reviews/2026-10-17-learner-coaching-fixes.md).

## Use while parked

1. Open Lab settings and test sources and choose Start local diagnostics.
2. Read the capture description and explicitly start. It records device/model
   versions, source/frame metadata, sampled detections and telemetry, decisions
   and accepted audio submissions. No images, video, microphone audio, GPS
   coordinates, phone identifiers or document URI/path are saved.
3. Exercise the normal parked setup or a lab source. The diagnostic capture
   spans source changes, preserving IDs and source labels.
4. End the road session to return to setup; open lab settings and Stop diagnostics.
   Wait for Finishing to become Stopped, then Export diagnostics (JSONL) and choose
   a destination through Android's document picker.
5. The local copy remains until Delete local diagnostics is explicitly confirmed.
   Exported copies are controlled by the destination you choose. Export or delete
   the saved local file before starting another capture; no automatic overwrite.

Capture defaults off and never silently resumes. Backgrounding stops and drains
the current interval. An interrupted startup cannot start capture after the app
has backgrounded. Existing recovered evidence can still be exported/deleted.

## Bounds and failure behavior

One private local file, at most 8 MiB. A separate writer thread drains a
128-record queue. Producers offer records without file IO or waiting for queue
space; oversized/multiline records are rejected. The file budget reserves room
for an end record. Queue/serialization loss is counted; storage-limit captures
are explicitly partial. IO failure remains visible and preserves any existing
partial evidence; it does not stop guidance or overwrite a prior session.

Exports require a finished writer. Export/delete errors remain visible in lab
status. A process kill may leave an incomplete last line or no end marker; those
are incomplete evidence, not completed captures. Buffered writes/OS storage do
not guarantee survival of a power loss. A missing end marker must never be
inferred to mean no hazard or no guidance.

## Format and provenance

UTF-8 JSON Lines with schema version 1 and a unique session ID in the boundary
records. `session_start.metadata` includes actual app version, phone/Android/OS
build, bundled model manifests, sampling policy, UTC start and the app's monotonic
clock. It explicitly states that recording-sidecar import is unsupported.

| Event | Meaning |
| --- | --- |
| `session_start` | Capture metadata and session identity |
| `source_start` | New camera/recording/screen/simulation generation; source 0 can mean capture has not started yet |
| `frame_observation` | Sample on semantic updates, up to 8 Hz in 0.3.6 (4 Hz in 0.3.5); not every camera frame |
| `decision` | Existing changes-or-at-least-once-per-second diagnostic sampling, with advice and measured-phone telemetry distinguished |
| `accepted` | Native/audio-worker submission acknowledgement, not audible playback or completion |
| `session_end` | Written/dropped counts, end reason, limit flag and whether this capture interval drained without reported loss |

Frame records preserve source/frame sequence, capture/completion time, semantic
capture time, original decoded presentation time, dimensions, crop, rotation,
pixel format/range and range provenance, optical metadata/origin, detections and
available hazard/road estimates. Semantic capture time remains distinct when boxes
are reused between detector updates. Model outputs and detections are estimates,
not labels or ground truth. Unknown TTI/motion values are JSON null, not zero or
non-standard Infinity/NaN.

Live camera intrinsics remain sensor/lens metadata and can be estimated, as
labelled. Decoder range metadata is distinguished from assumed video range.
MP4/screen original calibration remains unknown; no phone optics/IMU are promoted
to original recorded measurements. Measured phone telemetry in decision records
is explicitly separate from the advice telemetry used for that source. Source
IDs and model hashes support diagnosis. Version 0.3.6 adds selected camera-attention
tracks and boxes; unified actor links across all hazard producers, original-video
hashes, sidecar import and full raw-model input capture are still future work.
Producer threads can interleave records; use their source IDs and timestamps,
not file order alone, to relate in-flight frames to a source transition.

`session_end.complete=true` means only that this intentionally sampled capture
interval finished without reported writer loss. It does not establish a complete
drive, exhaustive decision trace, a heard clip, perception accuracy or learner
benefit. Frame/decision sampling and any partial status must accompany analysis.

## Inspect and verify

```powershell
python tools/inspect_session.py path/to/exported-session.jsonl
python -m unittest discover -s tests -p test_session_inspector.py -v
powershell -NoProfile -File tools/run_camera_health_tests.ps1
```

The inspector rejects malformed/non-standard JSON and inconsistent boundary/count
or complete-capture acknowledgement records, and identifies interrupted/lossy
captures as partial. It does not evaluate guidance correctness.

Host tests cover writer drain/stop, preservation of prior evidence, bounded queue
and byte budget, explicit loss, IO failures and invalid records, plus clean,
interrupted, lossy and inconsistent exports. Android compilation checks platform
integration. The [first Samsung S24 Ultra / Android 16 export](reviews/2026-10-17-s24-ultra-first-export.md)
passes structural inspection with zero reported loss. It contains only setup /
inactive decisions, so active-session capture still needs a physical check.
Delete, chooser cancellation, storage failure, backgrounding and diagnostic
performance overhead still need physical testing.
