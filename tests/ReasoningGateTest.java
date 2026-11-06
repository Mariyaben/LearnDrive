import ai.learndrive.mobile.ReasoningGate;
public class ReasoningGateTest {
    static void check(boolean value) {if(!value)throw new AssertionError();}
    static boolean gate(boolean enabled,boolean closed,int thermal,double finish,double latest,long source,long revision,long generation,boolean eligible) {
        return ReasoningGate.current(enabled,closed,thermal,1,finish,latest,1,2,3,source,revision,generation,eligible);
    }
    public static void main(String[] args) {
        check(gate(true,false,0,1.5,1.49,1,2,3,true));
        check(!gate(false,false,0,1.5,1.49,1,2,3,true));
        check(!gate(true,true,0,1.5,1.49,1,2,3,true));
        check(!gate(true,false,2,1.5,1.49,1,2,3,true));
        check(!gate(true,false,0,3.1,3.09,1,2,3,true));
        check(!gate(true,false,0,1.5,1,1,2,3,true));
        check(!gate(true,false,0,1.5,1.6,1,2,3,true));
        check(!gate(true,false,0,1.5,1.49,2,2,3,true));
        check(!gate(true,false,0,1.5,1.49,1,3,3,true));
        check(!gate(true,false,0,1.5,1.49,1,2,4,true));
        check(!gate(true,false,0,1.5,1.49,1,2,3,false));
        check(!gate(true,false,0,Double.NaN,1.49,1,2,3,true));
        System.out.println("12 reasoning source/revision/generation/clock/thermal/cancellation gates passed");
    }
}
