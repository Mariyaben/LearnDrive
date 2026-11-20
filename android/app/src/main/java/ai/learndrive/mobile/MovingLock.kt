package ai.learndrive.mobile

/**
 * Whether the car is known to be moving, for the controls that only work parked (destination,
 * Setup, starting a session). It locks once the measured speed has stayed at walking pace or more
 * for a moment, and unlocks once the car has stayed stopped (or the speed is unknown) for a moment.
 *
 * Up to 0.9.6 any reading over 1 km/h locked at once. A parked phone reads 1-4 km/h for seconds at a
 * time: GPS wanders under a roof or beside buildings, and the speed carried between fixes picks up
 * engine vibration and a hand holding the phone. The parked screen locked and unlocked many times a
 * minute, and each lock took the keyboard away and greyed out the destination, Setup and tabs.
 */
class MovingLock {
    var moving=false
        private set
    private var since=Double.NaN
    private var last=Double.NaN

    /** [kmh] is the measured speed, [valid] whether there is one. Times are seconds on one monotonic clock. */
    @Synchronized fun update(time:Double,valid:Boolean,kmh:Double):Boolean {
        if(!time.isFinite())return moving
        // Every hold needs evidence all along: a pause in updates (the app in the background) starts it again.
        if(!last.isFinite()||time<last||time-last>MAX_GAP_S)since=Double.NaN
        last=time
        val known=valid&&kmh.isFinite()&&kmh>=0
        val toward=if(moving)!known||kmh<STOPPED_KMH else known&&kmh>=MOVING_KMH
        if(!toward){since=Double.NaN;return moving}
        if(!since.isFinite())since=time
        if(time-since>=if(moving)STOPPED_HOLD_S else MOVING_HOLD_S){moving=!moving;since=Double.NaN}
        return moving
    }

    companion object {
        /** Walking pace and over. A parked phone's speed stays below it, even with poor GPS. */
        const val MOVING_KMH=6.0
        const val MOVING_HOLD_S=1.5
        const val STOPPED_KMH=3.0
        const val STOPPED_HOLD_S=3.0
        const val MAX_GAP_S=1.0
    }
}
