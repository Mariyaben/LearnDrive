package ai.learndrive.mobile;

public final class LevelReferenceStartTest {
    private static int passed;
    private static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static void test(String name,Runnable block){block.run();passed++;System.out.println("PASS "+name);}
    private static LevelReferenceStart request(){LevelReferenceStart q=new LevelReferenceStart();check(q.request(1,5),"request rejected");return q;}
    private static int poll(LevelReferenceStart q,double t,long source,boolean foreground,boolean moving,boolean healthy,boolean pending,boolean level,boolean camera){return q.poll(t,source,foreground,moving,healthy,pending,level,camera);}
    public static void main(String[] args){
        test("no implicit start",()->check(poll(new LevelReferenceStart(),1,5,true,false,true,false,true,true)==LevelReferenceStart.INACTIVE,"unrequested start"));
        test("waits for explicit new reference and eligible camera",()->{
            LevelReferenceStart q=request();check(poll(q,2,5,true,false,true,true,false,true)==LevelReferenceStart.WAITING,"started before reference");
            check(poll(q,6,5,true,false,true,false,true,false)==LevelReferenceStart.WAITING,"stale camera admitted");
            check(poll(q,6.1,5,true,false,true,false,true,true)==LevelReferenceStart.READY,"eligible request lost");
            check(poll(q,6.2,5,true,false,true,false,true,true)==LevelReferenceStart.INACTIVE,"request started twice");
        });
        test("source change cancels rather than following another camera",()->check(poll(request(),2,6,true,false,true,true,false,false)==LevelReferenceStart.CANCELLED,"source switch retained request"));
        test("background and movement cancel permanently",()->{
            for(int i=0;i<2;i++){LevelReferenceStart q=request();check(poll(q,2,5,i!=0,i==1,true,true,false,false)==LevelReferenceStart.CANCELLED,"unsafe context retained request");
                check(poll(q,7,5,true,false,true,false,true,true)==LevelReferenceStart.INACTIVE,"silently resumed");}
        });
        test("camera failure and expired reference cancel",()->{
            check(poll(request(),2,5,true,false,false,true,false,false)==LevelReferenceStart.CANCELLED,"failed camera retained request");
            check(poll(request(),2,5,true,false,true,false,false,false)==LevelReferenceStart.CANCELLED,"expired reference retained request");
        });
        test("timeout and backward clocks cannot start later",()->{
            check(poll(request(),21,5,true,false,true,false,true,true)==LevelReferenceStart.CANCELLED,"timeout accepted");
            check(poll(request(),.9,5,true,false,true,false,true,true)==LevelReferenceStart.CANCELLED,"backward time accepted");
            check(poll(request(),Double.NaN,5,true,false,true,false,true,true)==LevelReferenceStart.CANCELLED,"NaN accepted");
        });
        test("cancel and duplicate requests do not extend confirmation",()->{
            LevelReferenceStart q=request();check(!q.request(19,5),"duplicate extended timeout");q.cancel();check(!q.isPending(),"cancel failed");
            check(poll(q,6,5,true,false,true,false,true,true)==LevelReferenceStart.INACTIVE,"cancelled request started");
        });
        test("invalid requests are rejected",()->{LevelReferenceStart q=new LevelReferenceStart();check(!q.request(Double.NaN,5)&&!q.request(-1,5)&&!q.request(1,-1),"invalid request accepted");});
        test("explicit basic start waits for fresh evidence without claiming a reference",()->{
            LevelReferenceStart q=new LevelReferenceStart();check(q.request(1,5,false),"basic request rejected");check(!q.requiresReference(),"basic start unexpectedly required a reference");
            check(poll(q,2,5,true,false,true,false,false,false)==LevelReferenceStart.WAITING,"basic start admitted stale camera");
            check(poll(q,3,5,true,false,true,false,false,true)==LevelReferenceStart.READY,"eligible basic start blocked");
        });
        System.out.println("PASS "+passed+" level-reference start tests");
    }
}
