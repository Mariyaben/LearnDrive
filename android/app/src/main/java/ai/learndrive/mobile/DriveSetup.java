package ai.learndrive.mobile;

/** Session confirmations are independent of automatic IMU normalization.
 * A roll change is a limited consistency check, never proof of mount movement.
 */
public final class DriveSetup {
    public static final int SETUP = 0, MONITORING = 1, CHECK_MOUNT = 2;
    private int state = SETUP;
    private double anchorRoll = Double.NaN, changeSince = -1, lastUpdate = -1;
    private long referenceId, requestedAfter;
    private boolean referencePending, referenceExpired;
    private double referencePendingSince = -1;

    private static double roll(double x, double y, double z) {
        double norm = Math.sqrt(x*x+y*y+z*z);
        if (!Double.isFinite(norm) || norm < .9 || norm > 1.1 || Math.hypot(x,y)/norm < .35)
            return Double.NaN;
        return Math.atan2(x,y);
    }

    public synchronized boolean start(double time, double x, double y, double z) {
        if (state == MONITORING || referencePending || !Double.isFinite(time) || time < 0 || time < lastUpdate) return false;
        state = MONITORING;anchorRoll = roll(x,y,z);changeSince = -1;lastUpdate = time;
        return true;
    }

    public synchronized void stop() {
        state = SETUP;anchorRoll = Double.NaN;changeSince = -1;
        referenceId = 0;referencePending = referenceExpired = false;
        referencePendingSince = -1;
    }

    // Called only after the user explicitly confirms parked, level ground and
    // the native fusion is reset. An old automatic calibration cannot qualify.
    public synchronized boolean requestLevelReference(long previousId) {
        if (state == MONITORING || previousId < 0) return false;
        state = SETUP;referenceId = 0;requestedAfter = previousId;
        referencePending = true;referenceExpired = false;
        referencePendingSince = -1;
        return true;
    }

    public synchronized void skipLevelReference() {
        referenceId = 0;referencePending = referenceExpired = false;referencePendingSince = -1;
    }

    public synchronized boolean update(double time, long calibrationId, boolean calibrated,
            boolean freshMotion, double x, double y, double z,
            double measuredSpeedKmh, boolean measuredSpeedValid) {
        if (!Double.isFinite(time) || time < 0 || time <= lastUpdate) return false;
        boolean gap = lastUpdate >= 0 && time-lastUpdate > .25;
        lastUpdate = time;
        if (referencePending && referencePendingSince < 0) referencePendingSince = time;
        if (referencePending && time-referencePendingSince >= 15) {
            referencePending = false;referenceExpired = true;
        }
        if (referenceId != 0 && (!calibrated || !freshMotion || calibrationId != referenceId)) {
            referenceId = 0;referenceExpired = true;
        }
        if (referencePending && calibrated && freshMotion && calibrationId > requestedAfter) {
            referenceId = calibrationId;referencePending = referenceExpired = false;
        }
        if (state != MONITORING) return false;
        double currentRoll = freshMotion ? roll(x,y,z) : Double.NaN;
        if (!Double.isFinite(currentRoll)) {changeSince = -1;return false;}
        if (!Double.isFinite(anchorRoll)) anchorRoll = currentRoll;
        // Only measured GPS/fusion speed is eligible. Manual zero is not evidence
        // of stopping. Pitch changes on hills and yaw turns do not change roll.
        boolean stopped = measuredSpeedValid && Double.isFinite(measuredSpeedKmh) &&
            measuredSpeedKmh >= 0 && measuredSpeedKmh <= 1;
        double difference = Math.abs(Math.atan2(Math.sin(currentRoll-anchorRoll),Math.cos(currentRoll-anchorRoll)));
        if (!stopped || difference < Math.toRadians(15) || gap) {changeSince = -1;return false;}
        if (changeSince < 0) changeSince = time;
        if (time-changeSince < 2) return false;
        state = CHECK_MOUNT;referenceId = 0;referencePending = false;referenceExpired = true;
        return true;
    }

    public synchronized int state() {return state;}
    public synchronized boolean isMonitoring() {return state == MONITORING;}
    public synchronized boolean levelAllowed(long nativeCalibrationId) {
        return state != CHECK_MOUNT && referenceId > 0 && referenceId == nativeCalibrationId;
    }
    public synchronized long referenceId() {return referenceId;}
    public synchronized boolean referencePending() {return referencePending;}
    public synchronized String referenceStatus() {
        if (referencePending) return "Level reference: hold still on confirmed level ground for 5 seconds";
        if (referenceId > 0) return "Level reference set for this session; inclination remains an estimate";
        if (referenceExpired) return "Level reference expired. Set it again while parked on level ground";
        return "Level reference not set. Inclination coaching is off";
    }
}
