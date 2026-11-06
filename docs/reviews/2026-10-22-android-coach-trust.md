# Android live evidence and session review — 22 October 2026

This increment addresses three learner-facing trust gaps. It does not approve a
paid road launch or establish perception accuracy.

## Current road evidence

The former road HUD used the diagnostic display policy, which retains completed
results for up to three seconds from capture. It could show green boxes and
“Road analysis active” after the native 350 ms coaching freshness gate expired.

`RoadCoachingStatus` now applies that stricter capture-age boundary, source
identity, chronological completion, running-camera, error and critical-thermal
guards to the road HUD. A source-stamped semantic-first publication supplies
boxes without waiting for spatial completion. Expiry sends `null` to the overlay
on the ordinary UI tick. A separately fresh completed result is required to add
the road shape; camera pose readiness alone does not establish completed spatial
analysis. Retained diagnostic playback remains explicitly a test view.

Road status distinguishes camera delay/unavailability, thermal pause, unavailable
voice and basic detection without distance estimates. Idle supervised text says
that silence is not an all-clear. Numbered gear remains unavailable without
vehicle evidence; approximate navigation distance formatting is unchanged.

The stable completed-result setup reader and its Start admission checks remain
in place. New immutable overlay presentations are cached by semantic/spatial
input identity so unchanged ticks do not defeat the redraw guard.

## Recent cue context

The shared coaching strip now labels recently accepted cue text with its origin
(camera, navigation, road preparation, road scene or driving) and age since voice
submission. This age is not the evidence capture age. The cue remains brief and
expires under the existing source/session/route rules. A retained cue is history,
not a continuing assessment of the current road. Acceptance establishes that the
audio worker accepted the prompt; it does not prove audible output or a learner
response. The footer is 84 dp rather than 66 dp to accommodate the context line.

## Temporary completed-session review

An in-memory accumulator begins only after supervised-session admission succeeds.
It counts actual accepted road-session voice submissions, with navigation
separate, and retains at most 12 distinct phrases of at most 160 characters.
Repeated retained phrases retain their counts; other accepted cues remain in
the totals. Test Voice is outside this path. Test/source switches, phone reset,
mount interruption and backgrounding discard incomplete accumulation.

`End session` stores an immutable review. It opens automatically only with valid
measured speed at or below 1 km/h. The parked setup action also opens it, requiring
the user to confirm parking when speed is unavailable. Known movement blocks
opening, including the final confirmation recheck. The review is temporary to
the current activity/app session; it is not persisted, uploaded or exported.

Availability is sampled camera evidence freshness. An interval is credited only
when both sampled endpoints are current and their separation is no more than
500 ms. Longer gaps and the unsampled tail remain unmeasured; the denominator is
the entire session duration. It cannot report 100% availability by excluding
stalls. Availability does not measure category recall or detection accuracy.

Discussion questions use the actual cue families present: space/road users,
turn/bend preparation, phone inclination and vehicle-control cues. Navigation
announcements do not become skill faults. The review makes no score, braking,
gear, compliance, safe-road or observed driver-response claims.

## Verification

`tools/run_road_coaching_tests.ps1`: **20 host cases passed** (8 availability,
12 review). Cases include original semantic capture age versus recent completion,
wrong sources, malformed/future times, thermal/errors, semantic-first availability,
voice loss, sparse sampling/full-duration denominator, reset/discard, bounded
history, separate navigation and uncertainty in quiet/unobserved sessions.

The existing camera-health/readiness/setup/journal host runner also passed.
Added Android assertions cover cue origin/age expiry, unchanged presentation
idempotence, explicit unknown gear without evidence and approximate/invalid
navigation distance. Android assembly and physical execution are owned by the
integrating task; no device qualification is claimed by these host results.

## Remaining release evidence

Verify the taller footer, cue readability, review scrolling, keyboard/setup and
Start interaction on the S24 Ultra. Exercise actual delay/recovery and voice
failure while monitoring. A current-frame HUD does not fix inference availability,
missed hazards, nuisance cues or timing. Sustained runtime, held-out event labels,
instructor relevance/comprehension review and supervised learner validation
remain paid-launch blockers.
