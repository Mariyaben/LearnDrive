# Android coaching integration

Version 0.5.0 (code 29) uses the configured native coordinator. Android stores
`MINIMAL`, `BALANCED` or `GUIDED`, a separate voice setting and a monotonic
settings revision in local `driving_advice` preferences. Defaults are Balanced
and voice on. Unknown profile names fall back to Balanced without enabling a
previously muted voice. The controls are inside the existing **More setup
options** panel. Charcoal styling, the fixed Start action, `sensorLandscape`,
destination flow and automatic calibration after level-ground confirmation are
preserved.

Session admission still requires the current source, a fresh completed camera
result, supported mount, healthy camera/vision, acceptable thermal status and
the existing parked/session/reference checks. Voice or PCM readiness is not an
admission requirement. Camera, motion, navigation, map display and visual
coaching continue with voice muted or unavailable. Google SDK guidance remains
`SILENT`; cached app PCM is the only spoken guidance path.

## Acceptance and playback ordering

`NativeCore.deliverCoaching` holds the native monitor through exact pending
sequence/prompt/source/revision validation, optional PCM submission and logical
acceptance. A queued worker cannot acquire that monitor until acceptance
commits. The worker invokes `AudioPlaybackGate` outside its own monitor. The
gate acquires native first, then the worker acquires audio and revalidates the
accepted ticket immediately before `AudioTrack.play`. Waiting for the audio
monitor cannot extend freshness. The predicate calls JNI while the native
monitor is already held; it never acquires native while holding audio.

Settings changes use the same native-then-audio order, invalidate pending and
accepted tickets immediately and cancel queued/current PCM. Source and
navigation identity checks remain in force. Muting is also enforced at the
worker submission boundary, including Test voice. Audio focus refusal or an
onset rejection does not undo logical visual acceptance. Neither a successful
submission nor a `play` request establishes physical audibility or hearing.

Nonblocking PCM chunk writes share the audio monitor with cancellation and
recheck generation inside it. This prevents an old worker from writing a final
chunk after a mute/source-change flush. Every source change, including automatic
camera recovery, atomically invalidates native tickets and cancels PCM. Closing
the core also retires its source identity.

Only an exact eligible native `WARN` decision can carry routine-preemption
authority through the internal coaching submission method. This lets a safe
person/rider phrase with legacy priority 1 replace routine terrain/map PCM.
Ordinary submissions cannot replace an active/pending validated warning;
competing warnings retain legacy priority ordering. Public Test voice has no
warning flag. Mute, focus, generation and onset checks still apply.

## Observed capability fallback

The semantic detector remains usable if spatial initialization or processing
fails. Three consecutive finite completed spatial stages of at least 250 ms
also select Basic capability for that Vision instance, release the spatial
model and keep semantic detections. Explicit Restart detection creates a new
instance and retries; source switches do not flap the capability. Standard
means the existing spatial path remains available, not that a device class or
road accuracy is qualified. Advanced is unavailable/experimental here.

Pose assessment happens before capability mapping. A known unsupported mount,
missing intrinsics or unavailable gravity retains its own readiness state.
Basic does not bypass admission guards or the native 350 ms evidence lifetime.
Immutable performance snapshots and decision diagnostics expose Basic/Standard,
the reason, finite stage timings and sample/failure counters. No accelerator,
model, thread count or freshness threshold was retuned.

## Diagnostics and review

`coaching_accepted` records logical acceptance and a `voice_submitted` boolean.
The existing `accepted` event is emitted only after actual worker submission.
Both include the settings revision and explicitly leave audible playback
unconfirmed. The temporary review counts logical cues and voice submissions
separately. Native diagnostic counters distinguish logical acceptance,
submission and playback requests. Capability and coordinator fields are scalar
statistics; no coordinates, provider road names or new navigation content are
added. Existing speed-limit redaction and opt-in local export controls remain.

Google-backed decisions omit the entire provider-derived navigation object,
including maneuver, progress, distance and geometry estimates. The exact
provider identity and route context are captured together under the route
mutation lock so a provider switch cannot relabel Google data as offline data.
Transient navigation/caption behavior and offline context export are unchanged.

## Verification

The implementation pass ran `VisionCapabilityPolicyTest`,
`AudioInterruptionPolicyTest` and all 13 `PracticeSessionReviewTest` cases on the
host successfully. These cover invalid timing samples, consecutive slow stages,
failure fallback, explicit retry, preservation of mount guards, WARN/routine
priority and truthful muted review counts.

From the repository root, all nine Java host test classes are covered by:

```powershell
powershell -NoProfile -File tools/run_camera_health_tests.ps1
powershell -NoProfile -File tools/run_road_coaching_tests.ps1
```

The first runs camera health, spatial readiness, setup, level start, journal,
capability and audio interruption tests. The second runs road status and
session review tests.

Android instrumentation adds `CoachingPreferencesTest`, `AudioWorkerGateTest`
and `NativeCoachingContractTest`; existing Start and trace tests also cover
muted/unready PCM admission and separate logical/submission records. The worker
test checks actual queue cancellation and monitor ordering without speaking a
clip. The JNI test uses explicit Desk simulation, not road sensor evidence.
These require the coordinated Android build and device execution; no device
execution or acoustic onset qualification is claimed by the host results.
