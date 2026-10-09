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
            check(!CameraVisionHealth.shouldAnalyze(10.3,10,3), "hot phone runs at its normal cadence");
            check(CameraVisionHealth.shouldAnalyze(11.5,10,3), "severe heat never allows another frame");
        });
        test("critical heat pauses and cooling restores analysis", () -> {
            for (int state=4;state<=6;state++) {
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
