# First device qualification profile: Samsung S24 Ultra / Android 16

Recorded 17 October 2026 from the user's stated test device.

| Field | Current evidence |
| --- | --- |
| Phone | Samsung S24 Ultra — user supplied |
| Android version | Android 16 — user supplied |
| Exact model/build, firmware and API level | Export metadata: SM-S928B; Android 16 / API 36; BP4A.251205.006.S928BXXS6DZG1 |
| RAM, selected camera/lens and runtime provider | Pending measurement; do not infer from a product listing |
| Physical connection | Authorized ADB connection on 19 October; final 0.4.1 dashboard checks executed while unlocked |
| APK candidate | 0.4.1 / code 21 unified dashboard candidate; 0.4.0 / code 20 navigation baseline previously verified; see current review for artifact hashes |
| Stationary setup smoke test | User reported the supplied checklist worked on 17 October 2026; no device logs supplied |
| Diagnostic capture/export | First physical export passes structural inspection: 183 records, zero reported loss; all decisions remain in setup |
| Qualification | User-reported setup evidence plus short camera diagnostic evidence; active coaching, sustained hardware and road acceptance pending |

19 October integration evidence: Kerala offline routing/map checks passed in
0.4.0. During 0.4.1 work, physical tests verified camera/model publication, UI
ticks, in-place destination search, route attachment without capture-source
restart, preservation of the committed route while drafting a replacement, and
frame/model recovery after backgrounding. The final pre-dashboard run passed
three cases in 5.2 seconds. These are functional integration checks.

The final combined-dashboard run passed all five cases in 8.558 seconds, including
camera/model/route recovery after backgrounding and activity recreation. The
map/camera/status/caption layout was inspected in app-window images. Earlier
interrupted and failed candidates are not counted as passes. A real startup
layout-wait stall was repaired with bounded readiness checks. These are functional
bench checks; no sustained or road qualification is implied.

Use this device first to execute [the parked setup/angle-fixture protocol](../SETUP_FLOW.md#physical-angle-fixture-and-lifecycle-protocol).
Record the installed APK hash and actual build, camera metadata, permission state,
display/font scale and audio route with each run. Check first-launch permissions,
GPS/manual provenance, optional reference completion/skip, fresh frame gating,
Start/End, app background/resume and the limited roll-change behavior.

The user reported "everything worked" after the stationary install/camera/sound/
Start/End/background checklist and optional level-reference instructions. Record
this as a user-reported smoke pass, not instrumented per-step evidence. No measured
angle error, roll-check timing, latency, battery/thermal or road-accuracy result
was supplied. Those gates remain open.

The [first exported physical capture](../reviews/2026-10-17-s24-ultra-first-export.md)
now supplies device/build metadata and approximately 41 seconds of sampled camera
evidence. It contains 140 frame observations and 42 inactive/setup decisions,
with no accepted audio submissions. Sampled processing median is 141.870 ms and
capture-to-completion median is 224.316 ms. These are short camera measurements,
not active-session, camera FPS, audible latency or thermal qualification. Measured
speed is unavailable; the 30 km/h advice setting is explicitly manual fallback.
The next device check must demonstrate a MONITORING transition in the exported
trace. Physical delete/cancellation/interruption and failure paths remain open.

Android 16 qualification must exercise actual camera, location, motion and audio
permission/lifecycle behavior on this phone. Successful compilation against the
repository's current SDK does not establish runtime acceptance.

After setup acceptance, use the device-performance work package to measure
inference cadence, native decision latency, actual audio onset/interruption,
memory, battery/power and more than 30 minutes of thermal behavior. Record
ambient conditions, charging state, case/mount and starting battery level.
The S24 Ultra's qualification cannot substitute for a modest-phone profile.

The current `tools/profile_android.py` measures the existing camera/performance
criteria and relaunches the app. With the new setup gate, capture whether a road
session was actually started; camera-only inference and the native microbenchmark
do not establish active-session or physical-audio acceptance. Update the profiler
to report that distinction before treating it as full session qualification.

No FPS, battery life, audible latency, perception accuracy or supported Android 16
behavior is claimed from the device name alone. Exact gear-coaching research also
needs an identified manual car and independent gear/RPM reference data.
