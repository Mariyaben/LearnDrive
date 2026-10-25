# LearnDrive 0.4.6 — implemented repairs and remaining launch work

The candidate is built and installed on the S24 Ultra. It repairs specific
coordinator, wording, evidence-display and replay defects, and adds a grounded
session review. Recognition, useful timing, nuisance burden and learner benefit
remain unqualified for a paid driving-coach launch.

## Installed artifact

- Final APK: [LearnDrive-0.4.6-coach-debug.apk](../../build/LearnDrive-0.4.6-coach-debug.apk),
  version **0.4.6 / 26**, Android debug signing, **237,988,852 bytes**.
- SHA-256: `6d60a96fd90a2066f1bf5e8b8b96b8a2ca64331e3a5f4a03582cf3c4520d4134`.
- The installed package hash matches the final APK.
  [Verification](../../build/trust-increment-0.4.6/final-artifact-verification.json).
- Replays reference the preserved earlier assembly
  `build/LearnDrive-0.4.6-debug.apk` (SHA-256 `e9b7d9b94119fbad23d7d5696f1d94d156b999cb040835c36082e3098b5461ac`).
  The final assembly adds corrected caption/review topic wording. All **82**
  packaged asset/native-runtime entries are byte-identical between assemblies.
  [Payload comparison](../../build/trust-increment-0.4.6/final-payload-comparison.json).

Heat work remains deferred at the user's request. Detector weights, model
session/thread policy, inference cadence and native optimization flags retain
the 0.4.5 baseline. No heat, energy or sustained-performance improvement is claimed.

## What changed

| Area | Implemented behavior | Evidence and limit |
| --- | --- | --- |
| Distinct encounters | A fresh inward entry can supersede repeated attention wording after 3.5 s only with a separately visible, geometrically separated spoken reference actor in the same source | Same phrase/episode retains 10 s suppression; a new tracker ID alone cannot bypass it. Pending audio rechecks original capture age and relevance |
| Brief tracking loss | Bounded 750 ms identity history preserves an accepted actor through short detection gaps | Missing boxes never count as current observations or sustain dwell; longer ambiguous losses remain open |
| Following wording | Prompts 37/46 require known motion plus fresh independently matched trajectory-supported lead evidence; optical expansion alone uses neutral space wording | This establishes an evidence requirement, not validated lead/range accuracy |
| Rider association | Eligible person/bike boxes require coherent motion before a rider association; tiny, static and independently moving overlap negatives are covered | It is a heuristic. Actual rider/person classification is not solved |
| Live road display | Current semantic boxes clear at 350 ms; spatial road shape needs separately fresh completed evidence | Diagnostic retention cannot keep green road boxes current; setup's stable completed-result admission reader remains intact |
| Cue captions | Accepted cues show their age and an appropriate source/stage label, then expire | Road preparation includes camera bends and phone inclination, so it is not labelled universally as a phone measurement |
| Session review | Temporary bounded cue history, estimated camera availability and instructor discussion topics, accessible while parked | Counts reflect accepted submissions, not learner faults/actions. Topic wording follows actual phrases; navigation stays separate; interrupted/test sessions do not become road lessons |
| Replay | Semantic-first publication, separate clocks, no re-dated zero/old spatial tensors, expiring speed samples, full cache provenance | Ordinary MP4 still lacks original calibration/IMU. Concurrent Windows cache publication now preserves one complete validated origin without replacing readers |
| Retained navigation data | Diagnostic speed-limit content is redacted while live policy input remains intact | The test checks actual JSON and unchanged runtime input; future provider-content additions still need review |

## Verification

| Check | Result |
| --- | --- |
| Final Android app + test APK assembly | Pass |
| Native CTest, after build completion | **10/10 groups**, including real Supercombo inference; core suite **137 checks** |
| Camera-health/reference/journal host runner | Pass: 8 camera-health cases, 13 setup/reference cases, readiness and journal contracts |
| Road-status and review host runner | **20 cases**: 8 status + 12 review |
| Replay-contract regressions | **17/17**, including concurrent cache publishers and explicit regeneration |
| Final S24 / Android 16 suite | **15/15**, no skips: Start interaction, captions, dashboard, overlay, retained JSON, session review, Google feed contracts, real Google runtime, actual PCM playback |
| Focused visible review repeat | Pass, focused app-dialog PixelCopy verified; no whole-device screen capture used for the accepted screenshot |
| Audio | Production focus/output completes; observed playback covers the phrase; user directly confirmed it was clearly audible |
| Artifact integrity | Final installed SHA matches; assets/runtimes match the replay reference; debug signature verified |

The real Google check exercised autocomplete, route preparation, ENROUTE updates,
camera continuity, stop/clear and Activity recreation with no extra app route
request. It used no mock GPS or SDK simulator. It was stationary and does not
qualify driving turn timing, reroutes, GPS-loss behavior or sustained operation.

The review test uses a **two-second in-memory cue fixture**, not a drive. Its
availability percentage is not a phone performance result.
[App-dialog capture](../../build/trust-increment-0.4.6/device/session-review-ui.png).
The native regression suite's fixture sensors/boxes likewise establish software
contracts rather than real recognition or hazard accuracy.

The original review UI test initially read Android's parking-dialog button
before its queued listener ran; the corrected test waits for that dispatch.
The first batch exposed a Windows cache replacement race; it was stopped and
preserved under `traffic-cache-race-incomplete`. That run is excluded. The final
batch is a fresh run after the cache repair, with unchanged-source verification.

## Real-video comparison

The final corpus has **28 excerpts / 750.1516 seconds / 22,508 primary frame
steps**. The 62 configurations total 46,286 steps including repeats: 28 primary
semantic runs, 24 semantic controls and 10 assumed-optics spatial runs.
All **62 trace/acknowledgement/source/cooldown audits pass**. All seven known-zero
speed test controls remain quiet; all five 500 ms controls have zero current semantic
availability and zero video cues. Near-350 ms controls can straddle floating-point
boundary comparisons; they do not establish an acceptable production latency.

The separate historical comparator runs the preserved old DLL and new DLL with
the **same normalized YUV frames, current detector tensors, assumed 30 km/h,
224 ms delay, six-second pre-roll and PCM durations**. The old driver cannot
publish spatial evidence in this comparison. Its current-driver events exactly
match each v2 report for all 28 excerpts. This isolates policy behavior from the
new decoder/cache contract; historical inference caches are not reused.

| Matched primary result | Old policy | Current policy |
| --- | ---: | ---: |
| Accepted video cues | 69 | 70 |
| Scheduled speech | 163.616 s / 21.81% | 197.185 s / 26.29% |
| Following-style prompts 37/46 | 30 | 0 |

The zero following-style count is expected for these uncalibrated semantic
inputs. It does not mean all 30 old cues were dangerous or that the new neutral
cues are useful. Longer neutral wording and changed arbitration increase speech
burden. One clear 10-second clip reaches 74.8% scheduled speech; another
30-second clip reaches 53.8%. Neither is acceptable evidence of a calm coach.

In `gp_set_0005_vid_0001`, the same vehicle events at 4.76/16.36 seconds change
from “Keep a safe gap” to neutral space wording. The queued-rider excerpt still
has person cues at 4.23/14.23 seconds with different track IDs; the unknown-speed
control repeats them, while the supplied zero-speed control is correctly quiet. **That real-video
repetition is still open.** No strict distinct-entry exception was exercised by
the 28 primary clips, so the corpus does not establish its practical timing gain.

A matched synthetic two-person case separately proves the coordinator repair:
the old DLL accepts only the first person at 6.145 s; the new DLL accepts that
person and a proven different entry at 10.895 s, a 4.75 s gap within the encounter.
[Failing-before/passing-after evidence](../../build/trust-increment-0.4.6/encounter-failing-before-passing-after.json).

The clips are correlated development material, with night/rain stress scenes.
They are not representative independent Indian drives or instructor-labelled
coaching acceptance. The unchanged detector's small/occluded pedestrian
limitation remains open; recognition coverage was not requalified here. Spatial
optics/gravity remain explicit assumptions, so bend accuracy is not established.

[Derived metrics](../../build/trust-increment-0.4.6/metrics.json),
[per-clip results](../../build/trust-increment-0.4.6/RESULTS.md),
[source stability](../../build/trust-increment-0.4.6/traffic/source-stability.json),
[28 matched comparisons](../../build/trust-increment-0.4.6/matched-semantic-comparison/summary.json).

## Next work before selling learner coaching

1. Obtain event-level instructor labels and useful cue windows on a held-out,
   source-separated set. Fix missed people, late/repeated cues and speech burden
   against those labels; passing trace contracts is insufficient.
   The [70-cue review sheet](../../build/trust-increment-0.4.6/instructor-review/cue-review.csv)
   is prepared with blank human ratings. Review full clips for missed situations
   before auditing cues; this sheet alone cannot establish recall.
2. Compare legally distributable perception candidates under the actual device
   budget. Resume sustained workload/heat qualification separately when requested.
3. Implement validated original-sensor recording/replay bundles, then qualify
   integrated route/GPS/audio/lifecycle faults and supported-device sessions.
4. Establish learner comprehension/usefulness in consented supervised lessons.
5. Resolve model/data distribution, public privacy/terms, production signing/API
   controls, store purchase verification/restore/refunds, support and staged rollout.

The bundled YOLO detector has an AGPL-3.0 distribution decision still to resolve;
commercial sale itself is not prohibited by AGPL. A proprietary release needs
suitable terms or a properly qualified replacement. No license purchase or
source publication has been made.
[Vendor licensing](https://www.ultralytics.com/license).

Continue from the [paid-launch execution plan](../product/PAID_LAUNCH_EXECUTION.md)
and [privacy data inventory](../product/PRIVACY_DATA_INVENTORY.md).
