package ai.learndrive.mobile;

/** Pose readiness is separate from level-ground normalization and hazard accuracy. */
public final class SpatialReadiness {
    public static final int ORIGINAL_CALIBRATION_UNKNOWN = 0;
    public static final int INTRINSICS_UNAVAILABLE = 1;
    public static final int GRAVITY_UNAVAILABLE = 2;
    public static final int MOUNT_UNSUPPORTED = 3;
    public static final int READY = 4;

    private SpatialReadiness() {}

    // The native fusion supplies a unit vector aligned to the capture timestamp,
    // or zero when no sufficiently fresh pose exists. No upright fallback.
    public static int evaluate(boolean originalCamera, boolean hasIntrinsics,
                               double x, double y, double z) {
        if (!originalCamera) return ORIGINAL_CALIBRATION_UNKNOWN;
        if (!hasIntrinsics) return INTRINSICS_UNAVAILABLE;
        double norm = Math.sqrt(x*x + y*y + z*z);
        if (!Double.isFinite(norm) || norm < .9 || norm > 1.1) return GRAVITY_UNAVAILABLE;
        if (Math.hypot(x,y) / norm < .35) return MOUNT_UNSUPPORTED;
        return READY;
    }

    public static String status(int state) {
        switch (state) {
            case READY: return "Horizon aligned from phone motion; road geometry remains an estimate";
            case INTRINSICS_UNAVAILABLE: return "Camera optics unavailable; semantic detections only";
            case GRAVITY_UNAVAILABLE: return "Waiting for fresh phone motion; semantic detections only";
            case MOUNT_UNSUPPORTED: return "Point the rear camera forward, away from ground or sky; semantic detections only";
            default: return "Original calibration unknown; semantic detections only";
        }
    }
}
