# Phone heat (0.8.4)

## What the Nov 8 drive showed

The phone's heat level, read from the camera status line of the screen recording:

| From | Android thermal status | What the app did (0.8.1) |
|---|---|---|
| 0:06 | **moderate** already ("Road detection active · phone warm") | shed the distance model |
| 0:10 → 15:10 | moderate / severe | "Basic detection · distance estimates unavailable" for 595 s |
| throughout | frames running late | "Camera delayed · no current road assessment" for 178 s; road-user coaching paused each time |
| 15:11 → end | **critical** | "Camera coaching paused · phone too hot": no camera coaching at all (bus 15:03, truck 15:24, pedestrian 16:21) |

- **Already hot before the drive.** The phone was hot 6 s after starting, before the app had done much: sun on the dashboard, screen recording, perhaps charging.
- **It kept heating with the optional models shed.** The camera, the detector, the 120 Hz screen, Google's map and the recording took it to critical.
- **Heat delayed warnings too.** Fed the same 640-pixel frames, the detector boxes the LEADER bus at 12:55 on the desktop; on the phone it was 12:57.5.

## What the app itself cost (S24 Ultra)

| Workload (0.8.3) | Cost per run | When | CPU |
|---|---|---|---|
| Supercombo distance model | ~35 ms (phone benchmark, Oct 20) | every camera frame, ~20–25 a second, while thermal < moderate | ~0.7–0.9 core |
| Lane model (TwinLiteNet) | ~300 ms (desktop 177 ms × the phone's ~1.75 factor) | twice a second, while thermal < moderate | ~0.6 core |
| Road-user detector (YOLOv8n 320) | ~29 ms | 8 a second | ~0.25 core |

- **Agreement with the earlier check.** That sums to ~1.6–1.9 cores, matching the 159–188 % process CPU in the 20 October thermal check.
- **The lane model also delayed detection.** It ran on the detector's own thread, so each run held up road-user detection by ~0.3 s.
- **Little value on these roads.** It only feeds lane-drift cues, which need painted lane lines.

## What 0.8.4 changes

Heat now sheds work in order of value per watt (`CameraVisionHealth`):

| Thermal status | Distance model | Camera | Road-user detector |
|---|---|---|---|
| none (0) | on, at most 20 a second (its design rate) | 30 fps | 8 a second |
| light (1) | **off** (was on) | 30 fps | 8 a second |
| moderate (2) | off | **15 fps** (was 30) | 8 a second |
| severe (3) | off | 15 fps | 6 a second (was 5) |
| critical (4) | off | 15 fps | **5 a second** (was paused) |
| emergency (5+) | off | – | paused |

Also:
- **The lane model is off in coaching.** It loses lane-drift cues on marked roads, and it no longer holds up the detector.
- **The screen asks for 60 Hz** instead of 120 Hz.
- **Critical heat keeps road-user coaching alive.** At 5 a second plus processing, the evidence stays inside the 350 ms freshness gate. The status line says "Road users only · phone very hot", so quieter coaching is not taken for a quiet road. Only "emergency" pauses it.
- **Warm status names the reason:** "Basic detection · phone warm, distance model off".
- **Heat goes into the evidence log.** Every frame record now carries `thermal_status` and `camera_fps_cap`, so the next drive's export shows the heat over time.

Estimated CPU from the app's camera work:
- cool phone: ~1.7 → ~0.95 cores;
- warm (light): ~1.5 → ~0.25 cores;
- moderate and above: unchanged CPU, but half the camera frames and half the screen refreshes.

## Not changed, and why

- **The detector stays at 320×320, 8 a second, on the CPU.**
  - It could move to the phone's NPU (ONNX Runtime's QNN provider: a different runtime package and a quantized model).
  - Or it could become int8 on the CPU (~2× cheaper): this needs a calibration set and a check on the drive replays that far road users are still found.
- **Google's map rendering and the screen brightness are not the app's to change.**

## What the driver can do on a hot day

- Keep the phone out of direct sun (a vent mount, or shade).
- Avoid fast charging while driving.
- Screen recording costs heat; record only when needed.

## Checking on the next drive

The evidence export now has `thermal_status` and `camera_fps_cap` on every frame. `adb logcat -s LearnDrivePerf` shows:
- the analysis rate (`fps=`);
- per-frame time;
- thermal status;
- "camera frame rate capped at [..]" when the cap changes.

Not yet measured on the phone.
