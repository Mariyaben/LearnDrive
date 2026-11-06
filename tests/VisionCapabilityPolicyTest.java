package ai.learndrive.mobile;

public final class VisionCapabilityPolicyTest {
    private static void check(boolean condition,String message) {if(!condition)throw new AssertionError(message);}
    public static void main(String[] args) {
        VisionCapabilityPolicy policy=new VisionCapabilityPolicy();
        check(policy.spatialEnabled()&&policy.profile().equals("STANDARD")&&!policy.advancedAvailable(),"unmeasured Advanced claimed");
        for(double sample:new double[]{Double.NaN,Double.POSITIVE_INFINITY,-1})policy.completedSpatial(sample);
        check(policy.samples()==0,"invalid duration counted");
        policy.completedSpatial(250);policy.completedSpatial(300);policy.completedSpatial(249);
        check(policy.spatialEnabled(),"nonconsecutive samples disabled spatial");
        policy.completedSpatial(250);policy.completedSpatial(250);policy.completedSpatial(250);
        check(!policy.spatialEnabled()&&policy.profile().equals("BASIC"),"slow spatial work still blocks semantic frames");
        check(policy.samples()==6&&policy.slowSamples()==5,"timing counters differ from completed stages");
        check(policy.readiness(SpatialReadiness.READY)==SpatialReadiness.BASIC_SEMANTIC_ONLY,"fallback not exposed");
        for(int readiness:new int[]{SpatialReadiness.MOUNT_UNSUPPORTED,SpatialReadiness.GRAVITY_UNAVAILABLE,SpatialReadiness.INTRINSICS_UNAVAILABLE})
            check(policy.readiness(readiness)==readiness,"Basic capability bypasses a pose guard");
        policy.completedSpatial(10);check(!policy.spatialEnabled(),"fallback flaps without restart");
        VisionCapabilityPolicy retry=new VisionCapabilityPolicy();check(retry.spatialEnabled(),"explicit new instance cannot retry");
        retry.spatialUnavailable();check(!retry.spatialEnabled()&&retry.failures()==1,"real spatial failure stops semantic fallback");
        check(retry.readiness(SpatialReadiness.MOUNT_UNSUPPORTED)==SpatialReadiness.MOUNT_UNSUPPORTED,"failure bypasses mount guard");
        System.out.println("Vision capability policy checks passed");
    }
}
