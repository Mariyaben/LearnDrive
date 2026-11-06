# V2 baseline and comparison protocol

Base: `origin/master` at `aeb9fb8` (merged PR#1). Work branch:
`codex/temporal-pretrained-intelligence-v2`. The unrelated untracked
`qgis-accesslens/` directory is excluded from this work.

Before changing runtime code, the existing current-source native CTest run passed
13/13 groups and Python discovery passed 67/67 tests. Logs:
`build/intelligence-v2/baseline-ctest.txt` and `baseline-python.txt`.
The existing native structured bridge and its matching ORT DLL were copied to
`build/intelligence-v2/baseline-native/` before rebuilding. This immutable binary
will represent A (the real previous Balanced engine), rather than substituting
the narrower legacy audio gate.

Initial code inspection confirms:

- ShortMemory and namespaced track histories are bounded, but BalancedPolicy
  mainly ranks candidates, applies imminent-turn deferral and speed-trend silence.
- The earlier model runner sends a current WorldState rather than an interpreted
  event-evolution summary. It does not load a pretrained model itself.
- NativeCore accepts a visual/logical cue when voice is enabled and audio submit
  fails, acknowledging the encounter and consuming its routine interval. This
  must be separated from intentional muting.
- Original-capture validation, native monitor then audio monitor ordering,
  approved prerecorded phrases and independent validator must remain intact.
- The S24 Ultra is not presently listed by ADB. A previous test run had four
  immediate-start UI failures which passed on a focused rerun; this is recorded
  as a startup/flakiness concern, not a resolved product defect.

Comparison arms: A frozen Balanced; B improved native temporal interpretation;
C the same temporal context with actual locally executed pretrained proposals.
Use identical inputs and provenance, explicit event/response/silence reasons,
and independent safety validation. Synthetic expectations are software oracles,
not instructor labels. Instructor-rated nuisance/recall/silence metrics remain
unavailable until independently reviewed labels exist. The proposed25% nuisance
reduction is a research target, never an assumed result.

Pretrained weights will be publisher-provided, pinned and hash-verified. No
training, fine-tuning or paid inference is planned. Model decisions initially
remain shadow-only; deterministic urgent guidance never waits for inference.

## Implemented comparison

The new native policy is the default within the explicitly configured coaching
engine; legacy clients remain unchanged. Audio acceptance, real playback onset,
completion, interruption, expiry and intentional muting are separate. Voice-on
submission failure no longer acknowledges an encounter. A late completion cannot
resurrect a terminal failure. Source/settings cancellation keeps lock order core
then audio, and terminal callbacks run outside the AudioWorker monitor.

The old DLL SHA-256 is
`86ea9a32b5f45c7d2c8018463ea9b82b7eafa6cfa3e7cfa9d3dccf2062b325b0`.
All arms receive the same10Hz observation stream. PCM durations come from the
actual cached WAV files; submitted, onset and completion are simulated, never
claimed as heard. The frozen old engine has no completion hook and retains its
original submission acknowledgement behavior.

Three recorded development streams come from actual native video perception,
with original timestamps and unknown unavailable sensors. Their producer-side
candidate history is frozen and identical for all arms. These inputs isolate
coaching policy; they are not a full closed-loop or physical driving comparison.
The originating video DLL hash and the current comparison DLL hash are retained
separately. A final terminal-callback guard does not change perception snapshots.

Offline scenario time freezes during neural evaluation. Measured model wall time
and deadline misses are reported independently. This deliberately allows analysis
of a returned choice without pretending it could arrive on time during driving.
The Android path never waits for a learned decision or lets shadow speak.

Two test-harness defects were found and repaired: the stale-camera case initially
began with a fresh zero timestamp, and the runner initially invoked the model on
empty approved choice sets. Timeout failures now include measured elapsed time.
The interrupted `build/intelligence-v2/final-qwen/` diagnostic run is not final
evidence. Use `qualified-qwen` and `qualified-smol`, with tool/input/binary hashes
and raw failures retained. No failure is silently repaired into a valid proposal.

See [benchmark results](MODEL_BENCHMARK.md) and the
[quality report](COACHING_QUALITY_REPORT.md) for the distinction between engineering
corrections and unvalidated coaching judgments.
