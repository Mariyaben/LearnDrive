# Coaching usefulness: current evidence and limits

This is an engineering comparison, not evidence that either small language model
can coach a learner dependably. No qualified instructor labels are available. No
25% nuisance reduction, important-event recall, correct-silence rate, learner
outcome improvement or commercial readiness is claimed.

The software tests separate failures that can be checked objectively from judgments
about when a learner needs help:

| Question | Current evidence |
|---|---|
| Can failed voice submission count as delivered? | Regression rejects it and preserves fresh retry |
| Can queued/interrupted audio count as completed? | Delivery history records distinct stages; native response interpretation requires completion or intentional visual-only acceptance |
| Can repetition ticks manufacture actor history? | Distinct capture timestamps are required |
| Can a lost track establish a clear road? | No: lost and resolved are distinct states |
| Can the model invent wording, gear or an urgent command? | Constrained approved IDs and independent native evidence checks prevent promotion |
| Does a low cue count mean better coaching? | No; it can mean missed useful help or excessive silence |
| Is observed slowing proof of a correct response? | No; causal intent, controls and appropriateness remain unknown |

Twenty explicitly synthetic scenarios cover the requested categories: uncertain
pedestrian context, new relevance, addressed encounter, occlusion, left/right turn,
slowing/steady speed, competing events, normal silence, repeated detections,
reroute, missing GPS, stale camera, uncertain optical motion, stale/slow inference,
voice refusal/muting, urgent escalation and an inactive completed session. Their
titles state authored intent, not inferred ground truth. The slow-inference case
includes stale evidence; separate admission tests also reject late model results.
An inactive completed session intentionally generates no live neural instruction;
no learned post-drive analysis is implemented in this increment.

Three existing Indian traffic development clips additionally run actual detector
outputs through native tracking/coaching. They contain no original GPS, IMU or
calibrated optics; semantic replay leaves those unknown. Both models receive real
native temporal summaries. The matched A/B/C input stream freezes the original
candidate producer outputs, so it isolates policy behavior rather than proving a
closed-loop driving benefit. These clips were used during debugging and are not
an independent holdout. They have provisional research/noncommercial provenance;
commercial dataset rights are not established. No footage or raw recorded contexts
are redistributed in Git, and no model training uses them.

`benchmark_intelligence.py` writes shuffled `review-panels.json`, a blank
`blinded-review.csv`, and a separate `review-unblinding.json`. Keep the unblinding
file from reviewers. Supply the full evidence interval from `inputs.json`, not
just emitted-cue snippets. Review all silent intervals too. Existing strict
annotation support in `coaching_contract.py` requires reviewer identity, timing,
uncertainty, supported actions and provenance before labels count as reviewed.
Blank generated rows are never treated as instructor opinions.

Next quality gate: independently annotated, commercially permitted recordings
with held-out routes, vehicles and traffic conditions. Compare unnecessary routine
cues alongside missed important events, correct silence, timing, response-aware
redundancy and contradictions. Use paired exposure units and confidence intervals;
do not divide by spoken events alone. Disagreement requires adjudication. The
pretrained selector stays shadow-only until usefulness and device budgets both
pass. See [measured model results](MODEL_BENCHMARK.md) for observed behavior.
