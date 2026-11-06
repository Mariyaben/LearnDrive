package ai.learndrive.mobile;

/** Model metadata validity only; this never grants instruction/speech authority. */
public final class ReasoningGate {
    private ReasoningGate() {}
    public static boolean current(boolean enabled, boolean closed, int thermal,
            double started, double finished, double latestContextTime,
            long source, long revision, long generation,
            long currentSource, long currentRevision, long currentGeneration,
            boolean modelEligible) {
        return enabled && !closed && modelEligible && thermal < 2 && source > 0 && revision > 0
            && source == currentSource && revision == currentRevision && generation == currentGeneration
            && Double.isFinite(started) && Double.isFinite(finished) && Double.isFinite(latestContextTime)
            && finished >= started && finished - started <= 2
            && finished >= latestContextTime && finished - latestContextTime < .15;
    }
}
