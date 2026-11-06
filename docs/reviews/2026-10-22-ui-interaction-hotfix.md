# Setup interaction hotfix — 22 October 2026

The user asked to hold heat optimization and supplied the eight-second
`Screen_Recording_20261021_102812.mp4`. The recording shows the Start control
changing label/enabled state, moving setup content, and a mount confirmation
that returns to setup without entering the session.

## Verified causes

`Vision` intentionally publishes semantic classification before spatial work
finishes. That intermediate `VisionResult` has no `FrameContext`. MainActivity
used the same `lastVision` reference for rules, setup labels, Start admission
and dashboard rendering. Between intermediate and completed results, the setup
reader alternated between absent and ready camera evidence. A control could
be disabled between touch DOWN and UP, cancelling the click.

The 50 ms display tick also called `showSetup(true)`, which unconditionally
called `bringToFront()`, and reset unchanged presentation text. This requested
layout work while a person was scrolling, typing or pressing a control.

## Change

Android **0.4.5 / code 25** keeps a separate completed result for setup and
presentation while leaving the existing semantic-first rules path unchanged.
The completed reference is cleared on source transitions, camera restarts and
calibration reset. Start still requires the current source, capture age below
350 ms, supported mount, initialized voice, no camera/vision error, no critical
thermal state and no measured movement. No freshness guard was relaxed.

Setup visibility/z-order and unchanged displayed values now update only when
their presentation changes. Each setup refresh uses one movement observation
for its control states. A blocked mount-confirmation recheck keeps the dialog
open with retry guidance instead of silently dismissing it.

## Physical validation

Device: connected Samsung S24 Ultra, Android 16/API 36.

1. Installed only the new test APK over the previous application **0.4.3**.
   Both regressions reproduced: an intermediate publication cancelled a valid
   Start tap, and stable display updates requested another layout.
2. Installed **0.4.5** and ran the same cases plus existing caption/provider
   contracts: **7 passed, 0 skipped**, in 2.838 seconds.

The dashboard case checks draft focus/selection, scroll position, held-control
bounds/state, text callbacks and layout requests across repeated presentations.
The activity case dispatches actual Android button DOWN/UP with an intermediate
publication between them and verifies the confirmation opens. It also verifies
stale confirmation stays blocked/open, and source, unsupported mount, camera
error, thermal and movement guards still reject entry. Source change clears
the completed reference.

The activity regression uses explicitly synthetic UI result fixtures and a
temporary inference-executor barrier, released in `finally`. It does not inject
GPS/native model evidence or approve monitoring. These are interaction checks,
not camera acquisition, perception, road-use or sustained thermal qualification.

## Artifact and held work

APK: `build/LearnDrive-0.4.5-debug.apk`, **237,612,997 bytes**.
SHA-256: `378c1a96540a576bdfd7e7867a8c753c13804fb6f2d6cbbc34549d677d5d9d63`.
The installed APK hash matches. Debug certificate SHA-1 remains
`C7:6A:AA:C4:F9:5E:0E:3F:97:D8:C0:5E:BB:9D:3C:2A:3B:6D:A4:8F`.

All **64 assets**, both app-owned native libraries and both ORT libraries match
the prior verified 0.4.3 artifact byte-for-byte. `Vision.kt` also matches its
0.4.3 source hash. The hotfix does not introduce the held detector worker policy,
native optimization flags, model/cadence or overlay setter experiments.

The uninstalled 0.4.4 heat candidate, tests and source are preserved under
`build/efficiency-0.4.4/`, with restoration notes in `paused-source/README.md`.
Its factory and tests are outside the active build. Resume by applying targeted
changes; do not restore old MainActivity or overwrite these UI fixes.

Evidence: `build/ui-hotfix-0.4.5/` contains assembly logs, baseline failures,
seven passing cases, signature, installed hash and artifact comparison JSON.
