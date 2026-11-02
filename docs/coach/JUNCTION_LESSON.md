# Junction preparation: first task contract

Experimental software pilot, not instructor-qualified. Version 0.7.0.

Select **More setup options → Practice focus → Junction preparation · pilot**
while parked, choose a destination, and use the existing supervised-session flow.
The map, camera and coach remain in the same dashboard. The goal and saved records
are accessible while parked. General practice remains available.

## Objective and sequence

Prepare observation, signal/position planning and approach before an upcoming
junction. This increment does not decide right of way, tell the learner to enter
or turn now, or issue numbered gear or pedal commands.

| Phase | Condition | Behavior |
|---|---|---|
| Wait | No relevant route, unsupported context, or too far away | No task instruction |
| Prepare | Conservative lead estimate 22–40 seconds | One direction-specific mirror/signal/position planning cue |
| Await delivery | Submitted or started audio, not completed | No next teaching step |
| Observe | Completed audio or intentional visual-only acceptance | Observe fresh measured speed history after delivery recognition |
| Adapt approach | Lead 13–18 seconds, planning delivered, no observed slowing | At most one suitable-approach-speed/give-way preparation reminder |
| Quiet | Lead below 8 seconds | Suppress routine selections and cancel routine PCM; retain independently validated urgent warnings |
| Pause | Stale/missing evidence, braking or shock | Pause task instruction; cancel task PCM |
| End/restart | Source, route, event or reroute epoch changes; route loss | Old tickets invalid; outcome unassessed; new opportunity starts independently |

Lead is distance / measured speed, additionally capped by provider ETA for Google
navigation. These timing windows are experimental. The 13-second approach onset
threshold leaves room for the approximately 3.8-second recording before the quiet
boundary. Changed evidence can still cancel an in-progress recording.

Minimal/Balanced/Guided retain their existing 20/12/8-second routine intervals.
The lesson gets priority over routine announcements but does not bypass pacing.
If pacing or late arrival misses a step, skip it rather than compress the sequence.
Junction route announcements are replaced within this explicit pilot. Non-junction
map context and independent urgent evidence retain their existing paths.

## Evidence and delivery

Requires an active session, relevant route feature, stable current route/event
identity and reroute epoch, fresh original route data (Google <3 seconds, offline
<2.5 seconds), independent GPS <2.5 seconds, camera <0.35 seconds, and measured
telemetry <0.15 seconds. Speed must be finite and above 3 km/h. Upstream navigation
qualifies location evidence. Missing speed and stale camera do not become defaults.

Selection, audio submission, playback onset and completed playback are separate.
Submission refusal or interruption does not unlock the second step. Completed PCM
does not prove that the learner heard, understood or followed it. Intentional
visual-only acceptance supports sequencing but does not confirm that it was read.
The next step is revalidated at selection, acceptance and onset.

The post-delivery speed trend requires at least one second of continuous valid
samples from after delivery recognition, with the existing estimator's two km/h
change threshold and bounded history. It only suppresses a potentially redundant
reminder. It does not infer braking technique, causation, suitable speed or a
successful manoeuvre.

## Learning record

The phone saves at most 20 aggregate session records, showing the latest five:
lesson, duration, interrupted/ended status, opportunity count, planning/approach
deliveries and observed speed reductions. Session counter deltas avoid carrying
previous sessions into the record. Background interruption is recorded explicitly.
Records can be cleared in the parked records dialog.

No route coordinates, route identifiers, images, provider text, inferred controls,
skill grades or mastery claims are stored in these records. Mirror checks,
indicators, actual road position, gears/pedals and driving outcome remain unknown.
Native diagnostic JSON includes the same aggregate task state for sequence review.

## Reproduction and qualification

`task_coach_tests` covers direction, delivery stages, sequencing, pacing, response
timing, missing evidence, quiet execution, urgency, reroutes, source changes and
retries. `task_coach_scenarios` emits JSONL timelines for early/steady,
early/slowing, late arrival and location loss. These use supplied synthetic clocks
and measurements, with no instructor labels or pixel perception claims.

Run from the repository after configuring the existing native build:

```powershell
D:\bin\cmake.exe --build build/native-spatial --parallel 2
D:\bin\ctest.exe --test-dir build/native-spatial --output-on-failure
& build/native-spatial/task_coach_scenarios.exe > build/junction-task-timelines.jsonl
```

The Android `TaskCoachContractTest` and additional `AudioWorkerGateTest` case cover
JNI status, bounded records and routine cancellation preserving warnings. Building
their APK is not evidence of executing them on a phone. Full route/feed/audio,
lifecycle, sustained performance and instructor usefulness review remain open.
