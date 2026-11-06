package ai.learndrive.mobile;

public final class DriveSetupTest {
    private static int passed;
    private static void check(boolean ok,String reason) {if(!ok)throw new AssertionError(reason);}
    private static void test(String name,Runnable run) {run.run();passed++;System.out.println("PASS "+name);}
    private static boolean motion(DriveSetup setup,double time,double roll,double pitch,double speed,boolean measured) {
        double r=Math.toRadians(roll),p=Math.toRadians(pitch);
        return setup.update(time,1,true,true,Math.sin(r)*Math.cos(p),Math.cos(r)*Math.cos(p),Math.sin(p),speed,measured);
    }
    public static void main(String[] args) {
        test("automatic normalization never confirms level ground",()->{
            DriveSetup setup=new DriveSetup();motion(setup,1,0,0,0,true);
            check(!setup.levelAllowed(1)&&setup.referenceId()==0,"automatic normalization became a level reference");
            check(!setup.isMonitoring(),"session started without mount confirmation");
        });
        test("explicit level reference requires a new normalization and fresh motion",()->{
            DriveSetup setup=new DriveSetup();check(setup.requestLevelReference(5),"request rejected");
            setup.update(1,5,true,true,0,1,0,0,true);check(!setup.levelAllowed(5),"old calibration reused");
            setup.update(2,6,true,false,0,0,0,0,true);check(!setup.levelAllowed(6),"stale motion confirmed reference");
            check(!setup.start(2,0,1,0),"started while reference was pending");
            setup.update(3,6,true,true,0,1,0,0,true);check(setup.levelAllowed(6)&&!setup.referencePending(),"new reference not accepted");
            check(setup.start(3,0,1,0),"cannot start after normalization");
            check(!setup.requestLevelReference(6),"re-zeroed grade during a session");
        });
        test("sensor reset and automatic reacquisition expire confirmed reference",()->{
            DriveSetup setup=new DriveSetup();setup.requestLevelReference(0);
            motion(setup,1,0,0,0,true);check(setup.levelAllowed(1),"fixture reference missing");
            setup.update(2,1,false,false,0,0,0,0,false);check(!setup.levelAllowed(1),"lost normalization retained reference");
            setup.update(8,2,true,true,0,1,0,0,true);check(!setup.levelAllowed(2),"automatic reacquisition claimed level ground");
        });
        test("lost fresh motion expires reference even before native generation changes",()->{
            DriveSetup setup=new DriveSetup();setup.requestLevelReference(0);motion(setup,1,0,0,0,true);
            setup.update(2,1,true,false,0,0,0,0,false);check(!setup.levelAllowed(1),"stale motion retained confirmed reference");
            motion(setup,3,0,0,0,true);check(!setup.levelAllowed(1),"fresh motion silently restored expired confirmation");
        });
        test("sustained stopped roll change requires checking mount",()->{
            DriveSetup setup=new DriveSetup();setup.start(0,0,1,0);
            for(int i=1;i<21;i++)check(!motion(setup,i*.1,20,0,0,true),"check triggered too early");
            check(motion(setup,2.2,20,0,0,true),"sustained roll change missed");
            check(setup.state()==DriveSetup.CHECK_MOUNT&&!setup.isMonitoring(),"coaching stayed active");
            check(setup.start(2.3,0,1,0),"explicit recheck cannot restart session");
        });
        test("unknown or manually set zero speed cannot prove stopped",()->{
            DriveSetup setup=new DriveSetup();setup.start(0,0,1,0);
            for(int i=1;i<=40;i++)check(!motion(setup,i*.1,25,0,0,false),"unknown speed triggered mount check");
            check(setup.isMonitoring(),"unknown speed stopped coaching");
        });
        test("moving vehicle roll does not trigger the stopped check",()->{
            DriveSetup setup=new DriveSetup();setup.start(0,0,1,0);
            for(int i=1;i<=40;i++)check(!motion(setup,i*.1,25,0,30,true),"moving vehicle flagged as changed mount");
        });
        test("hills with unchanged roll do not trigger mount check",()->{
            DriveSetup setup=new DriveSetup();setup.start(0,0,1,0);
            for(int i=1;i<=40;i++)check(!motion(setup,i*.1,0,25,0,true),"pitch-only hill flagged as mount roll");
        });
        test("brief bumps and data gaps do not accumulate a false dwell",()->{
            DriveSetup setup=new DriveSetup();setup.start(0,0,1,0);
            for(int i=1;i<=15;i++)motion(setup,i*.1,20,0,0,true);
            motion(setup,1.6,0,0,0,true);
            for(int i=17;i<=30;i++)check(!motion(setup,i*.1,20,0,0,true),"separate movements accumulated");
            check(!motion(setup,5,20,0,0,true),"sensor gap counted as observed roll");
            check(!motion(setup,5.1,20,0,0,true),"gap dwell did not restart");
        });
        test("landscape wrap and invalid pose cannot cause mount checks",()->{
            DriveSetup setup=new DriveSetup();double r=Math.toRadians(179);setup.start(0,Math.sin(r),Math.cos(r),0);
            for(int i=1;i<=40;i++)check(!motion(setup,i*.1,-179,0,0,true),"roll wrap interpreted as a full rotation");
            check(!setup.update(5,1,true,false,Double.NaN,1,0,0,true),"invalid pose caused a check");
        });
        test("session end clears confirmation and cannot silently resume",()->{
            DriveSetup setup=new DriveSetup();setup.requestLevelReference(0);motion(setup,1,0,0,0,true);
            setup.start(1,0,1,0);check(!setup.start(1.1,0,1,0),"repeated start silently re-anchored mount");
            setup.stop();motion(setup,2,0,0,0,true);
            check(!setup.isMonitoring()&&!setup.levelAllowed(1),"session confirmation survived end/suspension");
            check(!setup.start(Double.NaN,0,1,0)&&!setup.start(1,0,1,0),"invalid/backward start time accepted");
        });
        test("optional reference can be skipped without blocking basic coaching",()->{
            DriveSetup setup=new DriveSetup();setup.requestLevelReference(0);setup.skipLevelReference();
            check(!setup.referencePending()&&!setup.levelAllowed(1),"skip left a reference active");
            check(setup.start(1,0,0,0),"missing optional motion blocked basic session");
        });
        test("late automatic normalization cannot satisfy an old level confirmation",()->{
            DriveSetup setup=new DriveSetup();setup.requestLevelReference(1);
            setup.update(1,1,false,false,0,0,0,0,false);
            setup.update(16,2,true,true,0,1,0,0,true);
            check(!setup.referencePending()&&!setup.levelAllowed(2),"old level confirmation accepted after timeout");
        });
        System.out.println(passed+" setup/reference/mount-consistency tests passed");
    }
}
