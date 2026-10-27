# Release readiness

Status: **engineering candidate for further evaluation; not a paid learner launch**.
No new model training. Qwen3-0.6B and SmolLM2-360M execute locally in the host
evaluation and connect to actual temporal contexts and the bounded hybrid policy.
Android has a compiled optional local shadow path, not verified on-phone execution.

Implemented: temporal interpretation and meaningful-change cache invalidation;
semantic delivered-cue suppression; voice refusal/retry and terminal delivery
tracking; actual pretrained loading/decoding and strict ID binding; independent
native validation and urgent fallback; optional Android background load/import,
thermal/resource admission and lifecycle cancellation; matched A/B/C comparison,
recorded perception snapshots, provenance and blank blinded-review support.
Existing dashboard, navigation, detector, Supercombo and prerecorded voice remain.

Validation evidence:14 native CTest groups including the real Supercombo smoke,
Python contract/replay tests, Java policy tests and both Android APK builds. The
machine-readable [engineering results](evidence/engineering-results.json) records
exact final counts, binary hashes and observed model timings. Phone instrumentation
is compiled but not run in V2; audio audibility from the earlier version is not
claimed as a V2 model test.

Open release gates:

- Independently reviewed usefulness, important-event coverage and silence on
  commercially permitted held-out traffic. No validated quality improvement yet.
- Android ARM64 model execution, coexistence with camera/navigation, memory,
  cancellation, audibility, lifecycle/start reliability and sustained heat/battery.
- Wider device/vehicle/mount/environment qualification and instructor-supervised
  road validation. The existing S24 is only one intended device profile.
- Product/legal/data rights, crash recovery, store/release signing, privacy/security
  review and existing component-license obligations before distribution.

Do not promote a model because it emits valid JSON or fewer cues. Keep core native
fallback independent, pretrained shadow off by default, no generated live voice,
no unsupported mechanical instructions and critical command safeguards. If the
learned policy fails quality or latency gates, retain the reproducible experiment
and ship neither its live authority nor claims of human-instructor equivalence.
