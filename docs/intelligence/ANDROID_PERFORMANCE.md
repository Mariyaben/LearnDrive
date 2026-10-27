# Android and host performance boundaries

0.6.0 application and instrumentation APKs assemble for ARM64 and x86_64. The ARM64
artifact contains a real pinned llama.cpp runtime; x86_64 retains the stub and
normal native coaching. No GGUF weights are inside the APK. Imported weights are
verified by exact SHA-256, not a filename. The embedded tokenizer is used.

This V2 run has no connected ADB device. No phone model load/decode, live shadow
result, coexistence measurement, Android memory peak, sustained battery use or
thermal result is available. Compile success and host Java gates do not substitute
for these checks. Prior S24 Ultra/Android16 app checks predate this inference path.
Earlier startup tests also had focused-rerun successes after initial failures;
that flakiness was not diagnosed in this increment. Heat qualification remains open.

The Android worker uses two CPU threads, background priority,4096-token context,
512-token prefill batches/128 microbatches and at most4 output tokens. Model input
is bounded to20000bytes and3800tokens; output is an allowlisted single letter.
It has no camera-frame FIFO, uses changed-context triggers, throttles to1.5s and
recaptures native context after a long load. A result needs unchanged source,
revision and generation, a latest-context age below150ms and inference within2s.
The runtime abort budget is5s; an over-budget result has no authority. Three slow
calls disable shadow. Missing model/runtime, Battery Saver, low battery, insufficient
RAM, warm thermal state or errors leave the native coach available.

Turning shadow off and backgrounding release weights on the inference worker.
Source/settings changes cancel and invalidate results. The model cannot cancel or
delay urgent native speech. Even shadow inference can compete for CPU/memory,
so enabling it while driving is not performance-qualified. Android clears model KV
per request; desktop llama-server prefix caching makes repeated host calls a
different workload. Native cancellation is cooperative, not a real-time guarantee.

Host semantic replays processed2340 frames across78s of video. Measured detector
p95 ranged24.70–59.88ms across these runs; the latter overlapped the APK build and
is not an isolated detector comparison. Simulated224ms publication delay is a test
assumption, not phone inference speed. Supercombo real-model smoke and bounded
native coaching tests pass, but no end-to-end Android speed is inferred from them.

For device qualification, use the pinned smaller SmolLM2 first while parked:
record app/native PSS, camera stage p50/p95 and drop counts, model cold load and
prefill/decode time, cancellation, lost focus/background/source changes, memory
pressure and thermal/battery state. Compare model-off and shadow-on runs after
cooling, unplugged, Battery Saver off, same mount/scene and navigation workload.
Keep charging heat out of the comparison. Then test sustained coexistence and
lower-memory phones. Do not remove safeguards to obtain a better benchmark.

The [benchmark](MODEL_BENCHMARK.md) reports actual host model wall time, process
resident/peak memory, CPU counters, prefix-cache timings and budget misses. Phone
performance fields remain explicitly unavailable.
