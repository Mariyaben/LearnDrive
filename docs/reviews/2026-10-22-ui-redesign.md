# LearnDrive 0.4.7 — dashboard and landscape interaction

The requested visual direction is now charcoal and white. The map occupies
64% of the main driving area, with a smaller road-camera pane and one shared
instruction rail. Setup uses typography and dividers rather than a card grid.
Google's maneuver/lane header, attribution and map controls remain available.

## Behavior

- Get ready and Destination are separate pages in one parked setup drawer.
  Start stays fixed at its bottom. Last session appears beside it when a
  completed temporary review exists. Optional settings and lab controls scroll.
- Where to opens destination entry and the normal keyboard. Full-screen IME
  extraction is disabled. A compact input and side-by-side Google predictions
  use the limited height above a landscape keyboard. Result attribution stays
  fixed outside the scrolling list.
- Committing a successful destination returns to preparation. Restoring a route
  in the background must not dismiss an uncommitted destination draft.
- The manifest uses `sensorLandscape`, permitting both landscape directions.
  A display listener updates both CameraX target rotations: a 180-degree UI flip
  must not leave analysis using the previous image orientation.
- Display updates retain text, focus, selection, scroll and touch bounds when
  the presented values do not change. Hidden lab diagnostics no longer format
  and replace text during ordinary camera use. Drawer animation is a single
  short parked transition and respects the system animation setting.
- Unavailable speed stays unavailable; an unmeasured gear tile stays hidden.
  Camera freshness, mount confirmation, session admission and audio ownership
  keep their existing evidence requirements.

The Google map uses the SDK's supported night mode API to match the chosen
dark theme. See [Google's UI controls reference](https://developers.google.com/maps/documentation/navigation/android-sdk/controls).

## Verification

The installed 0.4.7 APK is `build/LearnDrive-0.4.7-debug.apk`, 242,712,568 bytes,
SHA-256 `b004ee795c96a335e2425cfd69ae36388e913b9dfe056f0638f1bdf4d8075393`.
The installed hash matches, and all 82 packaged asset/native-runtime entries
are identical to 0.4.6. Vision.kt retains SHA-256
`4881f2cb44a244873aa723950bfadf841175411e5969b86b4a933245171f8eb5`.

- S24 Ultra / Android 16: **17/17 checks**, no skips. Three real interaction,
  rotation and Google navigation cases plus 14 setup/caption/overlay/review/
  trace/feed/audio regressions.
- Rotation verification forced both display rotations and checked the real
  CameraX frame rotation changed by 180 degrees while capture source continued.
  It did not physically rotate a vehicle mount or start a road session.
- Destination checks used actual touch dispatch and the Samsung IME, preserved
  drafts/cursors/source, checked suggestions were above the keyboard without
  obscuring the input, selected a real destination, and retained camera and route
  through Activity recreation. No mock GPS or SDK simulator was used.
- Short parked interaction sample: **82 UI frames**, p50 **5.00 ms**, p95
  **15.21 ms**, maximum **23.14 ms**, two exceeded their reported deadline.
  The phone was USB connected and charging. This is neither a sustained FPS
  qualification nor a comparison against the previous build.

Logs, scoped app screenshots and verification JSON are in
`build/ui-redesign-0.4.7/`. The initial keyboard regression failed on clipping
before repair. Preliminary APKs/screenshots are not the verified artifact.

Subsequent user feedback found Start label/readiness chatter not covered by the
original stable-semantic-publication fixture. The expanded regression fails on
0.4.7; its repair and automatic level-reference start are the 0.4.8 increment.

This change does not claim improved detection accuracy, road qualification or
sustained heat performance. Models, native libraries, inference cadence and
the safety policies have not been retuned for visual polish. Previous replay
results remain evidence for their recorded assemblies, not new road testing.
