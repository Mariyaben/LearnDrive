package ai.learndrive.mobile;

import java.util.Locale;

/** Camera scheduling and display freshness. Capture times remain unchanged for hazard rules. */
public final class CameraVisionHealth {
    private CameraVisionHealth() {}

    // PowerManager thermal states: none=0, light=1, moderate=2, severe=3, critical=4,
    // emergency=5, shutdown=6.
    //
    // Heat sheds work in order of value per watt (0.8.4). On the S24 Ultra, the optional
    // Supercombo distance model cost ~1 CPU core (35 ms a frame, every frame), the lane model
    // ~0.6 core (~300 ms twice a second, and it held up the detector), and the road-user detector
    // ~0.25 core at 8 Hz. On 2026-11-08 the phone was already at "moderate" 6 s into the drive (sun,
    // screen recording) and reached "critical" at 15:11, when camera coaching stopped for good.
    //   * Supercombo runs only while the phone is cool, at its own 20 Hz design rate.
    //   * The camera drops to 15 frames a second from "moderate" (MainActivity applies the cap).
    //   * The road-user detector keeps running through "critical", slower but inside the 350 ms
    //     coaching freshness gate: reduced coaching beats none. Only "emergency" pauses it.
    public static double intervalSeconds(int thermal) {
        if (thermal >= 5) return Double.POSITIVE_INFINITY;
        if (thermal == 4) return 1.0 / 5;
        if (thermal == 3) return 1.0 / 6;
        if (thermal == 2) return 1.0 / 8;
        if (thermal == 1) return 1.0 / 10;
        return 1.0 / 20;
    }

    /** Analysis stops only when the platform itself is about to shut work down. */
    public static boolean paused(int thermal) {
        return thermal >= 5;
    }

    /** Supercombo spatial inference is optional and the costliest: it runs only on a cool phone. */
    public static boolean spatialAllowed(int thermal) {
        return thermal < 1;
    }

    /** Camera frames per second: the detector needs at most 8; the preview stays usable at 15. */
    public static int cameraFpsCap(int thermal) {
        return thermal >= 2 ? 15 : 30;
    }

    /** The zoomed second detector pass doubles detector work: only with no heat and a fast detector. */
    public static boolean zoomAllowed(int thermal, Double lastDetectorMs) {
        return thermal <= 0 && (lastDetectorMs == null || lastDetectorMs <= 45.0);
    }

    public static boolean shouldAnalyze(double now, double lastStarted, int thermal) {
        return !paused(thermal) && Double.isFinite(now) && Double.isFinite(lastStarted) &&
            now >= lastStarted && now - lastStarted >= intervalSeconds(thermal);
    }

    public static boolean canDisplay(double now, double capture, double completed, int thermal, String error) {
        return !paused(thermal) && error == null && Double.isFinite(now) && Double.isFinite(capture) &&
            Double.isFinite(completed) && capture >= 0 && completed >= capture && now >= completed &&
            now - completed < 2 && now - capture < 3;
    }

    public static String status(double now, double capture, double completed, double lastFrame,
                                double started, int count, int thermal, String error, boolean busy) {
        if (error != null) return "Vision unavailable: " + error + ". Tap Restart detection.";
        if (paused(thermal)) return "Detection paused: phone too hot. Resumes automatically after cooling.";
        if (busy && completed < 0) return "Loading offline models; camera preview is ready";
        if (!busy && now - Math.max(started, lastFrame) >= 3)
            return "No camera frames received. Tap Restart detection.";
        if (completed < 0) return "Waiting for the first detection result";
        if (!canDisplay(now, capture, completed, thermal, null))
            return busy ? "Processing camera frame; previous detections expired" : "Waiting for a new detection result";
        final double age = now - capture;
        final String state = age >= .35 ? "Vision delayed" : "Vision active";
        final String heat = thermal >= 4 ? " · phone very hot; road users only, fewer updates"
            : thermal >= 1 ? " · phone warm; reduced detection rate" : "";
        return String.format(Locale.US, "%s: %d road users · frame %.0f ms old%s", state, count, age * 1000, heat);
    }
}
