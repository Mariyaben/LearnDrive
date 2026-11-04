# 2026-11-04: Task-aware scheduling (0.7.2, code 33)

Branch `driving-coach-foundation`, on top of `bfdc26c`. Host evidence only.

## Learner problem

In 0.7.1 the coordinator ranked whatever phrases appeared *this tick*. It did not use
what it already knew about the junction ahead. As a result:
- a low-value presence cue just before the planning window could remove the whole
  junction lesson (Minimal);
- the routine profile interval decided whether the approach step could exist at all;
- a planning cue started late in its window was cut off mid-sentence;
- losing GPS/route evidence in the last 8 s re-enabled routine chatter during execution.

## Behavior changed (automatic coaching only)

1. **Reservation.** A non-urgent routine cue that is not a developing closing conflict is
   withheld when accepting it would leave the next pending junction step less than 3 s
   of its window. The reason code is `RESERVED_FOR_UPCOMING_TASK` and the count is
   `reserved_for_task`. The cue stays a candidate. Urgent warnings and closing conflicts
   are never reserved away.
2. **Episode spacing.** The approach step follows its own planning step after
   min(profile interval, 8 s). Unrelated routine cues still use 8/12/20 s. This revises
   the 0.7.0 rule that lesson steps obey the routine interval.
3. **No truncation.** Started lesson speech continues while the same episode has fresh
   evidence and the lead time is at least 8 s. The onset window is applied only at
   selection and onset.
4. **Execution quiet hold.** Once measured lead time falls below 8 s, routine cues stay
   quiet for up to 10 s through route/GPS/camera loss. The hold ends when fresh
   navigation shows a different event, measured lead rises above 8 s, or the session ends.
   Urgent warnings are unaffected. The phase shows `EXECUTION_QUIET` and the
   `execution_quiet_holds` count is recorded.

`CoachingEngine::enableTaskScheduling(false)` reproduces the 0.7.1 scheduling policy for
comparison. It does not undo the two perception-plumbing fixes below, which apply to both modes.

## Defects found by real-video replay (fixed)

Real detector output on local clips, replayed through the shared core with continuation
checks every 10 ms (as AudioWorker does at 10 Hz), exposed defects the synthetic
intervals could not:

5. **Capture-time race (both modes).** Between frame publication and the next rule
   tick, `coachingCurrent` stamped the camera coach's previous-frame attention box
   with the new frame's clock. It then looked for a matching detection among the new
   frame's boxes and, finding none, set the actor's confidence to 0. Started speech
   was cut off after 5–94% of the phrase. The actor now carries its own capture time
   and detector score (c_api.cpp `coachingCurrent`). Regression test:
   `coaching_engine_tests` "speech continuation never pairs an old attention box…".
6. **Presence vs TTI confidence (both modes).** Actor confidence was
   min(detector score, optical-TTI confidence) whenever a TTI existed, so a weak TTI
   estimate erased a confidently detected road user. The TTI confidence now only
   qualifies a closing claim. Urgent requirements are unchanged, because they already
   require TTI confidence ≥ 0.65.
7. **Routine continuation through detector dropout (scheduling on).** Routine speech
   that was validated at onset may finish while its own actor was seen as relevant
   within the last 1.0 s. That is the tracker's 0.75 s re-association window plus one
   frame. A visible but no-longer-relevant actor, braking/shock, task or drive context
   loss, or a source/settings change still interrupts. Urgent speech keeps the 350 ms
   rule. Interrupted routine cues refund pacing and are retried, so truncation had
   produced half-phrases followed by repeats.

`ld_coaching_continuation_reason` (also in replay traces) now reports why speech stopped.

## Code

`TaskCoach.hpp/.cpp` (window constants, `reserves`, `continuing`, quiet hold),
`CoachingEngine.cpp` (reservation, episode spacing, continuation uses `started` evidence),
`DriveCoach.cpp` (shared `developingConflict`, held-quiet phase),
`tests/teaching_interval_benchmark.cpp` (new), `tests/drive_coach_tests.cpp` (+3 groups,
1 updated), `CMakeLists.txt`, version 0.7.2/33.

## Evidence

- `teaching_interval_benchmark`: 11 synthetic full intervals × 3 profiles × 2 policies.
  Every tick is evaluated. Simulated voice: 3.0/3.8 s clips, a busy worker refuses
  routine cues, urgent cues preempt. JSONL via
  `build/native-spatial/teaching_interval_benchmark.exe build/teaching-intervals.jsonl`.
  Delivered (completed) steps, old → new, on the same intervals:
  - Presence cue 41 s before the junction, Minimal: planning 0→1, approach 0→1. Cost: that presence cue 1→0.
  - Planning starting at 24 s lead, all profiles: planning 0→1 (0.7.1 truncated it at 22 s), approach 0→1.
  - Road user persisting between steps: Balanced approach 0→1, cost presence 1→0. Guided approach 0→1 with presence kept (0.7.1 truncated the approach at 13 s).
  - GPS freeze or route loss inside execution: routine speech in execution 1.7 s→0 s (1–2 cues → 0). Hill support resumes after the junction or after the 10 s hold.
  - Minimal, any junction with planning before 33 s lead: approach 0→1 (episode spacing).
  - **Pure costs observed:** pre-window bend (Minimal) and a presence cue between steps
    that its own evidence ends early (Balanced). There 0.7.1 still delivered the lesson,
    because the interrupted routine cue refunded its pacing, so the reserved cue
    (1→0) bought nothing.
  - Unchanged: urgent warning in execution, closing conflict before the window, approach suppressed after measured slowing, route-free mix (identical output).
- CTest 20/20 (includes the new group). The automatic-drive suite has 22 groups, including the existing 90-case matrix and 3 continuous drives.
- Recorded-perception replay (`tools/evaluate_automatic_drive.py`, gp2/gp4/gp9): 18
  configurations, 4,734 decisions, identical to 0.7.1. These captures have no route
  context, so the scheduler is not exercised there. This is regression evidence only.
- Python: 79 tests OK (`python -m unittest discover -s tests`).
- Allocation benchmarks (`*_bounded_allocations`) pass.

## Not established

Teaching value of the trade (a presence cue given up for a junction step) needs
instructor review. The timing constants (3 s minimum window, 8 s spacing, 10 s hold)
are experimental. Reservation assumes the current measured speed continues. There has
been no phone, audible, lifecycle or thermal run of 0.7.2. Multi-actor competition
inside `CameraCoach` is unchanged.

## Next step

Phone qualification of 0.7.2 on the S24 Ultra with a junction route drive (onset,
completion and interrupt traces, audible check), then the generic episode contract
for route bends and stop/move-off.
