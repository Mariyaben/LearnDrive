package ai.learndrive.mobile;

public final class AudioInterruptionPolicyTest {
    private static void check(boolean value,String message) {if(!value)throw new AssertionError(message);}
    public static void main(String[] args) {
        check(AudioInterruptionPolicy.mayReplace(true,false,2,false),"validated urgent person/rider cannot preempt routine terrain");
        check(!AudioInterruptionPolicy.mayReplace(false,false,2,false),"ordinary person phrase acquired WARN authority");
        check(!AudioInterruptionPolicy.mayReplace(false,true,1,true),"ordinary terrain preempted a validated WARN with a quiet legacy phrase");
        check(!AudioInterruptionPolicy.mayReplace(true,false,5,false),"safe attention phrase preempted critical legacy warning");
        check(!AudioInterruptionPolicy.mayReplace(true,true,1,false),"equal warning bypassed existing warning policy");
        check(AudioInterruptionPolicy.mayReplace(true,true,1,true),"higher-priority validated warning could not interrupt");
        check(AudioInterruptionPolicy.mayReplace(false,false,2,true),"ordinary legacy policy changed");
        System.out.println("Audio interruption policy checks passed");
    }
}
