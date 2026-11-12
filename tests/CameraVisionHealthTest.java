package ai.learndrive.mobile;

public final class CameraVisionHealthTest {
    private static int passed;
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static void test(String name, Runnable run) {
        run.run();passed++;System.out.println("PASS " + name);
    }
    private static String status(double now, double capture, double completed, int thermal, String error, boolean busy) {
        return CameraVisionHealth.status(now,capture,completed,now,10,2,thermal,error,busy);
    }
    public static void main(String[] args) {
        test("severe heat slows inference instead of stopping detections", () -> {
            check(Double.isFinite(CameraVisionHealth.intervalSeconds(3)), "severe heat silently disables inference");
            for (int state=1;state<=3;state++)
                check(CameraVisionHealth.intervalSeconds(state)>CameraVisionHealth.intervalSeconds(state-1), "heat does not reduce workload");
            check(!CameraVisionHealth.shouldAnalyze(10.15,10,3), "hot phone runs at its normal cadence");
            check(CameraVisionHealth.shouldAnalyze(10.21,10,3), "severe heat starves the 350 ms coaching gate");
            // Detector cadence plus inference must stay inside the native capture-age gate.
            check(CameraVisionHealth.intervalSeconds(3)+.1<RoadCoachingStatus.MAX_CAPTURE_AGE_SECONDS, "severe heat makes evidence stale");
            // The optional distance model (~1 core on the S24 Ultra) runs only while the phone is cool.
            check(CameraVisionHealth.spatialAllowed(0), "cool phone loses spatial inference");
            for (int state=1;state<=6;state++) check(!CameraVisionHealth.spatialAllowed(state), "heat keeps the optional spatial model");
            // Fewer camera frames once the phone is warm; the detector needs at most 8 a second.
            check(CameraVisionHealth.cameraFpsCap(0)==30&&CameraVisionHealth.cameraFpsCap(1)==30, "cool phone loses its frame rate");
            for (int state=2;state<=6;state++) check(CameraVisionHealth.cameraFpsCap(state)==15, "hot phone keeps 30 frames a second");
            check(CameraVisionHealth.intervalSeconds(0)>=1.0/20, "cool phone runs the spatial model above its 20 Hz design rate");
            check(CameraVisionHealth.zoomAllowed(0,null)&&CameraVisionHealth.zoomAllowed(0,30.0), "cool, fast phone loses the zoom pass");
            check(!CameraVisionHealth.zoomAllowed(1,30.0)&&!CameraVisionHealth.zoomAllowed(0,80.0), "zoom pass runs under heat or on a slow detector");
        });
        test("critical heat keeps the road-user detector alive, inside the coaching freshness gate", () -> {
            // Nov 8: "critical" from 15:11 silenced camera coaching for the rest of the drive.
            check(!CameraVisionHealth.paused(4)&&Double.isFinite(CameraVisionHealth.intervalSeconds(4)), "critical heat stops detection");
            check(CameraVisionHealth.intervalSeconds(4)>CameraVisionHealth.intervalSeconds(3), "critical heat does not reduce workload further");
            check(CameraVisionHealth.intervalSeconds(4)+.1<RoadCoachingStatus.MAX_CAPTURE_AGE_SECONDS, "critical heat makes evidence stale");
            check(CameraVisionHealth.shouldAnalyze(10.21,10,4)&&!CameraVisionHealth.shouldAnalyze(10.15,10,4), "critical cadence wrong");
            check(CameraVisionHealth.canDisplay(11,10.8,10.9,4,null), "critical heat hides current boxes");
            check(status(11,10.8,10.9,4,null,false).contains("very hot"), "reduced critical-heat detection is not labelled");
        });
        test("emergency heat pauses and cooling restores analysis", () -> {
            for (int state=5;state<=6;state++) {
                check(!CameraVisionHealth.shouldAnalyze(30,10,state), "critical heat still schedules inference");
                check(!CameraVisionHealth.canDisplay(11,10.8,10.9,state,null), "thermal pause shows old boxes");
                check(status(11,10.8,10.9,state,null,false).contains("paused"), "thermal pause is hidden");
            }
            check(CameraVisionHealth.shouldAnalyze(30,10,0), "cooling cannot resume analysis");
            check(status(30,29.8,29.9,0,null,false).contains("Vision active"), "cooling leaves the pause status stuck");
        });
        test("slow inference results remain visible with a delay label", () -> {
            check(CameraVisionHealth.canDisplay(11,10,10.9,0,null), "slow completed inference loses all boxes");
            check(status(11,10,10.9,0,null,false).contains("Vision delayed"), "delayed result presented as live");
            check(status(11,10,10.9,0,null,false).contains("1000 ms old"), "capture age hidden");
        });
        test("expired results are discarded even if the last result was delayed", () -> {
            check(!CameraVisionHealth.canDisplay(13,10,10.9,0,null), "old completion never expires");
            check(!CameraVisionHealth.canDisplay(14,10,13.9,0,null), "ancient capture shown as a new frame");
            check(status(13,10,10.9,0,null,true).contains("expired"), "processing stall invisible");
        });
        test("future and invalid times cannot keep detections visible", () -> {
            check(!CameraVisionHealth.canDisplay(10,10,11,0,null), "future completion displayed");
            check(!CameraVisionHealth.canDisplay(11,11,10,0,null), "completion precedes capture");
            for (double bad:new double[]{Double.NaN,Double.POSITIVE_INFINITY,Double.NEGATIVE_INFINITY}) {
                check(!CameraVisionHealth.canDisplay(bad,10,10.1,0,null), "invalid display clock");
                check(!CameraVisionHealth.shouldAnalyze(bad,10,0), "invalid scheduling clock");
                check(!CameraVisionHealth.shouldAnalyze(11,bad,0), "invalid prior-frame clock");
            }
        });
        test("inference errors remain visible and offer recovery", () -> {
            check(!CameraVisionHealth.canDisplay(11,10.8,10.9,0,"Model failed"), "fresh boxes hide inference failure");
            String failed=status(11,10.8,10.9,0,"Model failed",false);
            check(failed.contains("Model failed")&&failed.contains("Restart detection"), "model failure has no recovery message");
            check(status(12,11.8,11.9,0,null,false).contains("Vision active"), "successful recovery still reports failure");
        });
        test("camera stalls are distinguished from model loading", () -> {
            String stalled=CameraVisionHealth.status(14,-1,-1,10,10,0,0,null,false);
            check(stalled.contains("No camera frames")&&stalled.contains("Restart detection"), "frame stall hidden");
            check(CameraVisionHealth.status(14,-1,-1,10,10,0,0,null,true).contains("Loading offline models"), "slow model initialization mistaken for a frame stall");
        });
        test("zero detections are distinguished from stopped inference", () -> {
            String empty=CameraVisionHealth.status(11,10.8,10.9,11,10,0,0,null,false);
            check(empty.contains("Vision active")&&empty.contains("0 road users"), "empty scene looks like stopped inference");
            check(status(11,10.8,10.9,2,null,false).contains("reduced detection rate"), "thermal reduction hidden");
        });
        System.out.println(passed+" camera-health tests passed");
    }
}
