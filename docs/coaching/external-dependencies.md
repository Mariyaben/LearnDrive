# External dependencies — verified 2026-10-26

This is a read-only engineering review of primary publisher documents. No Google Cloud project/billing changes, paid API calls, model downloads or source uploads were performed. Published rules and prices can change; the account's agreement and eligibility still govern deployment.

## Google Navigation and retained content

The Android Navigation SDK targets commercial mobile turn-by-turn use. Heavy/large-vehicle navigation has additional consent/service constraints. The policy prohibits retaining road names or speed limits for another context outside the user session. First use must show the SDK terms and independent-judgment/cost disclosures. Keep supplied attribution visible and unmodified; distinguish Google Maps content from application content, and preserve visibility of navigation elements. Current Google map attribution already visible on the map does not need duplication. Coaching overlays must avoid obscuring required navigation information. [Google Navigation SDK policies and attribution](https://developers.google.com/maps/documentation/navigation/android-sdk/policies).

The general terms restrict caching except where service-specific permissions apply, and prohibit using Google Maps content to improve ML/AI models, including model training, testing, validation and fine-tuning. This rules out feeding retained Google route/road content into this project's policy benchmark under the reviewed general terms. The structured fixtures are independently authored synthetic values and contain no copied provider content. This is an engineering implementation boundary; signed exceptions or different applicable terms require separate review. [Google Maps Platform Terms of Service, section 3.2.3](https://cloud.google.com/maps-platform/terms).

Production deterministic route-aware coaching consumes transient, in-memory normalized context to gate existing approved cues. This is distinct from retaining Google-derived context to train or evaluate a learned model. The model-improvement restriction above does not by itself resolve every allowed product use; this task has not performed contract/legal review or audited product-wide compliance. Keeping data local does not remove contractual restrictions.

The new native coaching diagnostics add application event IDs, settings, counters, original evidence age and motion trend; they contain no road name or raw route array. A code check also found an existing, separate `GuidanceTrace.navigationFields` object retaining normalized route/feature IDs plus distances, progress, maneuver and GPS-quality scalars, with speed-limit value redacted. Therefore “diagnostics contain event IDs only” is not accurate for that existing navigation object. Google-backed retained traces must suppress provider-derived route details independently of transient runtime context; this task's model runner never imports that export format.

## Billing snapshot

Route calculation bills each requested destination/waypoint under the Navigation Request SKU. Starting guidance or rerouting for an already fetched destination does not add another such charge; billing must be enabled. Customer agreements can change this treatment. [Navigation Android SDK usage and billing](https://developers.google.com/maps/documentation/navigation/android-sdk/pricing).

Published pay-as-you-go Navigation Request prices are USD per 1,000 billable events:

| Schedule | Monthly free cap | First listed paid tier | Following tiers |
| --- | ---: | ---: | --- |
| Global | 1,000 | $25 through 100,000 monthly events | $20 through 500,000; $15 through 1 million; $8 through 5 million; $2 thereafter |
| India | 7,000 | $8 through 5 million monthly events | $2 thereafter |

The global and India schedules are distinct; location or currency does not itself establish the project's India eligibility. Other Places/Routes/Maps requests may use separate SKUs. Taxes and contractual discounts are not calculated here. [Google global pricing table](https://developers.google.com/maps/billing-and-pricing/pricing), [Google India pricing table](https://developers.google.com/maps/billing-and-pricing/pricing-india).

Google's current eligibility page requires India billing and a large majority of in-region usage in India; it monitors eligibility. No billing-account details were inspected to determine whether this project qualifies. [Google India pricing eligibility](https://developers.google.com/maps/billing-and-pricing/india-overview).

## Small pretrained reasoning candidates for future local shadow evaluation

Both candidates below are Qwen-published causal text models with optional thinking/non-thinking modes and Apache-2.0 model-card licenses. The proposed use is a bounded JSON hypothesis selector in shadow, never real-time speech authority. Neither model is installed or evaluated by this task. General reasoning benchmarks do not establish driving-coaching quality.

| Candidate | Publisher interface/license | Publisher-listed artifacts | Engineering comparison |
| --- | --- | --- | --- |
| Qwen3-0.6B | 0.6B nominal parameters; 32,768 context; thinking switch; Apache-2.0 | Safetensors weight about 1.5 GB; official Q8_0 GGUF 639 MB | Smaller starting point for local schema adherence and deadline tests |
| Qwen3-1.7B | 1.7B nominal parameters; 32,768 context; thinking switch; Apache-2.0 | Safetensors shards about 4.06 GB total; official Q8_0 GGUF 1.83 GB | Larger comparison point with greater storage/memory pressure |

These are actual publisher file-list sizes, not runtime RAM estimates. License notices must be retained when redistributing; quantized conversions/runtime dependencies also need provenance and their own license review. [Qwen3-0.6B model card](https://huggingface.co/Qwen/Qwen3-0.6B), [0.6B Safetensors listing](https://huggingface.co/Qwen/Qwen3-0.6B/tree/main), [0.6B official GGUF listing](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/tree/main), [Qwen3-1.7B model card](https://huggingface.co/Qwen/Qwen3-1.7B), [1.7B Safetensors listing](https://huggingface.co/Qwen/Qwen3-1.7B/tree/main), [1.7B official GGUF listing](https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/tree/main).

The model cards require a compatible runtime; their Transformers examples warn that versions below 4.51.0 do not recognize Qwen3. The inspected local Python environment has Transformers 4.46.1, so it cannot be presumed ready. No environment upgrade was made. Thinking tokens, tokenizer/chat template, generation/output limit, quantization and runtime/provider must be pinned separately. Start with non-thinking constrained JSON, and compare a separately budgeted thinking setting if justified; no chain-of-thought is needed in the retained proposal.

Samsung's India Enterprise Edition SM-S921BZKCW01 listing specifies 8 GB memory and Exynos 2400. This is one documented S24 variant, not evidence about the user's phone. Even a 639 MB weight file needs additional KV cache, activations, tokenizer/runtime buffers and OS/app memory while camera, maps, detector and Supercombo compete for resources. Fitting weights into nominal RAM does not establish a 150 ms response, stable frame rate or acceptable heat. These are engineering inferences, not measured S24 results. [Samsung official S24 variant specifications](https://www.samsung.com/in/business/smartphones/galaxy-s/galaxy-s24-sm-s921bzkcw01/).

## Evaluation boundary and unavailable evidence

The implemented `benchmark_coaching_shadow.py` requires existing local adapter/model artifacts and pins their hashes. Its admission contract permits only independent project-authored synthetic fixtures with `google_maps_content:false`; it explicitly rejects Google/Android exports and all unverified recorded content before model execution. A future consent/licence/source validator is required before admitting original or independently licensed recorded data. No benchmark in this increment ingests Google exports. These checks enforce the declared input contract; they cannot authenticate a dishonest provenance declaration.

The runner records sampled wall time including process startup, timeout/output failures, optional sampled RSS and independent native validator outcomes. Its default timeout is an experimental 150 ms test budget, not a performance claim. A bounded candidate index or silence/defer proposal passes the same native validator; it never enters logical acceptance or PCM playback. The deterministic baseline receives the same pinned structured inputs.

Currently unavailable: actual pretrained-policy schema success, driving usefulness, missed events, nuisance rate, comparative accuracy, runtime peak memory, sustained S24 latency/frame rate/power/thermal behavior and instructor-labelled quality. The existing seventy cue-review rows remain blank. Missing model/labels are reported as unavailable; no synthetic adapter output counts as pretrained reasoning, and no superiority is claimed. See [structured replay protocol](STRUCTURED_REPLAY.md) for scope and local commands.
