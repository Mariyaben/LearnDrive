# LearnDrive 0.4.8 — stable entry and automatic level calibration

The 0.4.7 Start action still changed its label and enabled state as completed
camera evidence changed. The expanded held-touch regression reproduced the
problem on that installed APK: `Start supervised session` became
`Start limited session` during the same tap. See
`build/start-repair-0.4.8/before-047.txt`.

## Start control

The parked entry action now keeps the label **Start session** and a stable touch
target. Camera freshness and capability updates do not disable it under a
finger. Tapping opens the confirmation and explains camera/voice/mount readiness.
Both actual admission paths recheck `canStartRoad`, including the original
350 ms capture bound, current source, mount support, camera faults, critical
heat, voice readiness and measured movement. Opening a dialog is not monitoring.

Confirming Start does not require tapping during a brief fresh-frame window.
The bounded request waits for fresh evidence instead. The explicit basic path
also waits for current camera/voice evidence without claiming a level reference.

Unchanged navigation status and hidden diagnostic text no longer trigger
repeated text replacements during normal camera use.

## Default level-reference flow

Start offers **Level ground · start**. The accompanying confirmation explicitly
asks the user to be parked on level ground, with the fixed rear camera forward.
That action automatically requests a new normalization, waits for its fresh
reference, then starts only when current camera admission succeeds. The user no
longer needs to find a separate level-reference control in optional settings.

A stationary phone does not prove level ground. Automatic IMU normalization
alone remains insufficient. No gear/grade/mount evidence is invented.

**Back** cancels a pending start. Backgrounding, a changed source, measured
movement, a camera/motion failure, reference expiry or a bounded timeout cancels
the request. It cannot silently resume when conditions return. The request can
be consumed once; the final session admission is checked again immediately.
Missing motion support cannot produce a level reference. **Skip slope lessons**
allows the existing basic-coaching path under its unchanged camera/motion guards.
Ending/backgrounding a session still clears its level confirmation.

## Evidence

Installed artifact: `build/LearnDrive-0.4.8-debug.apk`, version **0.4.8 / 28**,
**238,058,088 bytes**, SHA-256
`a81b7439a78cd3f1a6a00888548a594f27a12c2768e50fafe8fa3fefe610d11d`.
The installed package hash matches. Debug signing retains the configured Google
Android-key SHA-1. All **82** packaged asset/native-runtime entries are identical
to 0.4.6; Vision.kt retains SHA-256
`4881f2cb44a244873aa723950bfadf841175411e5969b86b4a933245171f8eb5`.

Artifacts and logs are in `build/start-repair-0.4.8/`:

- Host verification passed the existing **13** setup/reference/mount cases and
  **nine** bounded automatic-start cases, plus camera-health/readiness and journal
  contracts. Cases cover source changes, movement, background, faults, cancelled
  or expired references, stale-camera waits, timeout and one-time consumption.
- The final source-verified APK passed **19/19 Android checks**, with no skips:
  held-touch readiness churn and admission guards; automatic-reference entry and
  cancellation; captions; persistent dashboard; overlay; retained trace; review;
  Google feed contracts; actual voice output; touch/IME drafts; both landscape
  display/frame rotations; real autocomplete/route/feed/recreation with camera
  continuity. See `final-19-tests.txt` and `artifact-verification.json`.
- The calibration-cancel case starts with explicitly **stale camera-capability
  fixtures** and confirms that admission is still blocked. The ground-confirmed
  request initializes automatically, then is cancelled before completion. A
  six-second follow-up checks it never starts later or silently requalifies a
  level reference. Native motion/GPS is never injected and monitoring is never
  approved by that fixture. A completed automatic start on a properly mounted
  vehicle remains a physical qualification check.
- Test synchronization now respects AlertDialog's queued OnShow event and waits
  for actual WindowManager focus before the app-dialog PixelCopy. The focus
  assertion remains intact; the initial focus-dispatch failure is preserved in
  `review-focus-dispatch.txt`.
- Short parked UI sample: **89** frames, p50 **4.11 ms**, p95 **14.38 ms**, maximum
  **38.90 ms**; one exceeded its reported deadline. This is not sustained FPS or
  a controlled performance comparison. The phone was USB connected/charging.

The charcoal UI and both landscape directions from 0.4.7 are retained. Models,
native libraries and inference scheduling have not been retuned. Heat work and
road qualification remain separate work.
