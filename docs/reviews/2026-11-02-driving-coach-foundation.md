# Driving-coach foundation: implementation and evidence

2026-11-02. Branch `driving-coach-foundation`. Android 0.7.0, version code 31.

The product specification now covers the complete learner-driving curriculum.
This increment implements one sequenced junction-preparation task above the
existing native temporal policy, detection, map and delivery pipelines. It is
an experimental first lesson, not the complete coach or a paid-launch approval.

## Implemented

- Parked practice-focus selection and goal explanation in the existing dashboard.
- Native episode identity includes source, route, event and reroute epoch.
- Early observation/signal/position planning; delivery-stage tracking; measured
  post-delivery speed response; optional approach preparation; quiet execution.
- Late or unsupported steps are skipped. Braking/shock and stale camera, GPS,
  route or telemetry pause task instruction. Routine audio is cancelled at the
  final-approach boundary; independently validated warnings retain priority.
- Refusal/interruption does not count as delivery or unlock a later step.
- Four recorded phrases, matching native wording and IDs 56–59.
- Live lesson phase and parked aggregate records, capped at 20 sessions.
  Backgrounding, mode changes and calibration/mount resets save interrupted
  records instead of silently discarding active practice. No mastery grading.
- Existing voice toggle, routine pacing and urgent validation remain in force.
  The optional pretrained shadow worker is disabled for this pilot.

## Fresh verification

| Check | Result / scope |
|---|---|
| Native CTest | 17/17 groups pass, including 21 task-coach groups and 25 coaching-engine groups |
| Native integration | Synthetic frames + native provider feed/GPS + coordinator produce planning and approach; feed loss invalidates pending instruction |
| Python | 79 tests pass |
| Existing Java host scripts | Camera health, mount/reference/start, road status, review, audio policy and reasoning gates pass |
| Native allocation benchmark | Zero ordinary C++ allocations over 20,000 policy ticks, both ordinary and task-enabled configurations; desktop only |
| Android | App and instrumentation APKs assemble for the current source |
| Packaged assets | All 59 voices, models and notices match the source assets |
| APK checks | v2 signature verified; ZIP 16 KiB alignment verified; all 24 ELF libraries have load alignment of at least 16 KiB |
| Android runtime | Not executed for 0.7.0: ADB lists no connected device |
| Teaching usefulness | Not established; no independent instructor labels |
| Sustained heat / lower-cost phones | Not measured in this increment |

The assets verifier checks all 59 recorded prompts for matching native text,
non-silent PCM and pinned models. Local logs: `build/coach-ctest.txt`,
`build/coach-python-tests.txt`, `build/coach-android-build.txt`,
`build/coach-apk-assets.txt`, `build/coach-apk-signing.txt` and
`build/coach-apk-alignment.txt`.

App artifact: `build/LearnDrive-0.7.0-debug.apk`, 247,613,918 bytes.
SHA-256: `3857c9793aa5f99320972ab2de4d707028a0f599d381c66fb2dd11a0352fe0d3`.
Test artifact: `build/LearnDrive-0.7.0-androidTest.apk`.

## Reproducible episode evidence

`task_coach_scenarios` generates `build/junction-task-timelines.jsonl`.
Inputs are supplied synthetic measurements and clocks. Visual acceptance is used
to exercise sequencing; this is not a claim of spoken playback or driving skill.

| Scenario | Accepted instructions at simulated elapsed times | Result |
|---|---|---|
| Early, steady speed | Planning at 1 s; approach at 18 s | One delivery per step, then quiet |
| Early, observed slowing | Planning at 1 s | Approach reminder suppressed |
| First evidence arrives late | None | Missed teaching not compressed into approach |
| Independent GPS becomes stale | Planning at 1 s | Subsequent teaching paused |

All four end with the driving outcome unassessed. Mirror checks, indicators,
actual position and vehicle controls remain unobserved throughout.

## Remaining work

Use [the execution plan](../coach/EXECUTION_PLAN.md). First qualify the junction
lesson on the phone, review full scenario intervals for usefulness and omissions,
then expand task coverage. The old semantic repetition/pacing inconsistency and
perception freshness under serial model inference remain explicit foundation
work. Existing road-user, slope and bend cues have not all become task lessons.
No exact gear, clutch or accelerator state is newly inferred. OBD stays on hold;
the previously undone dataset work remains untouched.
