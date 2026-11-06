# Measured pretrained model comparison

2026-10-27, candidate0.6.0. **Both models actually execute. Neither is qualified
for live coaching.** These are Windows CPU measurements, not Android estimates.
The smaller SmolLM2 is the first optional device-profiling candidate because of
its footprint; this is not a finding of superior coaching usefulness.

| Checkpoint, Q8_0 | Cold server load | Request p50 | Request p95 | Peak resident | Returned valid / calls | Over2s |
|---|---:|---:|---:|---:|---:|---:|
| SmolLM2-360M-Instruct |4.79s|1.70s|7.74s|650.5MiB|50/52|19/52|
| Qwen3-0.6B |21.25s|2.36s|8.01s|1106.5MiB|47/52|30/52|

Request percentiles include measured failures. The HTTP request limit is8s;
maximum observed request elapsed times were8.03s and8.09s, including timeout/OS
overhead. These are not uncensored worst-case model-completion times. Failed
requests remain failures, and the native policy continues. The host had memory
pressure, runs are sequential and persistent llama-server prefix caching is active.
Do not infer comparative phone speed or causally attribute every difference to
model architecture. Actual process counters, raw timing and provenance are in the
[machine-readable aggregate](evidence/engineering-results.json).

Each checkpoint evaluated23 matched scenarios:20 explicitly synthetic cases and
three frozen real native video-perception streams. It made43 calls on synthetic
contexts and9 on recorded contexts. Recorded SmolLM2 calls returned7 valid choices
and2 timeouts; Qwen returned4 valid choices and5 timeouts. All18 recorded requests
across both models exceeded2s. No model processes raw camera frames or uploads
scenes. Google provider content is excluded.

There is a substantial **decision-bias problem**. Qwen chose SILENCE for every
returned proposal in this corrected run. SmolLM2 chose DEFER or the first approved
candidate; its returned recorded proposals all selected that candidate. Valid IDs
therefore do not establish contextual discrimination. The opposite biases and
latency failures weigh against promotion. No instructor labels exist to establish
which choices were useful. The models have not demonstrated a benefit over B.

| Emitted logical events | A previous Balanced | B native temporal | C learned hybrid |
|---|---:|---:|---:|
|20 synthetic scenarios, either checkpoint run|16|15|15|
|3 sampled recorded streams, either checkpoint run|5|5|5|

The synthetic difference is the repaired voice-submission refusal: A acknowledges
one unavailable voice cue, B/C do not. It is an audio-accounting correction, not a
learned nuisance reduction. Important escalation produces2 events in all arms;
stale-camera and missing-GPS scenarios produce0. Hybrid fallbacks and scene-change
invalidation contribute to C; matching counts must not be credited to the model.
Counts alone cannot score usefulness, silence, timeliness or recall.

The original full100Hz video replay produces6 simulated cues over78s/2340frames.
The frozen policy comparison samples producer WorldStates at10Hz and can miss
short candidate windows; it produces5. All arms receive identical sampled inputs.
Producer history is frozen, while each arm's delivery ledger evolves separately.
These are development clips with research-only/unresolved commercial data rights,
not an independent holdout. Raw footage and recorded contexts stay outside Git.

Offline logical time freezes during inference so a returned choice can be examined.
Wall time and deadline misses are retained. This is a counterfactual usefulness
experiment, not a live scheduling success. Android uses an independent native
coach and a separate optional shadow worker; model output never speaks there.

## Reproduction

Provision the pinned publisher artifacts using
[integration instructions](PRETRAINED_MODEL_INTEGRATION.md). Build the native
replay targets with the existing ORT SDK and CMake configuration. Preserve a
Balanced DLL built from `aeb9fb8` in
`build/intelligence-v2/baseline-native/`; the original audit preserved it before
changing code. Its exact SHA-256 is recorded in the aggregate. On a clean checkout,
build that revision in a separate checkout using the same native toolchain;
do not substitute the legacy audio gate for arm A.

Example real-perception export, with unavailable speed/IMU left unknown:

```powershell
python tools/review_video.py path/to/permitted-traffic.mp4 --mode semantic --coaching-profile balanced --save-temporal-context --latency-ms 224 --output build/intelligence-v2/video/example
```

Then compare actual pretrained inference:

```powershell
python tools/benchmark_intelligence.py --server .tools/intelligence/windows/llama-server.exe --model .tools/intelligence/smollm2-360m-instruct-q8_0.gguf --output build/intelligence-v2/qualified-smol --recorded-worlds build/intelligence-v2/video/gp2/temporal-worlds.json build/intelligence-v2/video/gp4/temporal-worlds.json build/intelligence-v2/video/gp9/temporal-worlds.json
python tools/benchmark_intelligence.py --server .tools/intelligence/windows/llama-server.exe --model .tools/intelligence/Qwen3-0.6B-Q8_0.gguf --output build/intelligence-v2/qualified-qwen --recorded-worlds build/intelligence-v2/video/gp2/temporal-worlds.json build/intelligence-v2/video/gp4/temporal-worlds.json build/intelligence-v2/video/gp9/temporal-worlds.json
python tools/summarize_intelligence_results.py build/intelligence-v2/qualified-smol build/intelligence-v2/qualified-qwen
```

The historical directory prefix `qualified-` names completed engineering runs,
not quality qualification. Use your own permitted streams on another machine.
Omit `--recorded-worlds` for the20 standalone synthetic tests. Each folder retains
inputs, per-case ticks, raw model content, native proposal admission, failures,
server logs, input/tool/prompt/model/library SHA-256, usage and process counters.
The aggregate publishes only counts and actual synthetic neural examples.

Compact real output examples are `D`→DEFER, `A`→the current allowlisted candidate,
and `S`→SILENCE. ID, reason and event metadata are deterministic trusted bindings;
they are not generated explanations. The earlier14s full-JSON Qwen attempt had an
unsupported workload reason and does not count as a validated success.

The benchmark writes shuffled reviewer panels and blank annotations, with a
separate unblinding map. No generated annotation is labelled reviewed. See
[quality evaluation limits](COACHING_QUALITY_REPORT.md). More useful next research
is task discrimination on independently labelled intervals, leaner context and
stationary Android coexistence profiling; promoting either current model is not
supported by this evidence.
