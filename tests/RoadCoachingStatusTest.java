package ai.learndrive.mobile;

public final class RoadCoachingStatusTest {
    private static int passed;
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static void test(String name, Runnable run) {
        run.run(); passed++; System.out.println("PASS " + name);
    }
    private static RoadCoachingStatus.State state(double now, double capture, double completed,
                                                   int readiness, boolean spatialCurrent) {
        return RoadCoachingStatus.assess(now, 7, 7, capture, completed, true, true, 0, null,
            readiness, spatialCurrent);
    }
    public static void main(String[] args) {
        test("road evidence expires at the native capture-age boundary", () -> {
            check(RoadCoachingStatus.currentEvidence(10.349, 7, 7, 10, 10.1, true, 0, null),
                "fresh completed semantics unavailable");
            check(!RoadCoachingStatus.currentEvidence(10.351, 7, 7, 10, 10.1, true, 0, null),
                "old boxes still look like current road evidence");
            check(!RoadCoachingStatus.currentEvidence(.35, 7, 7, 0, .1, true, 0, null),
                "exact 350 ms boundary must expire");
            check(CameraVisionHealth.canDisplay(11, 10, 10.9, 0, null),
                "the diagnostic retention contract changed");
            check(!RoadCoachingStatus.currentEvidence(11, 7, 7, 10, 10.9, true, 0, null),
                "diagnostic retention was used for live coaching");
        });
        test("recent spatial completion cannot renew retained semantic boxes", () -> {
            RoadCoachingStatus.State state = state(12, 10, 11.99, SpatialReadiness.READY, true);
            check(!state.currentEvidence, "completion time renewed a two-second-old capture");
            check(state.status.contains("no current road assessment"), "lost freshness is hidden");
            check(!state.status.contains("active"), "stalled scene is reported as active");
        });
        test("source transitions and stopped cameras discard current road evidence", () -> {
            check(!RoadCoachingStatus.currentEvidence(10.1, 8, 7, 10, 10.05, true, 0, null),
                "previous session boxes survived a source transition");
            check(!RoadCoachingStatus.currentEvidence(10.1, 7, 7, 10, 10.05, false, 0, null),
                "stopped camera has a current road assessment");
        });
        test("corrupt or future times fail closed", () -> {
            for (double invalid : new double[]{Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY}) {
                check(!RoadCoachingStatus.currentEvidence(invalid, 7, 7, 10, 10.05, true, 0, null), "invalid UI clock");
                check(!RoadCoachingStatus.currentEvidence(10.1, 7, 7, invalid, 10.05, true, 0, null), "invalid capture");
                check(!RoadCoachingStatus.currentEvidence(10.1, 7, 7, 10, invalid, true, 0, null), "invalid completion");
            }
            check(!RoadCoachingStatus.currentEvidence(10.1, 7, 7, 10, 10.2, true, 0, null), "future completion");
            check(!RoadCoachingStatus.currentEvidence(10.1, 7, 7, 10, 9.9, true, 0, null), "completion before capture");
        });
        test("thermal pause and camera errors override apparently fresh results", () -> {
            for (int thermal = 4; thermal <= 6; thermal++) {
                RoadCoachingStatus.State paused = RoadCoachingStatus.assess(10.1, 7, 7, 10, 10.05,
                    true, true, thermal, null, SpatialReadiness.READY, true);
                check(!paused.currentEvidence && paused.status.contains("paused"), "critical heat appears operational");
            }
            RoadCoachingStatus.State failed = RoadCoachingStatus.assess(10.1, 7, 7, 10, 10.05,
                true, true, 0, "model failed", SpatialReadiness.READY, true);
            check(!failed.currentEvidence && failed.status.contains("unavailable"), "model failure hidden by prior boxes");
        });
        test("semantic-first evidence does not promise uncompleted distance analysis", () -> {
            RoadCoachingStatus.State semantic = state(10.1, 10, 10.05, SpatialReadiness.READY, false);
            check(semantic.currentEvidence, "fresh semantic-first detections wait for spatial inference");
            check(semantic.status.contains("distance estimates unavailable"), "mount readiness treated as completed spatial evidence");
            RoadCoachingStatus.State missing = state(10.1, 10, 10.05, SpatialReadiness.INTRINSICS_UNAVAILABLE, true);
            check(missing.status.contains("distance estimates unavailable"), "missing optics promise distances");
            RoadCoachingStatus.State mount = state(10.1, 10, 10.05, SpatialReadiness.MOUNT_UNSUPPORTED, false);
            check(mount.idleCaption.contains("only when parked"), "mount recovery encourages adjusting while moving");
        });
        test("camera availability and spoken coaching availability are distinct", () -> {
            RoadCoachingStatus.State silent = RoadCoachingStatus.assess(10.1, 7, 7, 10, 10.05,
                true, false, 0, null, SpatialReadiness.READY, true);
            check(silent.currentEvidence, "voice initialization hides otherwise current boxes");
            check(silent.status.contains("spoken coaching paused"), "unavailable speech looks like normal silence");
        });
        test("normal silence never supplies an all-clear or speed instruction", () -> {
            RoadCoachingStatus.State normal = state(10.1, 10, 10.05, SpatialReadiness.READY, true);
            check(normal.status.equals("Road detection active"), "normal detection state unclear");
            check(normal.idleCaption.contains("Silence is not an all-clear"), "quiet scene implies a safe road");
            check(!normal.idleCaption.contains("maintain speed"), "quiet scene authorizes a driving action");
            RoadCoachingStatus.State warm = RoadCoachingStatus.assess(10.1, 7, 7, 10, 10.05,
                true, true, 2, null, SpatialReadiness.READY, true);
            check(warm.currentEvidence && warm.status.contains("warm"), "warm but current evidence is misrepresented");
        });
        System.out.println(passed + " road-coaching status tests passed");
    }
}
