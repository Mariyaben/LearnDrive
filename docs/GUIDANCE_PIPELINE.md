# Common guidance pipeline — first increment

Android Camera, Android MP4 and the desktop review bridge now call
`ld_guidance_decide` and acknowledge successful audio submission with
`ld_guidance_accepted`. The coordinator selects among tactical, reflex/context,
terrain and camera candidates, applies suppression and the global audio gate,
and commits policy cooldowns only for the accepted decision sequence.

This is shared selection logic, not proof of identical perception on different
devices or codecs. Android and desktop still have different frame capture and
decoding adapters. iOS still uses the earlier orchestration.

## Source and measurement contract

Each uninterrupted source has a monotonically increasing ID. Switching sources,
restarting capture or pausing invalidates previous frames and pending decisions.
Geometry, scenes, tracking, semantic results and recurrent image history reset;
global audio cooldowns survive. Android guards preprocessing, geometry and result
publication under the same engine lock as source changes. Inference runs outside
that lock. Old inference may finish, but cannot publish into the new source.

| Source | Optical metadata | Motion and speed |
| --- | --- | --- |
| Rear camera, road mode | Camera metadata or explicitly labelled lens estimate | Current phone sensors; GPS/manual speed selection remains labelled |
| Camera pointed at a screen | Original calibration unknown; semantic detections only | Explicit screen-test settings; phone tilt describes the phone |
| Android MP4 | Original calibration unknown; semantic detections only | Manual recorded-speed test setting; original motion unavailable |
| Desktop replay | Explicit assumed field of view for sensitivity experiments | Reviewed speed-overlay CSV; no invented IMU |
| Desk | Explicit simulation; no camera scene | Explicit simulation inputs |

Android MP4 retains original presentation time separately from the elapsed-time
clock used to pace playback and check freshness. Native recording guidance
rejects reviewing-phone pitch, acceleration, braking and shock as driving data.
It also disables slope coaching for recordings without original motion data.

Supercombo remains bundled and used for camera frames with optical metadata in
road mode, and desktop replay with explicit assumed geometry. Unknown-original-
calibration video/screen inputs deliberately skip metric inference. Lens
metadata is not a completed mount/calibration validation.

Original-camera/IMU/speed sidecar import, an Android video preview, persistent
session export, full shadow playback and calibrated cross-device golden replays
are still pending. Do not equate semantic detections with reliable range or TTI.

## Diagnostic trace, version 1

Android emits JSON records under the `LearnDriveGuidance` logcat tag on decision
changes and at least once a second; desktop replay writes every 100 Hz decision
to `decisions.jsonl`. Both emit separate `accepted` records. Acceptance means the
audio worker accepted submission, not proof that a driver heard a complete clip.

Decision fields: `schema_version`, `event`, `time`, `source`, `source_id`,
`sequence`, `prompt`, `origin`, `candidates`, `suppress`, `eligible`, `blocked`,
`capture_time`, `scene_age`, `semantic_available`, `spatial_available`, `reason`,
`speed_source`, `calibration_origin`, `recorded_time`, `research_emergencies`.
Unavailable timestamps use JSON null. `capture_time` is the last semantic
capture; availability flags separately report semantic/model freshness.

Android also emits optional `spatial_readiness` (0 original calibration unknown,
1 missing optics, 2 missing fresh gravity, 3 unsupported mount, 4 pose available;
null before a frame). See [mount readiness](MOUNT_READINESS.md). Pose/optics loss
invalidates prior spatial and derived metric evidence without resetting speech
cooldowns; semantic-only detections remain available.

Android road-camera sessions now start from explicit parked setup. Automatic
normalization does not enable inclination lessons without a matching confirmed
level reference. Additive Android fields `setup_state`, `calibration_id` and
`level_reference_id` record this distinction; see [SETUP_FLOW.md](SETUP_FLOW.md).
Desk, MP4 and screen-test lab paths retain their separate source rules.

Origins: 0 none, 1 reflex/context, 2 tactical, 3 terrain, 4 camera. Candidate
order: tactical, reflex/context, terrain, camera. Block reasons: 0 no outer gate,
1 inactive, 2 audio unavailable, 3 cooldown, 4 research command disabled,
5 invalid source/time. A zero prompt can have block reason 0 and remain ineligible.
Per-policy reasons also explain absence of a candidate.

Emergency phrases (IDs 4, 19, 42, 43) default off in this lab build and replay.
Android's research switch resets off when the activity is recreated; desktop
requires `--research-emergencies`. Direct voice/preemption test buttons remain
explicit hardware tests. No emergency-recognition acceptance is claimed.

## Physical-phone check for this increment

Use the lab build while stationary. Device-dependent results are not automated
acceptance until captured and reviewed.

1. Install the APK and press **Test voice**. Check media volume and audio focus.
2. In Camera, turn screen-test mode off to test the road-camera path. Verify
   unavailable calibration is shown when metadata is absent. Check that opening
   a different source clears old boxes and guidance.
3. Select MP4. Set **Recorded speed (manual test)** to the video's observed speed.
   Verify the app reports original motion/calibration unavailable. Tilting/tapping
   the reviewing phone must not generate recorded slope/braking/shock guidance.
4. Switch MP4 → Camera → Desk while inference is running. No previous-source
   detection or queued phrase should appear. A source switch must preserve the
   global cooldown. Pause/resume: MP4 stops and needs selection again.
5. Keep emergency commands off and use Desk with a short simulated TTI. The trace
   should show the candidate blocked as research-only. For an explicit lab audio
   check, enable the research switch and repeat; this does not test recognition.
6. Capture trace records while repeating these steps:

```powershell
adb logcat -v raw -s LearnDriveGuidance:I '*:S' |
  Out-File -Encoding utf8 build/phone-guidance.jsonl
```

Stop capture with Ctrl+C. Review `source_id`, decision sequences, candidates,
block reasons and accepted events. Existing logcat records may precede this test;
use their timestamps/source IDs to select the session.

## Automated evidence

`guidance_pipeline_tests` compares independent Camera/recording engines through
startup, central-car and pedestrian scenes with identical frame inputs and audio
acknowledgements. It also checks stale source IDs/results, missing recording
geometry, duplicate/expired/backward/superseded acknowledgements, global cooldown
across source changes, inactive sources, research emergency gating, and isolation
from reviewing-phone motion. Other native tests still exercise perception,
fusion and policy boundaries. These are deterministic engineering checks, not
road-accuracy estimates. See `EXECUTION.md` for actual run results.
