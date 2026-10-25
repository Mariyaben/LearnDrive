package ai.learndrive.mobile;

/** A bounded user-confirmed start request; this class never establishes level ground
 * or replaces DriveSetup/camera admission. Changing source or leaving the foreground cancels it. */
public final class LevelReferenceStart {
    public static final int INACTIVE=0, WAITING=1, READY=2, CANCELLED=3;
    private long source;
    private double requestedAt;
    private boolean pending;
    private boolean requireReference;
    public synchronized boolean request(double time,long sourceId) {
        return request(time,sourceId,true);
    }
    public synchronized boolean request(double time,long sourceId,boolean levelRequired) {
        if(pending||!Double.isFinite(time)||time<0||sourceId<0)return false;
        source=sourceId;requestedAt=time;requireReference=levelRequired;pending=true;return true;
    }
    public synchronized int poll(double time,long sourceId,boolean foregroundRoad,
            boolean moving,boolean healthy,boolean referencePending,boolean referenceAllowed,boolean cameraEligible) {
        if(!pending)return INACTIVE;
        if(!Double.isFinite(time)||time<requestedAt||time-requestedAt>=20||sourceId!=source||
                !foregroundRoad||moving||!healthy||(requireReference&&!referencePending&&!referenceAllowed)) {
            pending=false;return CANCELLED;
        }
        if((!requireReference||(!referencePending&&referenceAllowed))&&cameraEligible){pending=false;return READY;}
        return WAITING;
    }
    public synchronized boolean isPending(){return pending;}
    public synchronized boolean requiresReference(){return requireReference;}
    public synchronized void cancel(){pending=false;}
}
