# Phase 3: the first real drive with a destination

The aim of this drive is to replay the real route afterwards. The map-based coaching (turns,
bends and speed bumps) can then be compared, second by second, with what your instructor said.

## Before you leave (parked)

1. **App 0.7.8 or newer.** Check the version under Setup.
2. **Phone mount.** Mount the phone level and pointing straight down the road. Earlier drives
   had the camera pitched about 12° up, which hides close road users. The app warns you if
   the mount is too high, too low or tilted.
3. **Hold still while the app levels itself** ("Hold still on level ground"). The bump detector
   only works after this step.
4. **GPS logger app.** Install any GPS logger that writes **GPX** (for example "GPS Logger"
   by BasicAirData or Mendhak). Set it to log every **1 second** and start it before you
   drive. Google's own route and turn data can't be saved by the app (Google's terms), so this
   track is how the route gets replayed.
5. **Diagnostics.** In Setup, turn **Diagnostics** on. If everyone in the car agrees, also turn on
   **Instructor audio**: it records the instructor on the same clock as the app.
6. **Destination.** Choose the destination in Google, then start the supervised session.
   Pick a route of 15–25 minutes with several turns, a few bends and known speed bumps.
   Drive part of it twice if you can: a learned bump is warned about only after the phone has
   felt it on two separate passes.
7. **Optional: screen recording** with sound, as on 5 Nov. It shows exactly what the app
   displayed and said.

## During the drive

- Drive normally. The instructor teaches as usual; nobody needs to look at the phone.
- Don't move the phone in its mount. If it gets knocked, stop somewhere safe and let it level again.

## After the drive (parked)

1. Stop the GPS logger and export the **GPX** file.
2. In the app: stop the session, then **Export diagnostics** (the `.jsonl` file, plus
   `instructor.m4a` if it was recorded).
3. If you made a screen recording, add the instructor's words as subtitles, as you did on 5 Nov.
4. Copy everything into one folder, for example `D:\Downloads\Drive_<date>\`, and tell me the path.

## What I do with it

1. Line up the clocks. The GPX is in UTC; the diagnostics file records its start in UTC and in the
   phone's clock; the instructor audio uses the diagnostics clock. Subtitles are lined up
   through the app's own spoken instructions.
2. Rebuild the route from the GPX track on OpenStreetMap, with the turns a navigation app
   would announce.
3. Replay the brain along the real track at the real speeds. Compare what it says, and when,
   with:
   - what the app actually said during the drive (diagnostics), to find live problems such as
     GPS delay;
   - what your instructor said, for each turn, bend and bump.
4. Check every jolt the bump detector judged against the track and the video, and tune its
   thresholds. Each jolt is in diagnostics with its strength, axle gap, pitch, roll and speed.
   No position is stored there.
5. Look at the speed-display problem from earlier drives in the diagnostics.

## Privacy

- Diagnostics contain no GPS positions and nothing from Google.
- Learned speed bumps stay in the app's private storage on this phone.
- The GPX file is yours. Share it only for this analysis.
