# LearnDrive coaching: architecture review, direction and plan

2026-11-04. Branch `driving-coach-foundation`, reviewed at `bfdc26c` (0.7.1, code 32);
milestone below produces 0.7.2 (code 33). Host-only evidence: no phone, audio, thermal
or instructor run was performed for this review.

## 1. The real end-to-end path

```
CameraX frame ─► ONNX detector/road/supercombo ─► CameraCoach / DrivingCoach / DecisionEngine / DualPolicy
GPS+IMU (Sensors.kt) ─► ld_telemetry          │   (each picks ONE phrase id per tick)
Google/offline nav ─► NavigationContext ──────┤
                                              ▼
              ld_guidance_decide (core/src/c_api.cpp:446)  builds WorldState: ≤8 candidates
                                              ▼
CoachingEngine::decide: clocks → tracker → ShortMemory → EventLedger → TaskCoach
   → TemporalSceneInterpreter → SafetyValidator (per candidate) → DriveCoach::choose
   → pacing (8/12/20 s) → ticket
                                              ▼
NativeCore.kt accept → AudioWorker (onset gate, 10 Hz continuation gate) → Completed/Interrupted
                                              ▼
TaskCoach / DriveCoach aggregate counters → LearningRecordStore (≤20 sessions)
```

Key structural fact: the candidates are **already-chosen phrases**. Five producers
(`ld_decide`, dual policy, `CameraCoach`, `DrivingCoach`, navigation, c_api.cpp:461–503)
each select one prompt id with their own internal "spoken" state and pacing. The
coaching engine then validates and ranks those phrases. Only the junction task
(`TaskCoach`) generates candidates from a task model rather than from a phrase rule.

## 2. Candid assessment

**Genuinely implemented and solid**
- Evidence discipline: separate camera/telemetry/navigation clocks, per-candidate
  validation, original-clock onset, explicit unknowns, no fabricated control state
  (`SafetyValidator::validate`, CoachingEngine.cpp). This is the strongest part.
- Delivery lifecycle: selected → submitted → started → completed/interrupted, with
  pacing refunded on failure and completion counted once.
- One sequenced lesson (junction preparation) with delivery-gated step 2 and a
  measured-slowing response check.

**Still behaves like announcement rules**
- Road-user, bend and hill guidance: one phrase per detected condition, de-duplicated
  per identity. There is no preparation → response → adaptation sequence for them.
- `DriveCoach::rank` (DriveCoach.cpp:32) is a fixed integer priority table over phrase
  meanings. "Concurrent situations" means *at most one candidate per producer*: two
  pedestrians and a vehicle compete inside `CameraCoach` before the coordinator sees them.

**Where temporal memory actually changes decisions** (exhaustive, from code)
1. Ledger-based "already informed" suppression (DriveCoach.cpp:22, TemporalSceneInterpreter.cpp:172).
2. Novel-encounter deferral for one tick of continuity (TemporalSceneInterpreter.cpp:177).
3. Speed trend after delivery: silences speed-only cues (CoachingEngine.cpp:99) and the
   junction approach reminder (TaskCoach).
4. Continuation may bridge a ≤350 ms detector dropout.

`EncounterEpisode` phases, image deltas and most of `InterpretedScene` feed only the JSON
encoding (for the shadow model) and diagnostics. They do not influence selection.

**Measured defects in 0.7.1** (reproduced in `tests/teaching_interval_benchmark.cpp`,
synthetic intervals, voice playback simulated)

| Scenario | 0.7.1 behavior | Cause |
|---|---|---|
| Pedestrian presence cue 41 s before a junction, Minimal | Junction planning **and** approach never offered | Routine cue consumes the 20 s interval just before the known planning window |
| Planning starts at 24 s lead, any profile | Planning cut off mid-sentence at 22 s, so no lesson delivered | Continuation gate re-applies the *onset* window (22–40 s) during playback |
| Planning delivered at <25 s lead (Balanced) / <33 s (Minimal) | Approach reminder becomes impossible | The routine profile interval spaces lesson steps; profile silently decides lesson structure |
| GPS/route freezes at 6.5 s lead, pedestrian visible | Routine presence cue spoken inside the execution window (1.7 s of speech) | Lost lead evaluates as "not in execution", so evidence loss un-quiets |
| Route disappears at 5 s lead | Same: 2 routine cues inside execution | Same |

**Useful, vague, unsupported, late or redundant instructions**
- Unsupported: none found. The validator blocks control/permission content.
- Vague: "space" phrases for road users carry no task context (why it matters now).
- Late: see the truncation defect above. Bend cues are image-geometry based; their lead
  time is unknown (no metric distance), so timeliness cannot be claimed.
- Redundant: handled reasonably for the same identity. A tracker ID change after a
  >2 s gap still produces a "new" encounter.

**Missing useful instructions** (in order of value with current sensors)
Moving-off/stopping routines driven by measured speed transitions; speed-for-conditions
preparation before route bends (map curvature with measured speed); roundabout
approach/exit planning from route maneuver types; post-drive review of *which*
opportunities were taught vs missed; learner-stage support levels.

**Competition failures**
Fixed-rank arbitration has no notion of a known future task. Fixed in this milestone for
junctions. Single-candidate producers remain an unresolved limit. Already-informed
handling is correct only per identity.

**Stale data, interruptions, thermal reduction**
Freshness gates are conservative and correct, so slow inference produces silence, not
stale speech. Silence under perception loss is not surfaced to the learner as "coach
blind" beyond `CameraVisionHealth`. Thermal throttling has not been measured on 0.7.x.
It would lengthen frame intervals, so more decisions block on 350 ms camera freshness.
The effect on coaching coverage is unknown and needs a device run.

**Unnecessary complexity**
Three overlapping arbitration layers (producer-internal pacing, `TemporalPolicy` /
`BalancedPolicy`, `DriveCoach`) plus a separate legacy non-coaching branch in
`ld_guidance_decide` (c_api.cpp:520–560). The interpreter's JSON for a shadow model with
no authority, and the dual-brain/contextual engines, are maintained but have no live
authority over learner speech. Consolidate before adding more layers.

**What blocks dependable learner use**
No phone/audio validation of 0.7.x. No sustained thermal qualification. No instructor
review of wording, timing or omissions. Single forward camera coverage. Every
non-junction domain is still announcement-level.

**Solvable in software now vs needs evidence**
- Software now: task-aware scheduling (done here); a generic task-episode contract for
  bends, hills, stops and roundabouts using route geometry with measured speed; a
  multi-candidate camera producer; post-drive opportunity review; removing the legacy branch.
- Needs new evidence: lane position, mirror/blind-spot checks, indicator, gear/pedals
  (OBD on hold), right-of-way, rear/side coverage, metric distance to bends.
- Needs independent validation: teaching usefulness, distraction, timing windows
  (all constants are experimental), thermal/latency on S24 Ultra and a low-cost phone.

## 3. Architecture I would choose

Keep the native evidence/delivery core. Replace "phrase producers + rank table" with
**task episodes over a short horizon**, incrementally:

1. **Observation layer** (exists): qualified, timestamped measurements with explicit
   unknowns. Producers stop choosing phrases. They publish observations (actors, road
   shape, grade, route maneuvers).
2. **Task episodes** (TaskCoach is the template): each declares objective, prerequisites,
   step windows in lead time, response evidence, cancellation and completion limits.
   Several episodes can be live at once (junction ahead, bend now, pedestrian developing).
3. **Scheduler** (started in this milestone): owns the speech channel as a resource on a
   timeline. It knows each live episode's upcoming windows, reserves time for higher-value
   steps, spaces an episode's steps by the episode's own needs, holds execution quiet under
   evidence loss, and lets measured urgent conflicts preempt. Learner stage sets which steps
   an episode offers. Speech frequency only limits unrelated routine cues.
4. **Delivery** (exists): onset/continuation/interrupt with original clocks.
5. **Session evidence**: per-episode record of offered/delivered/missed/unassessed, not
   only aggregate counts.

**Pretrained model:** not on the decision path now. A small local model (the existing
SmolLM2/Qwen3 shadow work) earns a role only offline or post-drive. One example is
summarising the session record into learner-readable review text from structured
fields, with a native template fallback and no new claims. Online, it cannot add
observations, and latency/thermal cost on affordable phones is unqualified. Revisit only
if an interval benchmark shows a measurable selection improvement the scheduler cannot
reach.

## 4. Prioritized plan

| # | Milestone | Acceptance criteria |
|---|---|---|
| 1 | **Task-aware scheduling** (done on host, 0.7.2) | Interval benchmark: planning delivered in pre-window cases for all profiles; no routine speech inside junction execution under GPS/route loss; no lesson truncation; route-free output identical; recorded replay identical; all prior tests green |
| 2 | Phone qualification of 0.7.2 | S24 Ultra: onset/completion/interrupt traces for junction route drive; audible check; 30-min sustained run logging frame age, decision age, temperature, battery (charging state recorded) |
| 3 | Generic episode contract + route-bend and stop/move-off episodes | Same contract table as junction; benchmark intervals incl. missing data; no control-state claims |
| 4 | Consolidate arbitration | Producers emit observations; remove legacy non-coaching branch; one scheduler; replay identical or differences explained |
| 5 | Multi-actor camera candidates | Two concurrent road users both reach the scheduler; benchmark case for competition |
| 6 | Per-episode session review | Record lists offered/delivered/missed opportunities with reasons; no grading |
| 7 | Instructor full-interval review | Blinded review of benchmark + recorded intervals: necessity, timing, wording, omissions, silence; disagreement recorded |
| 8 | Learner-stage support levels | Stage changes which episode steps are offered, independent of speech frequency; instructor-confirmed |

Milestone 1 details: [../reviews/2026-11-04-task-aware-scheduling.md](../reviews/2026-11-04-task-aware-scheduling.md).
