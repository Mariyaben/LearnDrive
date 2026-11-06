# Running the pretrained integration

Run from the repository root with Python3.11+:

```powershell
python tools/fetch_intelligence_runtime.py llama-windows llama-android llama-headers qwen3-0.6b-q8 smollm2-360m-q8
```

This downloads publisher artifacts, verifies exact byte counts and SHA-256, checks
archive paths and extracts into ignored `.tools/intelligence`. No API key is needed.
Model files are large optional downloads. Provision before Gradle configuration.

Host single-summary inference:

```powershell
python tools/intelligence_model.py --server .tools/intelligence/windows/llama-server.exe --model .tools/intelligence/Qwen3-0.6B-Q8_0.gguf --context build/intelligence-v2/example-context.json --output build/intelligence-v2/example-proposal.json
```

The example context is generated from the real native replay stream by the
comparison, or can be one complete line from `temporal-contexts.jsonl`. It must
have schema_version2 and model_eligible=true. Google provider context is refused
before any request. The server is an owned local child process on127.0.0.1,
terminated on completion/error; no scene is uploaded. Use `--format json` only for
the slower strict-schema research path. Raw content, usage/timing, bindings and
validation failures are retained rather than silently rewritten.

The default network request is loopback HTTP. It uses a single slot, CPU-only,
two inference/prefill threads,4096-token context, deterministic greedy decoding,
Qwen thinking disabled, and an allowlisted one-letter grammar. Persistent server
prefix/KV caching may help host repeated prompts. Android currently clears its KV
between requests, so host warm timings cannot predict Android performance.

Android:

```powershell
cd android
.\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest
```

The ARM64 build links the pinned `libllama`, `libggml`, `libggml-base` and baseline
ARMv8.0 CPU backend. Other ABIs or absent provisioned runtime build a stub that
retains normal native coaching. No weights are bundled. On the phone, while
parked, copy a pinned GGUF to local storage, open More options → Import pretrained
GGUF model, and select it through Android's file picker. The app streams the file,
checks an explicit SHA allowlist and stores it privately. It requires at least
1.4GiB free storage and caps the import at700MiB.

Enable Experimental local AI shadow. Opt-in is session-scoped and off on a new
activity. Loading requires2GiB available RAM, no low-memory signal, Battery Saver
off and battery at least20%. Inference is paused at Android thermal status2 or higher,
with a5s abort budget and a2s result-admission budget. Three slow calls disable the
worker. Source/revision/generation changes, stale context or backgrounding discard
work. Turning it off, backgrounding and low-memory handling release loaded weights.

Inspect `adb logcat -s LearnDriveReasoning` for model_loaded and shadow_proposal.
Logs explicitly say live_authority=false and spoken=false. Normal guidance and
voice use only the native policy. The model's generated letter is mapped to an
approved instruction ID for diagnostics; it never reaches live audio.

No device was connected for V2 execution. The arm64/x86_64 builds compile and the
admission gates have host tests, but on-phone model loading, cancellation, latency,
memory coexistence and thermal behavior still need physical verification.
