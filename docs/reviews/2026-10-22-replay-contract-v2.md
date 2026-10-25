# Replay evidence contract v2

This increment fixes fidelity and provenance defects identified in
`build/traffic-review-2026-10-22/HARNESS_AUDIT.md`. It does not make ordinary
downloaded video equivalent to an Android supervised drive.

## Changes

- The native bridge reports contract version 2. Rebuild `learndrive_replay`
  before using the updated reviewer. An old DLL produces an explicit upgrade
  error. Inference-only calls cannot publish a policy scene. A semantic-label
  update cannot republish an old or all-zero spatial tensor at a new timestamp.
- Policy replay publishes current semantic evidence before spatial evidence
  from the same capture. `publications.jsonl` records recording-relative capture,
  simulated completion and publication times. `--latency-ms` models semantic
  completion; `--spatial-extra-latency-ms` adds a separate spatial completion
  delay. These are explicit assumptions. The fixed-rate schedule does not model
  Android's serial worker, dropped frames, XNNPACK provider or thermal behavior.
- FFmpeg autorotation is disabled and rotated source streams fail with a
  normalization instruction. Packed YUV input is normalized to limited-range
  BT.601 to agree with the replay bridge's native pixel conversion. This can
  change inference relative to historical reports, which must remain historical.
- Schema-v2 caches include full video/model/DLL/source/header hashes, native
  runtime DLL hashes, Python/runtime/provider versions and binary hashes,
  FFmpeg binary/version and preprocessing settings. Arrays, semantic sample
  indices, finiteness, payload fingerprints and origin metadata are validated.
  An invalid matching cache fails; `--fresh-inference` explicitly regenerates it.
  Historical cache filenames are not eligible. Reports retain the cache's
  creation ID, UTC time and originating output folder when it is reused.
- The corpus runner permits verified current-provenance comparisons only from
  the same evidence directory, freezes its protocol, derives the reference app
  version from Gradle and labels a missing reference APK as unavailable. It
  fails if reviewed sources or its native binary change during evaluation.
- CSV speed uses only an already observed sample and expires at a configurable
  age (`--speed-max-age-seconds`, default 1.5 seconds). Gaps and time before the
  first sample produce unknown speed. Traces retain sample time, age and origin.
  Manual test speeds remain explicitly assumed. Original speed must never be
  inferred from the presence of a video file.
- The batch contract audit retains ordinary 3.5-second speech spacing and the
  10-second same-phrase lock. The fresh distinct-encounter exception requires the
  coordinator's actor/reference/source proof and a fresh scene; a changed actor
  ID alone is insufficient. This validates software provenance, not the quality
  of the real-world advice.

## Verification and remaining limits

Run `python -m unittest discover -s tests -p test_replay_contract.py -v` for the
focused regression suite. It uses synthetic arrays and a tiny local lossless
white-frame video, with no downloaded footage or model inference. Native bridge
build and production-video smoke checks must be recorded separately after the
shared C++ changes settle.

No recorded sensor sidecar is accepted yet. Calibrated intrinsics, original
pose/IMU and an aligned original speed clock require a validated recording
producer and replay consumer together. Ordinary MP4 replay remains semantic
only, or explicitly assumed-optics spatial sensitivity testing. It does not
test integrated navigation, real audio focus/onset, UI, admission or device heat.

## Persisted navigation diagnostics

`GuidanceTrace` now redacts the speed-limit value for every navigation provider
because the diagnostic array does not carry provider provenance. Availability
and timing remain available; live navigation/policy input is unchanged. No road
name is emitted by that format. Google explicitly disallows persisting SDK road
names/speed limits outside the user session in its
[Navigation SDK policy](https://developers.google.com/maps/documentation/navigation/android-sdk/policies#content-scraping).
`GuidanceTraceTest` checks serialization redaction, availability and unchanged
live input. Old diagnostic exports may still contain previously recorded values;
this change does not rewrite those historical files.
