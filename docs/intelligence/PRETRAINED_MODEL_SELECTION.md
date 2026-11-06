# Pretrained model selection

No training or fine-tuning. Both evaluated checkpoints are publisher-provided
GGUF Q8_0 instruction models with embedded tokenizers.

| Model | Download bytes | Source revision | License |
|---|---:|---|---|
| Qwen3-0.6B Q8_0 |639446688|23749fefcc72300e3a2ad315e1317431b06b590a|Apache-2.0|
| SmolLM2-360M-Instruct Q8_0 |386404992|593b5a2e04c8f3e4ee880263f93e0bd2901ad47f|Apache-2.0|

Weight SHA-256:

```
Qwen3: 9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031
SmolLM2: 48ab3034d0dd401fbc721eb1df3217902fee7dab9078992d66431f09b7750201
```

Qwen is the larger reasoning experiment, with thinking disabled to bound output.
SmolLM2 is the smaller device-feasibility candidate. Neither is trained or certified
as a driving coach. Smaller size alone establishes neither usefulness nor phone
compatibility. Current results do not justify promoting either to live authority.
Use SmolLM2 first for optional stationary device profiling because of its smaller
weight footprint; retain Qwen as the comparison, not as an assumed quality winner.

Runtime: official llama.cpp nightly `b11932`, source
`0c8c59ef3d1b7a5c073f3cd1c2fe28d4fa960c89`. The stable0.7.0 release inspected during
selection lacked the needed binary assets. Matching pinned headers are downloaded
and individually SHA-verified. GGUF includes model-specific tokenization; existing
Supercombo ORT is not repurposed to run a language model.

| Runtime archive | SHA-256 |
|---|---|
| Windows CPU x64 |1b57df69f58dc1bfab83c4743a443b1c4973e7f83283ac7a39239e496815d0a3|
| Android ARM64 |bd3763d6b8fb85de145302fa50d66b4d1877346f5d4c9727567c6c4e925cd57d|

llama.cpp is MIT licensed; its notice is included in the APK. Weights and downloaded
binaries remain outside Git. Model download is optional, separate from the APK.
No paid inference service or cloud upload is introduced.

The first unconstrained-schema Qwen execution mixed SILENCE with an instruction
and was rejected. A tightened full-JSON experiment took about14s and supplied an
unsupported workload explanation; retrospective validation rejects that explanation.
The compact constrained neural-choice format removes arbitrary generated wording
and substantially bounds output. It does not prove decision usefulness. Lower-bit
quantization and accelerators remain future experiments requiring repeat evaluation.

Primary references: [Qwen official GGUF](https://huggingface.co/Qwen/Qwen3-0.6B-GGUF),
[Qwen model card](https://huggingface.co/Qwen/Qwen3-0.6B),
[SmolLM2 official GGUF](https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct-GGUF),
[llama.cpp Android documentation](https://github.com/ggml-org/llama.cpp/blob/0c8c59ef3d1b7a5c073f3cd1c2fe28d4fa960c89/docs/android.md).
