# Public traffic footage — exploratory test, 16 October 2026

I independently located, downloaded, checksum-pinned and replayed four public
source clips. All four were processed in full, about **84 seconds / 2,183 unique
analyzed frames**. Thirteen configurations cover semantic/spatial comparison,
missing speed and an optics-sensitivity experiment. These are now development
examples, not an independent held-out benchmark.

**The software contracts pass. Guidance acceptance does not pass this review.**
There are concrete false/irrelevant cues to fix before learner release.

## Watch and inspect

- [Interactive video/cue review](../build/public-traffic/index.html): nine annotated
  videos with the actual cached guidance audio, cue seeking, raw report/trace links
  and reviewed failure-frame sheets.
- [Measurement table and findings](../build/public-traffic/RESULTS.md).
- [Aggregate measurements](../build/public-traffic/aggregate.json).
- Source URLs, attribution, hashes and test conditions:
  `scenarios/public-traffic/sources.json`.
- Provisional event windows recorded before the first inference run:
  `scenarios/public-traffic/review-events.csv`.
- Post-hoc exploratory visual findings:
  `scenarios/public-traffic/review-findings.json`.

## Sources and test conditions

| Source | Content | Length | Analysis frames |
| --- | --- | ---: | ---: |
| [IDS-JODHA clear preview](https://shaaashvat.github.io/ids-jodha/) | Indian urban junction, motorcycles, crossing car, pedestrians and auto-rickshaws | 10 s | 250 |
| [IDS-JODHA rainy preview](https://shaaashvat.github.io/ids-jodha/) | Indian urban traffic, rain, reflections and wipers | 10 s | 250 |
| [Udacity challenge video](https://github.com/udacity/CarND-Advanced-Lane-Lines) | Freeway, overpass shadows and passing/adjacent traffic | 16.15 s | 484 |
| [Udacity harder challenge video](https://github.com/udacity/CarND-Advanced-Lane-Lines) | Winding road, strong shadows and oncoming cars/motorcycles | 47.96 s | 1,199 |

Original files are retained under `build/public-traffic/sources`. Full clips were
resized to 640×360, source audio removed and source frame rates retained for the
prepared files. Replay uses 25 Hz for the 25 FPS sources and 30 Hz for the
30000/1001 FPS freeway source. The latter is slightly resampled. Resizing can
remove useful spatial detail; no results are substituted for original-resolution
or phone validation. Upstream Udacity license text is retained with the sources.

Eight primary configurations compare semantic coaching without original optics
against actual native Supercombo inference with assumed 110° HFOV. **30 km/h is
an explicitly assumed manual test input, not measured speed.** Four additional
semantic runs leave speed unknown. One additional winding-road run assumes 70°
HFOV. All use a six-second pre-roll, 180 ms simulated processing delay and the
same native guidance coordinator. Emergency speech remains disabled, but all
candidate IDs are audited; no emergency candidate occurs in these cases.

The US footage is an out-of-domain stress/control set: the native model remains
configured for India's traffic convention. Rain is outside the proposed initial
dry-road support. No collision recall, depth accuracy, safe speed, precision/
recall percentage, physical audio performance or instructor approval is claimed.

## What the tests found

1. **False obstacle instruction at 47.50 s on the winding road.** A large car box
   with score approximately 0.43 covers the visible road/guardrail. This queues
   an obstacle/slow-down/conditional-gear phrase in both semantic and spatial
   primary runs. The unknown-speed control still issues an obstacle phrase.
2. **Another spurious large car box at 29.86 s.** A real oncoming car is visible
   on the left, while a second large box covers road/roadside. The same compound
   phrase is queued. The trace lacks the responsible track ID, so exact actor
   attribution cannot be established from the decision record alone.
3. **Irrelevant gap coaching at 2.05 s on the freeway.** The retained vehicle
   detection is across the concrete divider, at the far left. The generic
   traffic/gap cue does not establish a same-direction lead.
4. **Very low geometry availability with the initial assumed optics.** Only
   1/2,183 primary spatial frames passes the road-shape gate; none does on the
   winding-road run at 110°. At 70°, 127/1,199 frames pass and bend cues appear
   at 21.38 s (left) and 33.94 s (right). This demonstrates a calibration
   dependency. It does not identify the correct FOV or validate bend timing.
5. **Speech is too dense to accept without learner review.** At manual 30 km/h,
   scheduled guidance occupies 9.82/10 seconds in the Indian clear clip and
   about 80% of the rainy clip. These values account for interruptions and
   clipping at the recording end; they are not measured driver distraction.
6. **Some attention cues do occur.** The Indian clips produce road-user cues
   at 0.18 s. A later clear-clip obstacle cue overlaps the crossing-car window,
   but its capture shows a large motorcycle before the crossing white car.
   Generic timing overlap is not credited as crossing-car recognition.

Semantic and 110° spatial primary timelines are identical on each clip. In those
conditions most delivered advice comes from image-based camera coaching, not
useful model geometry. The 70° experiment shows Supercombo can affect guidance,
while also showing why unmeasured optics cannot support a reliability claim.

## Checks completed

- All 13 replay contracts pass: source hashes, trace sequence/acknowledgement
  consistency, global/duplicate audio timing, no fabricated speed in missing-speed
  controls and no spatial result leakage in semantic mode.
- Actual model inference runs for the five spatial input configurations; cached
  raw inference is reused for repeated policy/rendering runs.
- Output JSON is strict: unavailable values use null rather than Infinity/NaN.
- The prior 1,201-frame Kochi recorded-speed replay retains exactly the same
  five-cue timeline and candidate tick counts after replay-tool changes.
- Python compilation and generated review-page JavaScript syntax checks pass.
- All nine annotated MP4s have video/audio, matching durations and error-free
  full ffmpeg decode; hashes are in `build/public-traffic/media-verification.json`.
- A headless Edge check exercises all 13 viewer selections, cue counts and
  disabled controls for cases without rendered media; browser video readiness
  passes. Passing playback/UI checks does not establish guidance quality.

Only replay/test tooling changed in this task. The Android APK remains 0.3.2;
this is shared-core desktop testing, not an Android device test.

## Reproduce

Build the `learndrive_replay` target, then:

```powershell
python tools/test_public_traffic.py --test-speed-kmh 30 --render
python tools/test_public_traffic.py --mode semantic
python tools/review_video.py build/public-traffic/clips/udacity-curves.mp4 --mode spatial --fps 25 --hfov 70 --test-speed-kmh 30 --pre-roll-seconds 6 --latency-ms 180 --output build/public-traffic/runs/udacity-curves-spatial-hfov70-manual-30 --render
python tools/report_public_traffic.py
```

Keep these findings as development regressions. The next fixes should address
confirmed vehicle-track evidence before imperative obstacle coaching, ego-path/
oncoming/parked separation, actor-linked decision reasons, calibrated original
inputs, and shorter episode-based speech. Evaluate fixes on additional unseen
footage and independently reviewed event labels.
