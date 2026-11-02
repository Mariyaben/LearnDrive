# Driving-coach execution plan

2026-11-02. The target is the complete learner-driving curriculum in
[PRODUCT_SPECIFICATION.md](PRODUCT_SPECIFICATION.md). A lesson is a sequenced
teaching task, with evidence, response observation, adaptation and review.
Detection and map announcements alone do not meet the target.

## Milestones and exit conditions

| Order | Work | Concrete exit condition | Current status |
|---|---|---|---|
| 1 | Specify all curriculum domains; audit what the phone actually measures | Every domain has a capability gap; unknown actions remain explicit | Written |
| 2 | Integrate tasks into one automatic supervised drive | Context → native sequence → audio/visual delivery evidence → measured response → quiet execution → whole-drive record | Junction sequence and automatic arbitration for supported road-user/bend/hill cues implemented; host checks pass; phone/teaching gates open |
| 3 | Qualify that first lesson | Full-interval replay review, Android delivery/lifecycle checks, instructor assessment of omissions, timing and workload | Open; software checks alone do not close this |
| 4 | Resolve foundation defects | Semantic duplicate policy agrees with pacing; no stale instruction during slow inference; stable interruption/recovery; sustained phone measurements | Open; individual faults must have reproductions and acceptance evidence |
| 5 | Expand speed/space and bend/hill preparation tasks | Each has the same episode contract, response/unknown distinction and adverse-condition tests | Supported cues share automatic arbitration; richer teaching sequences remain open |
| 6 | Add moving-off/stopping and observation routines | Parked instruction plus properly scoped live opportunities; no inferred control use or permission to move | Not implemented |
| 7 | Add junction variants, roundabouts and position planning | Reliable relevant context and manoeuvre-specific review; ambiguous geometry/priority leads to silence or instructor handover | Not implemented |
| 8 | Add lane changes, overtaking, reversing, parking, difficult conditions and recovery | Surrounding evidence and teaching contract are sufficient for each enabled instruction | Not implemented; current forward phone view has coverage gaps |
| 9 | Add learner-stage adaptation | Instructor-confirmed goals and corrections; evidence-backed changes to teaching support, distinct from voice frequency | Only aggregate practice history exists |
| 10 | Qualify a constrained paid learner product | A published supported-use envelope, independent teaching review, sustained device results and supervised-use evidence | Open |

OBD remains on hold. Exact gear selection and control-specific corrections need
verified vehicle/control evidence and context. A general explanation in a parked
lesson can teach a sequence without claiming that the learner carried it out.
Adding an LLM does not supply missing observations.

## Repeatable lesson delivery process

1. Define the learner objective and a full scenario interval, including quiet time.
2. List observable context, its original timestamps, confidence and missing views.
3. Specify the instruction sequence, prerequisites and the last useful delivery time.
4. Name the observable response. Separate correlation from correct technique.
5. Define workload suppression, interruptions, source changes and recovery.
6. Review wording and sequence with a qualified instructor before enabling it for learners.
7. Implement one bounded native task; reuse the existing perception, ledger and delivery gates.
8. Run positive, negative, missing-data and interruption cases; record omissions as well as emitted instructions.
9. Test the Android delivery path and sustained performance before supervised evaluation.
10. Save only justified learning evidence; leave unmeasured actions and mastery unassessed.

## Evaluation beyond live physical testing

Use complementary tests. Ordinary videos cannot show which pedals, mirrors,
indicators or gears the recording driver used unless those are independently
recorded. Synthetic telemetry attached to a video is useful software input and
must remain labelled synthetic.

| Method | Establishes | Does not establish |
|---|---|---|
| Deterministic native scenario timelines | Sequence, deadlines, silence, evidence loss, response gating | Perception accuracy or teaching usefulness |
| Recorded frames with original capture timing | Detection/tracking errors, stale-result behavior, end-to-end omissions | Unrecorded driver controls or real GPS |
| Paired video + synchronized real telemetry | Whether observed speed/context supports the instruction | Whether the learner checked mirrors or chose a safe gap |
| Fault injection | Dropouts, reversed clocks, reroutes, audio refusal, interrupted delivery | Safe behavior in all untested situations |
| Blinded full-interval instructor review | Necessity, timing, wording, omitted teaching and appropriate silence | On-device heat, attention burden or long-term learning |
| Parked phone and simulator scenarios | UI/audio/lifecycle behavior and repeatable interactive tasks | Road readiness or a validated transfer of learning |

For each interval, reviewers record context, desired teaching task, earliest/latest
useful timing, instruction versus silence, actual output, omission, severity,
reason and disagreement. Keep the evaluation set separate from development cases.
Compare the current policy and task policy on the same intervals. Do not score
only emitted cues: silence can be a correct decision or an omission.

Report unsupported instructions, stale onsets, duplicate instructions, late/truncated
instructions, missed useful opportunities, urgent-warning preservation, uncertainty
handling and instructor agreement separately. Any unsupported control/permission
command or false mastery inference blocks the relevant lesson. Software invariants
are necessary; they are not a substitute for instructor usefulness assessment.

Device qualification must measure actual decision age, accepted/onset/completed
audio, camera stalls, battery and thermal behavior over sustained sessions on the
S24 Ultra / Android 16 and the intended lower-cost device class. Heat testing is
still deferred at the user's request. No performance improvement is claimed here.
