# Bundled models and exact interface

The active spatial backbone is now **Comma.ai Supercombo v0.8.13**, bundled as
`supercombo.onnx` with `COMMA-LICENSE.txt` and `supercombo-manifest.json`.
[INTEGRATION.md](../docs/INTEGRATION.md) records the exact tensor contract,
upstream commit, SHA-256, coordinate transforms and recurrent-state handling.
Run `python tools/verify_supercombo.py` to check the actual model. Android uses
YOLO below for semantic road-user labels; TwinLiteNet is retained for legacy
tests and is no longer executed in the active road pipeline. iOS uses the same
native ORT Supercombo runtime and does not require the older Core ML exports.

Android includes `detector.onnx`, `road.onnx` and 35 PCM16 guidance/warning WAVs in `android/app/src/main/assets/`. The app requires no model downloads or installed TTS voice. `model-manifest.json` records original URLs, source hashes, exported hashes and tensor shapes. Gradle rejects builds missing any required asset.

## Detector: pretrained YOLOv8n COCO

The source is the [official Ultralytics v8.2.0 weight release](https://github.com/ultralytics/assets/releases/tag/v8.2.0), exported using Ultralytics 8.2.59 without built-in NMS. The full upstream AGPL-3.0 license is included as `YOLO-LICENSE.txt`; retain it and follow the upstream license when redistributing the model/application.

| Platform | Input | Output |
| --- | --- | --- |
| Android `detector.onnx` | `images`: Float32 `[1,3,320,320]`, NCHW RGB 0..1 | `output0`: Float32 `[1,84,2100]`; first four values are cx,cy,w,h in **320x320 pixels**, followed by 80 COCO probabilities |
| iOS `detector.mlmodelc` (separate export required) | `images`: Float32 `[1,3,320,320]`, RGB 0..1 | `detections`: Float32 `[1,84,N]` or `[1,N,84]`; coordinates in **320x320 pixels** |

The core selects the highest-scoring COCO category before filtering to person 0, bicycle 1, car 2, motorcycle 3, bus 5 and truck 7. It performs class-aware NMS and removes letterbox padding. Other COCO categories are intentionally excluded from driving perception. Android displays these boxes even without calibration or a GPS fix; spoken driving advice has additional motion, path and confidence gates.

**Zoomed second pass (0.7.4, built but OFF in coaching).** It is designed to run only when the phone reports no thermal throttling and the last detector run took 45 ms or less. Android would run the same detector again on a square crop of the road ahead. The crop is half the long side of the frame, centred horizontally and at 58% of the height, which is 2x the pixels for distant road users. `ld_zoom_merge` maps those candidates into full-frame model pixels (dropping boxes cut by the crop edge) and appends them, so one decoding and class-aware NMS covers both. The cost is a second detector run plus one extra preprocessing. On the unlabelled 2026-11-04 drive it added confirmed detections ahead and earlier first detections. Most of a visually reviewed sample were real riders, pedestrians and vehicles; people printed on billboards were the main false positives. In the coaching replay of that drive it produced false instructions for billboard people and "brake now" for road users still far away. So `ZOOM_IN_COACHING` stays false until elevated-object rejection and distance-aware urgency for small boxes exist. See `tools/evaluate_detector_zoom.py` and the [0.7.4 review](../docs/reviews/2026-11-04-holistic-coach.md). Replays use `tools/review_video.py --detector-zoom`.

**Licensing.** YOLOv8n is AGPL-3.0 (see above). A permissively licensed detector (for example an Apache-2.0 RT-DETR, D-FINE or YOLOX export) at the same `[1,84,N]`-compatible output contract would remove that question. It needs a download, an export, and the same recall evaluation on drive videos before it replaces this model.

## Road/lane model: pretrained TwinLiteNet

The architecture and `best.pth` checkpoint come from the [authors' TwinLiteNet repository](https://github.com/chequanghuy/TwinLiteNet), under the included `TwinLite-LICENSE.txt` MIT license.

Android `road.onnx` consumes the same Float32 RGB NCHW `images` tensor as the detector. `drivable` and `lanes` outputs are Float32 `[1,2,320,320]` logits. Channel zero is background and channel one is foreground. The core converts these to foreground probabilities. iOS requires a separately exported `road.mlmodelc` with the same named inputs and outputs.

The official example resizes to 640x360. This Android export uses letterboxed 320x320 for a smaller inference workload. PyTorch/ONNX numeric equivalence is checked at the exported resolution; perception accuracy at this new size and on target roads still needs validation. In-path gating intersects the drivable mask with a central corridor. Reliable ego-lane topology, curves and cut-ins remain limitations.

## Regenerate and verify

With Python, CPU PyTorch 2.1+, Ultralytics 8.2.59, ONNX, ONNX Runtime and NumPy installed:

```powershell
python tools/export_android_models.py
powershell -NoProfile -File tools/render_android_prompts.ps1
python tools/verify_android_assets.py
python tools/verify_android_assets.py --apk android/app/build/outputs/apk/debug/app-debug.apk
```

The export script downloads build-time sources into `.tools/models`, exports static opset-17 graphs, compares TwinLiteNet outputs with its PyTorch reference and records hashes. The speech script uses the installed Windows Microsoft Zira Desktop voice and renders 35 fixed phrases at 22050 Hz, mono PCM16: tactical, camera, bend, slope and conditional/engine-data gear guidance, including low-speed variants. WAVs are prerecorded app resources; no speech synthesis or network access occurs during Android use. Text comes from the shared native prompt table; the asset verifier checks that `tools/render_android_prompts.ps1` has the same ordered phrases and clip count.

For the real image smoke check, download the [official Ultralytics bus sample](https://raw.githubusercontent.com/ultralytics/assets/main/im/bus.jpg) to `.tools/models/bus.jpg` and run:

```powershell
python tools/verify_android_assets.py --image .tools/models/bus.jpg --apk android/app/build/outputs/apk/debug/app-debug.apk
```

This asserts person and bus detections, finite road masks, non-silent PCM clips, model hashes, output contracts and byte-identical APK resources. It does not measure phone latency or road accuracy. The Android CPU baseline uses two inference threads; accelerator selection and quantization require separate verified exports and device benchmarks.
