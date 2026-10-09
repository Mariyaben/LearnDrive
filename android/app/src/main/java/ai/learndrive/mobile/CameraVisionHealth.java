package ai.learndrive.mobile;

import java.util.Locale;

/** Camera scheduling and display freshness. Capture times remain unchanged for hazard rules. */
public final class CameraVisionHealth {
    private CameraVisionHealth() {}

    // PowerManager thermal states: none=0, light=1, moderate=2, severe=3, critical=4.
    public static double intervalSeconds(int thermal) {
        if (thermal >= 4) return Double.POSITIVE_INFINITY;
        if (thermal == 3) return 1.5;
        if (thermal == 2) return .75;
        if (thermal == 1) return .4;
        return .25;
    }

    public static boolean shouldAnalyze(double now, double lastStarted, int thermal) {
        return thermal < 4 && Double.isFinite(now) && Double.isFinite(lastStarted) &&
            now >= lastStarted && now - lastStarted >= intervalSeconds(thermal);
    }

    public static boolean canDisplay(double now, double capture, double completed, int thermal, String error) {
        return thermal < 4 && error == null && Double.isFinite(now) && Double.isFinite(capture) &&
            Double.isFinite(completed) && capture >= 0 && completed >= capture && now >= completed &&
            now - completed < 2 && now - capture < 3;
    }

    public static String status(double now, double capture, double completed, double lastFrame,
                                double started, int count, int thermal, String error, boolean busy) {
        if (error != null) return "Vision unavailable: " + error + ". Tap Restart detection.";
        if (thermal >= 4) return "Detection paused: phone too hot. Resumes automatically after cooling.";
        if (busy && completed < 0) return "Loading offline models; camera preview is ready";
        if (!busy && now - Math.max(started, lastFrame) >= 3)
            return "No camera frames received. Tap Restart detection.";
        if (completed < 0) return "Waiting for the first detection result";
        if (!canDisplay(now, capture, completed, thermal, null))
            return busy ? "Processing camera frame; previous detections expired" : "Waiting for a new detection result";
        final double age = now - capture;
        final String state = age >= .35 ? "Vision delayed" : "Vision active";
        final String heat = thermal >= 1 ? " · phone warm; reduced detection rate" : "";
        return String.format(Locale.US, "%s: %d road users · frame %.0f ms old%s", state, count, age * 1000, heat);
    }
}
