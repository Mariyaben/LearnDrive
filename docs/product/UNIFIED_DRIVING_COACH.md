# Unified LearnDrive coach

Product direction recorded 19 October 2026 from the user's correction and
`D:/Downloads/Mobile Devices/gemini_generated_video_1420d4bc.mp4`.
This is the product target, not a statement that the lab build already supplies it.

## Intended experience

One landscape dashboard combines a navigation map and route, a live road-camera
view, compact driving status and one prominent coaching strip. Destination entry
belongs in the same app flow: **Where to? → choose a suggestion → start drive**.
Region imports, sliders and diagnostics belong in settings. Selecting a destination
must preserve camera capture, detector/Supercombo state and the shared coach.

The assistant should **observe → anticipate → prepare → observe the learner's
response → adapt**. Object narration or navigation announcements alone do not
satisfy this. Coaching should remain useful without a destination.

## Video evidence

The reference is 20 seconds, 1280×720, 24 FPS. Twelve sampled frames and extracted
audio are retained in `build/reference-coach-video/`. Verified visual captions
include bend preparation at about 5–8 seconds, crossing-related instruction near
15 seconds and roundabout preparation near 18 seconds. The map and road view are
visible together; gear/speed status sits between them; instructions share a footer.

This generated clip contains inconsistent numeric readouts. Use its layout and
coaching intent; real measurements must supply production values. A local speech
recognition draft has confidence 0.36–0.57 and inconsistent segment timestamps;
it is not an authoritative transcript. The visual captions support this review.

## Recommended map provider

Recommend Google Places plus Google Navigation SDK inside LearnDrive for the first
market version. Search, routing and rerouting use the provider; camera/Supercombo
processing and the coaching policy remain local. A Google Maps launch link does
not supply the structured live route feed required by the coach.

Google needs internet and a billing-enabled project/API key. Its SDK caches journey
information but offers no full offline mode. The user selected this direction and
requested project setup help, changing the earlier offline-only navigation
constraint. No Google service or billing has been activated by this work.
Keep the existing offline engine as an optional independent fallback, with one
active route and one speech authority. Do not archive Google route data through
the OSM route-persistence path without checking permitted storage terms.

Primary references checked 19 October:
[SDK](https://developers.google.com/maps/documentation/navigation/android-sdk/overview),
[maneuver feed](https://developers.google.com/maps/documentation/navigation/android-sdk/tbt-feed),
[geometry/offline FAQ](https://developers.google.com/maps/documentation/navigation/android-sdk/faq),
[policies](https://developers.google.com/maps/documentation/navigation/android-sdk/policies).

## Coaching contract

Navigation supplies advance layout/maneuver context. Camera perception supplies
current visible road-user/path evidence. Fresh measured motion supplies speed and
response evidence. Each event has an identity, approach/traversal phase, confidence,
freshness and playback history. Prepare early, observe before repeating, retire
passed events, and invalidate old context on rerouting or uncertain position.
Urgent risks outrank routine preparation; both share audio cooldown and acceptance.

Exact gears require a qualified vehicle profile and gear/RPM/clutch-engagement
evidence. Do not substitute universal speed-to-gear bands. Measured slowing does
not prove a pedal action. Unavailable values stay unavailable. AR directions need
qualified camera/map projection, and displayed metric distances need qualified
measurements; visual polish cannot supply those observations.

Crossings, bumps and signs need actual qualified producers: the existing detector
and Supercombo do not establish all these classes. Map tags may provide hints.
Losing a detection does not prove a crossing is clear or authorize acceleration.
The forward-camera phone does not observe everything a human instructor sees.

## Execution order

1. Repair the integration seam: in-place suggestions, background route attachment,
   continuing capture, fast search and no separate navigation activity.
2. Build the reference dashboard: map and road view together, compact status,
   destination control and a single shared instruction strip. Verify the actual
   S24 layout, keyboard, insets, readability and ordinary-session controls.
3. Add the chosen provider through a route-context adapter. Google setup needs
   user-owned project/billing/key configuration. Test real search, updates,
   rerouting, unavailable service and audio coordination; a mock is not integration.
   Recheck combined capture freshness, speech timing and thermal behavior after
   both dashboard and provider changes, before adding further detector workload.
4. Implement turn/bend coaching episodes through preparation, driver response,
   traversal and completion. Exercise already-slowing drivers and conflicting,
   missing or stale evidence, with source/quality guards preserved.
5. Qualify crossings/bumps/signs on labelled Kerala data, including held-out false
   cues, misses, availability and instruction timing with instructor review.
6. Continue the separate phone-only RPM/gear research and validate recommendations
   only for supported vehicles, preserving unknown and abstention states.
7. Measure the combined workload: model freshness, frame pacing, speech onset and
   completion, battery and 30-minute thermal behavior. A microbenchmark alone does
   not establish app smoothness.
8. Run instructor/learner sessions for comprehension, correct response, distraction
   and repeated use. A polished demonstration is not market validation.

The earlier screen-recording false-detection/usefulness findings remain in scope.
This definition replaces the separate-map-interface interpretation of 0.4.0.

## Build checkpoints

Work proceeds through these reviewable increments. Passing software checks does
not close the instructor or learner gates.

| Increment | Concrete output | Completion evidence |
| --- | --- | --- |
| Unified foundation | Persistent map, compact measured status, road camera and one expiring caption; destination setup and lab controls outside the driving surface | S24 screenshot and physical search/route/camera/model/background-recovery tests; no route loss while drafting a replacement |
| Production navigation | Embedded Google search/map/navigation, translated route context, one speech owner, rerouting and unavailable-service states | Real project configuration and phone tests; no camera upload; no duplicate Google/coach speech |
| One complete turn/bend lesson | Route-version/event identity; prepare, approaching, traversing and passed states; measured response and prompt history | Deterministic cases for early/late fixes, already slowing, stopped, off route, stale GPS, parallel roads, reroutes, competing camera risks and repeated bends |
| Useful timing | Preparation budget covers position uncertainty, phrase duration and learner response; avoid arbitrary late ten-metre instructions | Timestamped held-out clips and supervised drives, instructor-labelled useful windows, late/missed/false cues reported together |
| Evidence-linked drive review | A few specific observed moments and a practice suggestion, linked to delivered cues and measured response | Every claim traceable to an episode; unknown actions omitted; learner can understand the review without raw diagnostic logs |
| Expanded road features | Qualified crossing, hump, sign and roundabout producers, connected to the same event and speech policy | Kerala held-out labels, false/missed cues and availability, instructor-approved wording and response windows |
| Vehicle-specific manual coaching | Qualified phone-only RPM/gear evidence and vehicle profile, otherwise explicit unknown | Held-out vehicle tests; wrong-gear and abstention rates; no universal speed-to-gear rule |
| Pilot and release decision | Stable combined workload and supervised instructor/learner pilot | Sustained phone thermal/audio checks, comprehension and distraction review, repeated-use and willingness-to-pay evidence |

For the first complete lesson, use a route bend/turn with fresh measured speed.
Prepare before entry, remain quiet when the learner is already responding,
invalidate advice on uncertain position, and retire the event after passing.
The review may say that measured speed reduced before the event; it must not
claim the learner used the brake, selected the correct gear or checked mirrors.
Route geometry and the camera corridor must refer to the same encounter rather
than announce two separate bend lessons.

For ordinary driving, the dashboard shows measured speed or an unavailable mark,
the next qualified maneuver and capability status. Gear remains unavailable until
supported. Detailed sensor values, inference timings, simulated inputs and region
imports stay in settings. The map provider remains replaceable; there is still
one destination, one active route, one camera pipeline and one coaching voice.

## What makes it a coach

An encounter should lead to a useful, timely preparation cue and then an observed
response, rather than a running description of everything visible. Start with
turns/bends, build an evidence-linked review, and only then adapt repetition and
lesson difficulty to demonstrated needs and the learner's chosen experience
level. Do not infer competence or anxiety from a few sensor readings.

Detailed explanations belong after the maneuver or in the parked review. Later
hands-free questions can add conversational help, with a clear microphone choice
and the same workload/priority policy; free-form generated replies must not issue
live vehicle-control commands. English phrases need instructor and learner review
first, followed by deliberate Malayalam/local-language work rather than automatic
translation of unqualified instructions.

The first commercial proposition is a contextual supervised-practice companion.
Matching a human instructor's full observation and judgment remains an aspiration:
the mounted forward camera does not establish mirror checks, pedal/clutch use,
blind spots or the current gear. The interface should make supported coaching
useful while preserving those unknowns.
