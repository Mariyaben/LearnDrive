package ai.learndrive.mobile;

/** Observed capability, not a device-class rating. Owned by one Vision instance.
 * Three consecutive >=250ms spatial stages leave too little of the 350ms
 * evidence lifetime for semantic work and delivery. Explicit restart retries. */
public final class VisionCapabilityPolicy {
    private int consecutiveSlow;
    private long samples, slowSamples, failures;
    private String reason = "";

    public boolean spatialEnabled() { return reason.isEmpty(); }
    public String profile() { return spatialEnabled() ? "STANDARD" : "BASIC"; }
    public String reason() { return reason; }
    public long samples() { return samples; }
    public long slowSamples() { return slowSamples; }
    public long failures() { return failures; }
    public boolean advancedAvailable() { return false; }

    public void spatialUnavailable() {
        if (spatialEnabled()) reason = "Spatial model unavailable; semantic detections only";
        failures++;
    }

    public void completedSpatial(double milliseconds) {
        if (!spatialEnabled() || !Double.isFinite(milliseconds) || milliseconds < 0) return;
        samples++;
        if (milliseconds >= 250) { consecutiveSlow++; slowSamples++; }
        else consecutiveSlow = 0;
        if (consecutiveSlow >= 3)
            reason = "Spatial processing exceeded its budget repeatedly; semantic detections only. Restart detection to retry";
    }

    /** Pose assessment always wins, including a known unsupported mount. */
    public int readiness(int poseReadiness) {
        return poseReadiness == SpatialReadiness.READY && !spatialEnabled()
            ? SpatialReadiness.BASIC_SEMANTIC_ONLY : poseReadiness;
    }
}
