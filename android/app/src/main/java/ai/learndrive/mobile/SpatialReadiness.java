package ai.learndrive.mobile;

/** Pose readiness is separate from level-ground normalization and hazard accuracy. */
public final class SpatialReadiness {
    public static final int ORIGINAL_CALIBRATION_UNKNOWN = 0;
    public static final int INTRINSICS_UNAVAILABLE = 1;
    public static final int GRAVITY_UNAVAILABLE = 2;
    public static final int MOUNT_UNSUPPORTED = 3;
    public static final int READY = 4;
    public static final int BASIC_SEMANTIC_ONLY = 5;

    private SpatialReadiness() {}

    // The native fusion supplies a unit vector aligned to the capture timestamp,
    // or zero when no sufficiently fresh pose exists. No upright fallback.
    public static int evaluate(boolean originalCamera, boolean hasIntrinsics,
                               double x, double y, double z) {
        if (!originalCamera) return ORIGINAL_CALIBRATION_UNKNOWN;
        double norm = Math.sqrt(x*x + y*y + z*z);
        // A fresh unit pose can prove an unsupported mount even without lens
        // metadata. Missing optics must not hide this admission guard.
        if (Double.isFinite(norm) && norm >= .9 && norm <= 1.1 && Math.hypot(x,y) / norm < .35)
            return MOUNT_UNSUPPORTED;
        if (!hasIntrinsics) return INTRINSICS_UNAVAILABLE;
        if (!Double.isFinite(norm) || norm < .9 || norm > 1.1) return GRAVITY_UNAVAILABLE;
        return READY;
    }

    /**
     * Mount advice from the camera's pitch and roll (gravity "up" in phone axes; the rear
     * camera looks along -z). Pointing up wastes the frame on sky and shrinks distant road
     * users; on the 2026-11-04/05 drives about 75% of each frame was sky and trees.
     * Returns null when the mount is fine. Adjust only when parked.
     */
    public static String mountAdvice(double x, double y, double z) {
        double norm = Math.sqrt(x*x + y*y + z*z);
        if (!Double.isFinite(norm) || norm < .9 || norm > 1.1) return null;
        double pitchUp = Math.toDegrees(Math.atan2(-z, Math.hypot(x, y)));
        double roll = Math.toDegrees(Math.atan2(Math.abs(x), Math.abs(y)));
        double tilt = Math.min(roll, 90 - roll); // landscape or portrait: deviation from level
        if (pitchUp > 6) return "Camera points too high · tilt it down when parked";
        if (pitchUp < -20) return "Camera points too low · tilt it up when parked";
        if (tilt > 8) return "Camera is tilted sideways · level it when parked";
        return null;
    }

    public static String status(int state) {
        switch (state) {
            case BASIC_SEMANTIC_ONLY: return "Basic capability: semantic detections only; metric road geometry unavailable";
            case READY: return "Horizon aligned from phone motion; road geometry remains an estimate";
            case INTRINSICS_UNAVAILABLE: return "Camera optics unavailable; semantic detections only";
            case GRAVITY_UNAVAILABLE: return "Waiting for fresh phone motion; semantic detections only";
            case MOUNT_UNSUPPORTED: return "Point the rear camera forward, away from ground or sky; semantic detections only";
            default: return "Original calibration unknown; semantic detections only";
        }
    }
}
