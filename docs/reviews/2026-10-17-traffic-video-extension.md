# Traffic video evaluation — 17 October 2026

Video replay is useful for finding concrete failures before supervised road
evaluation. This run completes **nine replay configurations across five source
clips**, with 8,954 analyzed frame steps. Software trace/audio contract checks
pass. **Guidance quality remains unaccepted**: false obstacle cues, opposing
traffic relevance, speech density and limited semantic freshness remain open.
No application code, model weights, freshness threshold or APK changed.

## Review artifacts

- [Annotated Bangalore video with scheduled LearnDrive speech](../../build/public-traffic-extension/runs/bangalore-semantic-manual30-delay224/guidance-review.mp4).
- [Bangalore cue capture/context frames](../../build/public-traffic-extension/bangalore-cue-context.jpg).
- [Reproduced false/irrelevant cue frames](../../build/public-traffic-extension/regression-cue-contact.jpg).
- [Machine-readable aggregate and audit results](../../build/public-traffic-extension/aggregate.json).
- [Protocol and source identity](../../scenarios/public-traffic-extension/protocol.json),
  [structured visual findings](../../scenarios/public-traffic-extension/findings.json).

Each run directory contains `report.json`, `observations.json`, `decisions.jsonl`,
`guidance.csv` and a `command.json` argument list for reproduction. The annotated
video uses actual bundled clips scheduled at native accepted-submission times;
it is a synthetic review track, not a recording of physical phone audio.

## Sources and test conditions

New source: [Driving in Bangalore, C V Raman Road](https://commons.wikimedia.org/wiki/File:Bangalore_20220814_154815.webm),
2022, L. Shyamal, published under CC0 1.0. Downloaded original: 150,326,691 bytes;
published SHA-1 verifies. SHA-256:
`aefe85da6cf800b989f975231e37b9148c4ad19d4d466630a0dff1be79adb79d`.
Full derived clip: 48.3516 seconds, resized to 640x360, audio removed, H.264 CRF18;
SHA-256 `896405922e219e83b330db6dc5837151771fd8562e233076383829819ac3f6be`.
Source contact frames were inspected and the initial protocol saved before
running inference on this clip. It is now development data, not a future held-out
test. No tuning was performed after viewing the results.

The four existing development clips are [IDS-JODHA clear/rain previews](https://shaaashvat.github.io/ids-jodha/)
and [Udacity highway/winding-road clips](https://github.com/udacity/CarND-Advanced-Lane-Lines).
Their source identities remain in [the existing manifest](../../scenarios/public-traffic/sources.json).
Prior evidence is preserved; these new runs use the current rebuilt native DLL.
Model inference is reused from matching caches where available; native policy
decisions and acknowledgement audits execute again. New Bangalore semantic and
spatial model outputs are inferred for this extension.

The shared desktop C++ replay is explicitly active and bypasses Android's setup
UI. Semantic-only runs preserve unknown original optics and motion. The 30 km/h
setting is an **assumed input**, not the video's measured speed. Unknown-speed
control preserves null speed. Six seconds of startup pre-roll precede each video.
Emergency commands remain off. A fixed 224 ms result delay is informed by the
first phone capture's median; it does not simulate full Android scheduling or
establish active-session phone latency. This delay differs from the prior
180 ms development baseline, so these are delay-sensitivity regressions.

Native replay DLL SHA-256:
`944797b7bfedb874076fb55605688afc09cea0d7d4f5a9854127efd92d8127fb`.
Per-run reports preserve tool/native/policy hashes; the aggregate includes model
hashes. `cmake --build build/native-spatial --target learndrive_replay --parallel 2`
reported no work to do before these runs.

## Results

Availability below is the fraction of native decision ticks with fresh semantic
evidence, excluding startup and the first 350 ms of video. It is **not detection
accuracy or hazard recall**. Speech occupancy uses scheduled clip durations,
interruptions and truncation at the video end; it is not measured distraction.

| Clip / configuration | Frame steps | Cues during video | Speech occupancy | Semantic availability |
| --- | ---: | ---: | ---: | ---: |
| Bangalore, semantic, unknown speed, 224 ms | 1,451 | 4 | 33.48% | 46.72% |
| Bangalore, semantic, assumed 30 km/h, 224 ms | 1,451 | 4 | 45.27% | 46.72% |
| Bangalore, semantic, assumed 30 km/h, 350 ms | 1,451 | 2 | 21.76% | 0.48% |
| Bangalore, semantic, assumed 30 km/h, 500 ms | 1,451 | 0 | 0% | 0% |
| Bangalore, spatial sensitivity, assumed 30 km/h, 224 ms | 967 | 4 | 49.67% | 48.80% |
| Jodhpur clear, semantic, assumed 30 km/h, 224 ms | 250 | 2 | 97.70% | 44.97% |
| Jodhpur rain, semantic, assumed 30 km/h, 224 ms | 250 | 2 | 80.43% | 44.97% |
| Udacity highway, semantic, assumed 30 km/h, 224 ms | 484 | 1 | 22.29% | 46.77% |
| Udacity curves, semantic, assumed 30 km/h, 224 ms | 1,199 | 6 | 48.78% | 44.19% |

All nine existing `audit_report` checks pass: accepted submissions have eligible
matching decisions; ordinary spacing and duplicate locks hold; emergency
candidate ticks remain zero; semantic-only runs do not gain spatial estimates;
unknown speed remains unknown. The extra 500 ms run was added after inspecting
the 350 ms boundary and confirms zero fresh semantic ticks and zero video cues.
Startup speech is separate and is not included in the table.

At the 350 ms boundary, two accepted cue ages evaluate to
`0.34999999999999964` seconds under floating-point arithmetic. The code's strict
`age < 0.35` check accepts those boundary-adjacent ticks; it does not demonstrate
broad availability or justify widening the threshold. At 224 ms, semantic updates
every roughly 250–280 ms leave little freshness margin. Actual phone cadence and
processing must be investigated with an explicitly active trace.

The exploratory spatial run resamples at the model's 20 Hz temporal contract,
with **assumed** 110-degree pinhole optics and constant level gravity. Semantic
control runs resample at 30 Hz. This is not an isolated semantic-versus-spatial
ablation: differences cannot be attributed solely to the spatial model. Fresh raw
spatial estimates are present, but **zero road outputs pass validity**. This is
neither calibrated bend recall nor proof of an absence of bends. Original optics,
pose and motion are unavailable; no road-geometry acceptance is credited.

## Visual findings and implications

1. **False obstacle cues persist.** On the winding-road regression, cues at 29.91 s
   and 47.55 s coexist with large car-labelled road regions. At 47.32 s capture,
   no vehicle is visible inside the large road/guardrail box. Prior F01/F02 are
   reproduced. Keep these as explicit failures while adding actor-linked traces
   and temporal/relevance checks; do not hide them by merely reducing cue count.
2. **Opposing traffic still produces advice.** At the highway cue at 2.10 s, the
   retained car detection is across the divider. Same-direction lead relevance
   is not established. Prior F03 is reproduced.
3. **Fresh Bangalore traffic is detected, but risk attribution is unproven.**
   Cue/context frames show genuine cars, motorcycles and overtaking vehicles.
   Obstacle wording at 5.30 s and 33.30 s requires review of the responsible actor
   and useful timing. Sparse frames cannot establish collision risk, and generic
   cue overlap cannot be credited as recognition of a particular encounter.
4. **Phrase length materially changes speech.** Bangalore's same four semantic
   cue times at 0.23, 5.30, 20.23 and 33.30 s occupy 33.48% with unknown speed and
   45.27% with assumed 30 km/h. Longer slowdown/conditional engine-struggle wording
   causes the difference. The system has no recorded gear/RPM evidence here.
   Jodhpur clear remains nearly continuously spoken, with one ordinary cue
   interrupting another. Learner comprehension and usefulness require review.
5. **Availability needs its own acceptance gate.** At 224 ms fixed delay, fresh
   semantic evidence is available on only about 44–47% of decision ticks in the
   semantic runs. At 500 ms it is withheld throughout. Correct rejection of stale
   data is useful, but silence during unavailable perception is not a quality pass.

## Next work

First, finish the parked active-session phone export to verify actual cadence,
decision reasons and availability. In parallel with later implementation, turn
the reproduced false-box and opposing-traffic cases into actor-linked encounter
regressions. Prioritize relevant tracks and credible temporal evidence before
imperative obstacle speech, then shorten ordinary wording and measure speech
occupancy together with missed/late cues. Preserve the existing freshness gates.

Add independent instructor labels and additional consented local drives, with
unseen sessions/routes reserved before further tuning. The current corpus has
no defined hazard denominator and cannot provide an honest accuracy percentage.
Rain and US right-hand-traffic clips are stress/control footage outside the
initial daylight/dry Indian-road scope. No video replay here validates phone
mounting, IMU/GPS, real audio onset, sustained thermal performance, gear coaching,
learner benefit, market demand or readiness for independent driving.
