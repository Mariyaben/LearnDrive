# Instructor benchmark

The benchmark compares what a qualified driving instructor says during a real lesson with what the app said at the same moments. It is the evidence the software tests cannot give. Passing native, replay or simulator tests shows that the code does what it was designed to do. It does not show that the coaching teaches well.

**What it measures:** agreement with one instructor on one drive. It is not a validation of teaching quality, safety or legality.

## 1. Record a lesson (phone)

1. Mount the phone and, while parked, open **Lab settings and test sources**.
2. Press **Start local diagnostics** and choose **With instructor audio**.
3. Confirm that everyone in the car agrees, then allow the microphone.

The app records AAC mono audio at 16 kHz to app-private storage, up to about 3.5 hours. Recording runs only while diagnostics run, and it stops when the app leaves the screen, when diagnostics stop, or when the size limit is reached.

The diagnostics journal records two events:

- `instructor_audio_start`: the start time on the journal clock, as the midpoint of `MediaRecorder.start()`, with the uncertainty.
- `instructor_audio_stop`

Each `coaching_accepted` record now also carries the composed instruction text.

After the drive:

- **Export diagnostics (JSONL)** and **Export instructor audio (M4A)**.
- **Delete local diagnostics** removes both from the phone.

The microphone also hears the app's own voice and anything else said in the car. Nothing is uploaded.

## 2. Transcribe

The benchmark needs a transcript with times in seconds from the start of the audio. Any of these formats works:

- Whisper JSON (`{"segments": [{"start", "end", "text"}]}`)
- SRT
- WebVTT
- CSV or TSV with columns `start[,end],text`

Hand annotation in the CSV form is fine, for example while replaying the audio.

To transcribe with Whisper, install the optional `faster-whisper` package, which downloads model weights on first use, then pass `--audio`. For an instructor speaking Malayalam, Hindi or another language, add `--language ml --translate` so that the text is in English for the lexicon. You can also extend the lexicon with `--lexicon extra.json` (`{"clutch": ["\\bklachu\\b"]}`).

## 3. Compare

```bash
python tools/instructor_benchmark.py LearnDrive-session.jsonl --transcript lesson.srt --output build/benchmark/lesson.json --markdown build/benchmark/lesson.md
```

The tool works the same way on a replayed or filmed drive, using a `review_video.py` `report.json` or `guidance.csv`. Pass `--audio-offset` if the transcript does not start at video time 0.

### Intents

Both sides are classified by one instruction lexicon. Each sentence can carry several intents:

| Group | Intents |
|---|---|
| Speed | brake, slow, stop (`reduce_speed` covers both brake and slow) |
| Car controls | clutch, gear (with the gear number), accelerate, steer |
| Signals and checks | signal (with the side), mirror, look, horn |
| Position and timing | keep left, give space, wait |
| Context | junction, slope, road user, speed bump |

One intent is not given by the app by design: **proceed** ("go now"). The app never grants permission to go. It appears in the report but is excluded from the coverage score. The horn is advised on Indian roads (blind bends, junction entry, passing someone at the road edge) and is scored.

### Matching

An instructor instruction and an app instruction agree when they share an intent within ±3 s (`--window`). Matching is one-to-one, nearest first. The report shows:

- instructor actions the app also gave;
- app actions the instructor also gave;
- per-intent matched, instructor-only and app-only counts;
- median timing (app minus instructor; negative means the app spoke first);
- gear-number and signal-side agreement;
- a timeline;
- unclassified instructor speech, to review and extend the lexicon.

### App echo

A transcript segment that repeats an app sentence verbatim (runs of 4 or more words, or the whole of a shorter instruction) while the app was speaking is removed. App words are stripped from mixed segments. The app is never credited with its own voice. If the instructor says exactly the app's words at the same moment, that instruction is lost too. This is conservative for agreement.

## Reading the result

An instructor-only instruction is not automatically an app error. The instructor sees what the app cannot: gaze, the selected gear and the pedals are unobserved by the app, and the instructor may give route directions.

An app-only instruction is not automatically noise either: instructors stay silent for many reasons. Use the timeline to judge each case.

Aggregate several lessons, instructors and roads before drawing conclusions. Report the numbers with this caveat.
