package ai.learndrive.mobile;

public final class SpatialReadinessTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        double r12=Math.toRadians(12),r25=Math.toRadians(25),r5=Math.toRadians(5),r15=Math.toRadians(15);
        check(SpatialReadiness.mountAdvice(1,0,0)==null,"level landscape mount flagged");
        check(SpatialReadiness.mountAdvice(Math.cos(r5),0,Math.sin(r5))==null,"slightly downward camera flagged");
        check(String.valueOf(SpatialReadiness.mountAdvice(Math.cos(r12),0,-Math.sin(r12))).contains("too high"),"camera 12 degrees up not flagged");
        check(String.valueOf(SpatialReadiness.mountAdvice(0,Math.cos(r12),-Math.sin(r12))).contains("too high"),"portrait camera up not flagged");
        check(String.valueOf(SpatialReadiness.mountAdvice(Math.cos(r25),0,Math.sin(r25))).contains("too low"),"camera 25 degrees down not flagged");
        check(String.valueOf(SpatialReadiness.mountAdvice(Math.cos(r15),Math.sin(r15),0)).contains("sideways"),"rolled mount not flagged");
        check(SpatialReadiness.mountAdvice(0,0,0)==null,"missing gravity produced advice");
        check(SpatialReadiness.evaluate(false,true,0,1,0)==SpatialReadiness.ORIGINAL_CALIBRATION_UNKNOWN,"recording inherited phone calibration");
        check(SpatialReadiness.evaluate(true,false,0,1,0)==SpatialReadiness.INTRINSICS_UNAVAILABLE,"missing optics accepted");
        for(double[] up:new double[][]{{0,0,0},{Double.NaN,1,0},{0,Double.POSITIVE_INFINITY,0},{0,10,0}})
            check(SpatialReadiness.evaluate(true,true,up[0],up[1],up[2])==SpatialReadiness.GRAVITY_UNAVAILABLE,"invalid gravity accepted");
        for(double roll:new double[]{0,Math.PI/2,Math.PI,-Math.PI/2}) {
            double pitch=.6;
            check(SpatialReadiness.evaluate(true,true,Math.sin(roll)*Math.cos(pitch),Math.cos(roll)*Math.cos(pitch),Math.sin(pitch))==SpatialReadiness.READY,"tilted landscape mount rejected");
        }
        for(double z:new double[]{-1,1}) {
            check(SpatialReadiness.evaluate(true,true,0,0,z)==SpatialReadiness.MOUNT_UNSUPPORTED,"vertical camera accepted");
            check(SpatialReadiness.evaluate(true,false,0,0,z)==SpatialReadiness.MOUNT_UNSUPPORTED,"missing optics hid a known unsupported mount");
        }
        check(SpatialReadiness.evaluate(true,true,.34,0,Math.sqrt(1-.34*.34))==SpatialReadiness.MOUNT_UNSUPPORTED,"near-vertical camera accepted");
        check(SpatialReadiness.evaluate(true,true,0,0,0)==SpatialReadiness.GRAVITY_UNAVAILABLE,"loss not reported");
        check(SpatialReadiness.evaluate(true,true,0,1,0)==SpatialReadiness.READY,"pose cannot recover");
        check(SpatialReadiness.status(SpatialReadiness.GRAVITY_UNAVAILABLE).contains("semantic detections only"),"limited capability hidden");
        System.out.println("Spatial readiness: source provenance, missing data, mounts, loss and recovery passed");
    }
}
