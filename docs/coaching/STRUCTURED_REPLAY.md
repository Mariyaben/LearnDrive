# Structured coaching replay and shadow contracts

`python tools/generate_coaching_fixtures.py` regenerates the seventeen **synthetic** fixtures under `scenarios/balanced-coaching`. None is a recorded drive, original-sensor calibration, instructor judgment, learner outcome or private provider dataset. Supplied hypotheses are outputs at the candidate boundary; no person-box geometry is converted into path/approach evidence by this harness. Native policy, validator, memory, ledger, acceptance and playback gates use the shared production `CoachingEngine`.

After building the `learndrive_coaching_replay` target, run locally:

```powershell
$fixtures = Get-ChildItem scenarios/balanced-coaching/*.json | ForEach-Object FullName
python tools/replay_coaching.py @fixtures --output build/balanced-coaching/structured-replay
python tools/benchmark_coaching_shadow.py @fixtures --output build/balanced-coaching/shadow-benchmark.json
python -m unittest discover -s tests -p 'test_coaching*.py' -v
python tools/coaching_contract.py docs/coaching/instructor-annotation-template.json
```

The second command records the policy model as **unavailable** unless both an existing local adapter argv file and existing local weight/tokenizer/config artifacts are supplied. It does not download a model. Shadow input admits only independent project-authored synthetic fixtures with explicit `google_maps_content:false`; Google/Android exports and unverified recorded observations are rejected before model execution. Extending shadow evaluation to original/licensed recorded data requires a separate consent/licence/source provenance validator. Tests use fake adapters to verify parsing, timeout/resource hooks and the shadow boundary; those fixtures cannot be reported as pretrained reasoning.

The benchmark's legacy path invokes the existing native global/phrase audio gate and existing prompt priority after the same validator, on identical supplied candidate/evidence records. It is named `legacy_candidate_audio_gate`. It excludes detector-derived CameraCoach distinct-entry exceptions and the full old perception/coordinator. It is therefore a narrow scheduler comparison. Video replay v2 retains its legacy default and cache contract; an explicit new policy option runs Balanced. The historical matched DLL comparator rejects Balanced reports so results cannot be relabelled as historical comparisons.

An omitted candidate expiry derives from its original capture time plus its maximum age. It never derives from replay publication/decision time. Unknown capture remains unknown. Clock reordering, expiry and source tombstones are validated by the native engine.

Recorded-video opt-in, using actual local detector output and the shared C API:

```powershell
python tools/review_video.py path/to/clip.mp4 --mode semantic --coaching-profile balanced --latency-ms 224 --output build/balanced-coaching/video-replay/clip
```

The 224 ms completion delay is an explicit test assumption. No original optics/IMU, road grade or selected gear is fabricated. The configured recording/screen policy does not treat manual test speed or the phone holding a replay as measured driver response; a qualified original-motion sidecar remains future work. The structured harness can already supply explicitly valid synchronized motion for independent deterministic tests. Video acceptance/onset are simulated, not physical playback. Source/library/model/tool hashes are pinned in each report. No video or provider data is uploaded.

`replay_coaching.py` retains input/library/source hashes, evidence kind, per-tick mode/block reasons, exact acceptance/source/settings revision, logical versus voice submission versus simulated validated onset counts, rejected queue/onset work, deferred/duplicate/silence/braking-block ticks, accepted original capture age and host call timing. Counts and host timings describe software execution. Silence is permitted and never means all clear. A simulated onset never means a user heard or understood a cue. Host JSON/ctypes timing is not phone CPU, battery, heat or end-to-end speech latency.

The structured file contract rejects nonfinite JSON, duplicate keys, unknown fields, unbounded actor/candidate arrays and implicit valid measurements. Publication times must increase. Capture clocks may deliberately be stale, future or out of order so native freshness checks can be exercised without silently fixing the input. `null` stays unavailable. Sources and settings retain their monotonic tombstones in the engine/bridge. Independent route GPS capture time is explicit; ordinary provider content is not included. Supplied congestion is an explicit producer candidate, never inferred from actor count. Missing mechanical/slope measurements cannot qualify those actions.

Local shadow adapters receive a bounded JSON request on stdin. They may propose only `SILENCE`, `DEFER`, or a zero-based supplied candidate index with `COACH`/`WARN`; diagnostic rationale is limited to 512 characters and never used as speech. Routine candidates cannot be promoted to `WARN`. Every non-silent proposal passes the same C++ `SafetyValidator`; no shadow proposal calls acceptance or playback. Resource controls are polled timeout/output limits and sampled process-tree RSS when `psutil` is installed. Process startup is included. Offline environment flags are hints, not an OS network sandbox; supply only trusted local adapter code. Pin its executable/script alongside model files with repeated `--model-artifact` options. There is no paid-service integration.

The blank `instructor-annotation-template.json` and JSON schema establish a separate human annotation workflow. `coaching_contract.validate_annotation` also rejects reversed timing intervals and incomplete reviewed labels. Review whole source intervals independently of cues; include silence windows and missed encounters. Pin source SHA256 and data split, reviewer role/ID, actor/encounter, useful timing, permissible action IDs, rationale and uncertainty. Preserve disagreements and adjudication. Pending templates are unavailable for scoring. The existing seventy-row instructor CSV remains unfilled. No coaching precision, recall, nuisance, useful-window or superiority score is reported without genuine reviewed labels and complete exposure denominators.
