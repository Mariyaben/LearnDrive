# Unified coach foundation — Android 0.4.1

The user's reference video defines one dashboard with navigation, measured status,
the live road view and a shared coaching strip. This increment replaces the
separate navigation activity. It is a development foundation; complete coaching
episodes and learner-release qualification remain open.

## Implemented

- A persistent local map and route beside the original CameraX preview/detection
  overlay, a compact status column and one caption footer. Parked setup/search
  overlays the left side; lab controls live in settings. Monitoring keeps the
  screen awake. No numbered gear or unqualified projected AR arrows are displayed.
- In-place town, landmark and business suggestions from a prebuilt SQLite FTS4
  index. Kerala v3 contains 178,079 records. Route preparation runs off the UI
  thread and joins the same native coach without restarting capture. Drafting a
  new destination preserves the committed route until its replacement is ready.
- Local PMTiles rendering through a replaceable map surface, async route display,
  measured position expiry and independent camera/map lifecycle management.
  Street/place text labels await glyph data or the selected Google integration.
- Bounded turn-preparation phrases, including approximate distances only with
  qualified GPS accuracy and sufficient lead. Map and camera share speech gates.
- Accepted captions expire after playback/read time, source changes, route loss,
  uncertain position or passing their map event. Interrupted action speech also
  clears its caption. Upcoming maneuvers remain separate from spoken coaching.
- The policy no longer selects “Maintain speed” from an empty camera scene.
  Ordinary Android road coaching uses measured telemetry directly; missing speed
  stays unknown. Manual speed remains available only in explicit test modes.
  Mock locations cannot qualify road position or speed.
- Camera startup waits for a resumed activity and attached/measured preview,
  through a bounded three-second readiness retry, with one recovery for initial
  CameraX stream configuration failure. This fixes an observed indefinite
  layout-wait stall; queued waits are invalidated on pause or successful binding.

## Verification and evidence

Artifact identity and final check results are recorded in
`build/unified-coach/verification.json`; APK: `build/LearnDrive-0.4.1-debug.apk`.
The existing development signing certificate is preserved. App data and the
user's saved route are preserved; the integration fixture backs up/restores the
route and does not constitute a road drive.

The native suite passes all 10 groups: 132 core cases, 55 navigation checks,
shared map/camera selection and cooldown, spatial contracts and the real pinned
Supercombo model. Model/voice/license/config APK copies are verified, including
all 55 PCM clips and both native ABIs.

Physical tests on SM-S928B / Android 16 check search, actual model publication,
map rendering, route attachment without source restart, committed-route retention,
both visible dashboard regions, and background/resume recovery. Caption tests
cover time/source expiry, cancellation and passed/rerouted/lost-position events.
The final physical suite passed **5/5 cases in 8.558 seconds**, including activity
recreation. See `build/unified-coach/android-tests.txt`. App-window images are
stored alongside it; the monitoring image uses a bench layout fixture, not a
qualified mount or road drive.

Earlier runs exposed two test-environment issues: a locked phone paused the app
before capture, and unlocking/orientation recreated the activity while the test
retained its old search field. The test now binds the current activity, detects
recreation during continuity assertions and captures only its own app window.
Neither interrupted run is counted as a camera/search pass.

`kerala-runtime-verification.json` contains standalone phone index/router timings.
These exclude sustained camera/map/audio workload. The cold route target of
100 ms is not established; the final first route took 135.0 ms, with 403.6 ms
separate engine startup and 57.7 ms warm median (68.7 ms maximum). Index load was
44.8 ms and four representative queries took 2.4–5.9 ms. Short
functional tests cannot establish smoothness, battery life or thermal stability.

## Next checkpoints

The user selected embedded Google Places/Navigation and requested setup help.
[Project, billing and restricted-key instructions](../GOOGLE_NAVIGATION_SETUP.md)
are ready; the user supplied project ID `learndrive-development-512108` and the
development key is stored in the ignored Android key file. Cloud Console confirms
the saved Android package/certificate restriction and access to only Navigation
SDK, Maps SDK for Android and Places API (New), all enabled in this project.
Google has not been activated
in this build, which still has no INTERNET permission. Its current SDK/toolchain
requirements need a deliberate integration pass.

Then build one complete turn/bend episode: early preparation, observed response,
traversal, retirement and an evidence-linked review. Resolve map/camera duplicate
events and useful timing before expanding to crossings, humps, signs, roundabouts
and vehicle-specific gear inference. Keep the earlier screen-recording false
detection/usefulness findings in scope.

Real GPS timing, parallel roads/flyovers, route restrictions, instructor-approved
wording, physical audible behavior and learner usefulness remain unqualified.
See [the detailed build checkpoints](../product/UNIFIED_DRIVING_COACH.md).
