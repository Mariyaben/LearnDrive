package ai.learndrive.mobile;

/** Learner-facing availability. Retained lab images are not a current road assessment. */
public final class RoadCoachingStatus {
    private RoadCoachingStatus() {}

    // Match the native guidance capture-age gate, not the longer diagnostic display window.
    public static final double MAX_CAPTURE_AGE_SECONDS = .35;

    public static boolean currentEvidence(double now, long source, long evidenceSource,
                                          double capture, double completed, boolean running,
                                          int thermal, String error) {
        return running && thermal < 4 && error == null && source == evidenceSource &&
            Double.isFinite(now) && Double.isFinite(capture) && Double.isFinite(completed) &&
            capture >= 0 && completed >= capture && now >= completed &&
            now - capture < MAX_CAPTURE_AGE_SECONDS;
    }

    public static final class State {
        public final boolean currentEvidence;
        public final String status;
        public final String idleCaption;

        private State(boolean currentEvidence, String status, String idleCaption) {
            this.currentEvidence = currentEvidence;
            this.status = status;
            this.idleCaption = idleCaption;
        }
    }

    public static State assess(double now, long source, long evidenceSource, double capture,
                               double completed, boolean running, boolean voiceReady, int thermal,
                               String error, int readiness, boolean spatialCurrent) {
        boolean current = currentEvidence(now, source, evidenceSource, capture, completed, running, thermal, error);
        if (error != null || !running)
            return new State(false, "Camera unavailable · no current road assessment",
                "Use your instructor; camera coaching is unavailable.");
        if (thermal >= 4)
            return new State(false, "Camera coaching paused · phone too hot",
                "Keep watching the road; camera coaching is paused.");
        if (!current)
            return new State(false, "Camera delayed · no current road assessment",
                "Keep watching the road; camera assessment is delayed.");
        if (!voiceReady)
            return new State(true, "Voice unavailable · spoken coaching paused",
                "Keep watching the road; the coaching voice is unavailable.");
        if (readiness == SpatialReadiness.MOUNT_UNSUPPORTED)
            return new State(true, "Basic detection · mount needs adjustment",
                "Adjust the camera only when parked.");
        String status = readiness != SpatialReadiness.READY || !spatialCurrent
            ? "Basic detection · distance estimates unavailable"
            : thermal >= 2 ? "Road detection active · phone warm" : "Road detection active";
        return new State(true, status, "Keep scanning the road. Silence is not an all-clear.");
    }
}
