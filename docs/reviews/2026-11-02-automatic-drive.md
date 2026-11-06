# Automatic drive integration

Android 0.7.1 (32), branch `driving-coach-foundation`.

The 0.7.0 junction lesson selector was a development restriction that did not fit
the intended learner flow. The normal Android session now enables one automatic
coordinator for supported junction, bend, hill and road-user guidance. Destination
selection adds route context; independent camera/terrain support works without it.
Enabling automatic task selection does not automatically start monitoring.

## Changes

- Native `DriveCoach` shares the existing scene, temporal policy, ledger and
  evidence validator. Urgent warnings retain priority over teaching tasks.
- A measured developing conflict ranks ahead of routine junction pedagogy without
  being promoted to warning authority or bypassing routine pacing.
- Same-encounter semantic variants remain suppressed after the ordinary speech
  interval. Already-informed or low-value candidates cannot starve another task.
- Junction quiet time ends when its context ends. Route loss pauses route guidance
  while independent road-user/terrain inputs can still be selected.
- Routine automatic teaching needs fresh camera evidence. Power, bend and route
  instructions require measured motion; stationary/unknown motion is not a basis
  for them.
- Spoken coaching has a continuation gate, separate from immutable onset evidence.
  Current support for the same event is checked up to ten times per second, outside
  the audio monitor. Expired evidence and source/context changes can stop PCM.
  Brief actor loss may retain only independently captured, still-fresh evidence.
- Practice records aggregate actual completed/intentional visual deliveries across
  supported domains. No inferred control use, driving grade or mastery is added.

## Verification

| Check | Fresh result |
|---|---|
| Native CTest | 19/19 groups pass |
| Automatic-drive tests | 19 named groups, including 90 profile/feature/fault combinations and three continuous synthetic mixed drives |
| Existing Python suite | 79 tests pass |
| Allocation benchmark | Zero ordinary C++ allocations during 20,000 policy ticks in ordinary, junction and automatic configurations; desktop only |
| Recorded-perception policy replay | Three cached captures × three profiles × two modes = 18 configurations, 4,734 decisions; outputs identical to prior ordinary policy on those inputs |
| Android | App and instrumentation APKs assemble; new JNI/state/record, UI-startup and ongoing-PCM tests compile |
| Packaged APK | 59 voices/models/notices match source; v2 signature and ZIP 16 KiB alignment pass; all 24 ELF libraries have load alignment of at least 16 KiB |
| Phone runtime / audible output | Not run for this version: user requested continuing without the phone |
| Instructor usefulness | Not established; no independent instructor labels |

The continuous synthetic drive exercises road-user space, left bend, uphill,
route-backed junction preparation, warning interruption, measured slowing that
suppresses the second junction reminder, final-approach warning priority, downhill,
camera loss/recovery, right bend and a source change. Each profile admits nine
visual cues across the supported domains, including exactly two urgent warnings.
There is no admission during the camera-loss interval or after session inactivity.
All driving outcomes and unmeasured controls remain unassessed/unobserved.

Reproduction commands and evidence scope are documented in
[AUTOMATIC_DRIVE.md](../coach/AUTOMATIC_DRIVE.md). Local outputs:

- `build/automatic-ctest.txt`
- `build/automatic-python-tests.txt`
- `build/automatic-android-build.txt`
- `build/automatic-drive/mixed-drive.jsonl`
- `build/automatic-drive/recorded-policy.json`

App: `build/LearnDrive-0.7.1-debug.apk` (245,884,095 bytes).
SHA-256: `5fdcb19c05a79d45dd546b33b486751b1ec2393f9902be7734dd555bf6ac5a78`.
Instrumentation: `build/LearnDrive-0.7.1-androidTest.apk`.

These results establish software integration and specific regressions. They do not
establish perception ground truth, audible delivery, coaching usefulness, sustained
phone performance or dependable coverage of the complete driving curriculum.
Existing bend/hill/space phrases are coordinated; they have not all acquired the
richer multi-step junction pedagogy. OBD, exact control-state inference and dataset
generation remain outside this increment.
