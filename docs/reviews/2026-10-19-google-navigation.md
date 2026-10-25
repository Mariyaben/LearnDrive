# Embedded Google navigation — 0.4.2 implementation review

**20 October checkpoint: Android assembly, full native regression and six
physical smoke checks pass.** Version 0.4.2 / code 22 remains a development
candidate. Real Places predictions and selected-place resolution work with the
restricted Android key. Full route/feed/lifecycle integration and sustained
thermal qualification remain open. The verified 0.4.1 evidence remains a separate
historical record.

The phone's installed APK matches `build/LearnDrive-0.4.2-debug.apk`: 237,675,276
bytes, SHA-256 `fff3d4698501072aa7a9c157687beed5393b63ec2e357fb0b90a09b18c467f66`.
During the user's heat investigation, substantial vision CPU load and rising
temperature were observed while charging. See the
[20 October thermal quick check](2026-10-20-thermal-quickcheck.md). This candidate
has no demonstrated sustained-power improvement yet.

## User-visible flow implemented in source

LearnDrive keeps the existing road camera, detections, Supercombo pipeline,
measured status and shared coaching strip in one dashboard. The parked setup
panel offers Google navigation or the installed offline region. Changing a
navigation provider changes route context without starting a second driving
activity or transferring ownership of capture to the map.

The Google panel contains destination autocomplete and selection. Requests are
asynchronous, begin after typing and use a 300 ms debounce with Places session
tokens. Selecting a result resolves its location and prepares a Google route.
Typing a draft does not clear the current route; choosing another destination
requests its replacement. Search and provider controls are disabled during a
supervised session and while measured motion prevents parked editing.

The existing explicit mount/session confirmation starts supervised monitoring.
When a Google route is ready, this also starts its guidance feed. Ending the
session or backgrounding pauses guidance. Returning to the app requires the
normal supervised-session start; there is no automatic driving-session resume.
The Google route is ephemeral and must not be serialized as an offline OSM route.

Google's embedded NavigationView supplies its map, route and attribution. Its
own voice is set to SILENT so LearnDrive retains one speech authority. Google
initialization uses its own terms flow; a saved provider choice can restore an
already accepted map, but does not accept terms for the user. Google needs an
internet connection. The local Kerala map/router remains a separately selected
fallback with its existing regional coverage and limitations.

Source entry points are `MainActivity.kt`, `navigation/google/` and the existing
provider-neutral `NavigationMapSurface`. Android now declares internet/network
access and the non-exported turn-feed service. The manifest removes imported
background-location and unrelated startup/storage permissions for this
foreground evaluation. Final merged permissions still require APK verification.

## Structured turn context and coaching contract

Google's structured turn feed goes directly into a provider-neutral native
adapter. It is not converted to Valhalla shape indices and no Google maneuver
is placed at the nearest polyline bend. The adapter receives:

- Adapter-owned provider, navigation-state and maneuver values.
- A monotonic journey/session token, route epoch and callback sequence.
- Local monotonic receipt time and nullable step identity.
- Provider estimates of remaining distance and time.

The Android adapter explicitly distinguishes ordinary turns, forks, ramps,
merges, U-turns, roundabouts, straight travel and arrival. Native speech currently
supports ordinary left/right preparation only; other maneuver kinds remain
available to the visual HUD and do not receive an invented ordinary-turn lesson.

Receipt time is not the provider's measurement time or accuracy. A separate,
measured phone GPS fix is mandatory for native turn coaching. The current
provisional gates require two good fixes at least 0.5 seconds apart, horizontal
accuracy at most 15 m, valid measured speed and speed accuracy at most 2 m/s.
The feed expires after 3 seconds and measured position after 2.5 seconds. Poor,
missing, stale, mock or wrong-source evidence cannot qualify a spoken route cue.

Preparation requires more than 3 km/h measured speed, an estimated arrival
window of 8–25 seconds and independent speed/distance lead of 8–35 seconds,
including horizontal-accuracy and receipt-age margins. Braking or shock
suppresses the cue. These are initial eligibility budgets for speech and learner
response, not a validated calculation of safe speed or turn execution.

Only generic phrases such as “Right turn ahead. Check for traffic” are selected
from this provider feed. Google estimates do not qualify the approximate
10 m/50 m voice clips used by the separate local-map policy. Provider-derived
spatial progress, shape index, posted speed and gear remain unknown. Google
route geometry is not used to produce bend coaching in this increment.

Route preparation enters the existing camera/tactical/reflex/terrain arbiter.
It uses the shared global audio cooldown and acknowledgement path, and cannot
interrupt critical speech. The native hot path performs no heap allocation or
network work. Before playback, the pending check revalidates current provider
eligibility, including the remaining learner timing budget.

## Identity, stale work and audio fixes

The native adapter rejects older sessions, route revisions, callback sequences
and regressing steps within a route epoch. Rerouting, unknown navigation,
missing step identity, arrival and stopped navigation invalidate turn guidance.
Step/revision changes invalidate pending speech; accepted encounter identity
survives temporary feed loss so recovery cannot repeat the same lesson.

Explicit route/provider clear retires a session against delayed callbacks.
Temporary unavailability uses a separate invalidation API: it retains the
session high-water marks and independent GPS, and permits recovery through a
newer callback. Every fresh Google start allocates a monotonic session token;
this also fixes Google → Offline → Google recovery after a native clear.

The Messenger feed binds delivery to its owning session. Old-owner delivery and
stale detach must not attach old route data to a newer journey. This is a local
ownership guard: NavInfo does not independently establish the age or originating
route of an SDK payload. Live feed behavior remains a device qualification item.

Integration review also found and fixed two UI/audio races in source:

- A queued caption acceptance could restore an obsolete turn after an event
  change. A navigation-caption generation now guards queued route captions;
  session/epoch/step/state/maneuver changes clear navigation captions.
- Submitted or playing turn PCM could continue after rerouting or feed loss.
  Context invalidation and selective navigation cancellation retire it while
  preserving urgent camera/reflex speech. Native mutation, generation changes
  and cancellation must remain ordered against guidance delivery.

Unknown provider progress is handled explicitly by CoachCaption. It does not
invent distance traveled to determine whether a turn was passed. Provider event
identity, evidence validity and bounded caption lifetime govern retirement.

## Google account and dependency checkpoint

Cloud project `learndrive-development-512108` is configured with Navigation SDK,
Maps SDK for Android and Places API (New). The development key is restricted to
LearnDrive's Android package/development signing certificate and those three
APIs. Its value lives in the ignored local Android key file and is injected into
manifest metadata; no key value belongs in this review or evidence exports.
Cloud configuration is complete. Native Places prediction and selected-place
requests pass on the S24; full Navigation routing/feed qualification is pending.

The candidate source currently pins Navigation 7.6.1 and Places 5.1.1, uses NIO
core-library desugaring, excludes a duplicate standalone play-services-maps
dependency and selects a Datastore patch for native page alignment. These
versions are pinned in the assembled APK. All 55 bundled PCM/model/config/license
copies match; signature verification and all 18 native libraries' minimum 16 KiB
ELF LOAD alignment pass. This is not an actual 16 KiB-page-device runtime test.

The initial assembly was stopped after R8 repeatedly reported errors rewriting
existing Valhalla Kotlin 2.3 metadata under NIO desugaring. AGP 8.13.2 with Gradle
8.13 supplies D8/R8 8.13.19 and fixes that compatibility issue. Kotlin remains
2.2.10. Both APKs now assemble successfully. One intermediate instrumentation
compile failed because tests referenced APIs added after that invocation's app
compile; the final incremental assembly includes those APIs and passes.

## Verification status

| Check | Current result |
| --- | --- |
| Focused native build: `test_offline_geometry`, `guidance_pipeline_tests` | PASS |
| Navigation/matching/provider checks | **103 checks PASS** |
| Shared provider/camera selection and speech cooldown | PASS in native coordinator test |
| Provider hot-path allocation check | PASS; zero allocations in repeated evaluation |
| Full native regression suite against the final 0.4.2 sources | PASS: 10/10 groups, including the real Supercombo model |
| Complete Android debug and instrumentation APK assembly | PASS; `android-build-lifecycle.txt` |
| APK identity, signature, merged permissions, assets and native LOAD alignment | PASS; installed APK SHA matches the staged artifact |
| Android contract and caption checks | PASS: 2 adapter + 3 caption cases; camera/offline integration regression still pending for 0.4.2 |
| Real Places predictions and selected-place resolution on S24 Ultra / Android 16 | PASS: 1 case; shared autocomplete token, LOCATION-only fetch, 425 ms observed |
| Google map authorization/rendering, actual destination route and terms behavior | PENDING |
| Live structured feed, physical GPS gates, rerouting, pause/resume and provider switching | PENDING |
| Audible turn cancellation, urgent camera speech preservation and caption race checks | PENDING |

Focused native commands were:

```text
cmake --build build/native-spatial --target test_offline_geometry guidance_pipeline_tests --parallel 2
build/native-spatial/test_offline_geometry.exe
build/native-spatial/guidance_pipeline_tests.exe
```

The added checks exercise independent feed/GPS staleness, GPS quality, unsupported
maneuvers, null step, rerouting/stopped/arrival, session and route tombstones,
old sequences/steps, same-session recovery, consumed encounters, pending-speech
invalidation, timing-budget expiry and shared provider/camera arbitration.
They do not substitute for Google API, Android lifecycle or real-road testing.

## Remaining product work

- Qualify sustained camera + Supercombo + map + location + audio workload on the
  S24 Ultra: frame pacing, UI responsiveness, audible latency, battery and
  thermal behavior. Short functional checks cannot establish smoothness.
- Validate actual map matching on parallel roads, flyovers and close junctions,
  route changes, network loss and GPS uncertainty before learners rely on cues.
- Build complete coaching episodes: prepare, observe the learner's response,
  traverse, retire and produce an evidence-linked review. Advancing a navigation
  step is not proof that the learner drove correctly.
- Resolve camera/map duplicate encounters and validate useful timing and wording
  with instructors. Unsupported roundabout/fork/ramp/merge/U-turn lessons remain
  visual only; crossings, humps and sign lessons need qualified producers.
- Keep gear/RPM unknown without validated vehicle-specific evidence. No universal
  speed-to-gear table or numbered gear command is introduced here.
- Keep Google-derived bend voice and unqualified projected AR arrows disabled.
  Route geometry alone does not establish a safe bend lesson or a maneuver
  anchor.
- Run a supervised instructor/learner pilot with measurable false-cue, late-cue,
  interruption and usefulness outcomes before considering a learner release.
  This increment does not establish equivalence to a human driving instructor.

Primary integration references: [turn-by-turn feed](https://developers.google.com/maps/documentation/navigation/android-sdk/tbt-feed),
[NavInfo](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/mapsplatform/turnbyturn/model/NavInfo),
[StepInfo](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/mapsplatform/turnbyturn/model/StepInfo),
[Navigator](https://developers.google.com/maps/documentation/navigation/android-sdk/reference/com/google/android/libraries/navigation/Navigator),
[release notes](https://developers.google.com/maps/documentation/navigation/android-sdk/release-notes).
