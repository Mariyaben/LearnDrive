package ai.learndrive.mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/** The parked controls (destination, Setup, start) lock only while the car is clearly moving. */
class MovingLockTest {
    private val tick=.05   // the drive screen checks every 50 ms

    /** Feeds [speed] (km/h, null = no speed) from [from] to [to] s; returns the times the lock changed. */
    private fun MovingLock.drive(from:Double,to:Double,speed:(Double)->Double?):List<Double> {
        val changes=ArrayList<Double>();var t=from;var before=moving
        while(t<to-1e-9){val v=speed(t);update(t,v!=null,v?:Double.NaN);if(moving!=before){changes+=t;before=moving};t+=tick}
        return changes
    }

    @Test fun aParkedCarWithWanderingSpeedNeverLocks() {
        // What a parked phone reads: 0-4.5 km/h wandering (weak GPS, engine idling, the phone in a
        // hand), and now and then a jump to 5-9 km/h that lasts up to a second. 0.9.6 locked on each >1 km/h.
        val rng=Random(7)
        for(run in 0 until 20) {
            val lock=MovingLock();var level=1.0;var spikeUntil=-1.0;var spike=0.0
            val changes=lock.drive(0.0,600.0) {t->
                level=(level+rng.nextDouble(-.4,.4)).coerceIn(0.0,4.5)
                // One bad fix at a time: each jump lasts until the next fix, a second later at most.
                if(t>spikeUntil+1&&rng.nextDouble()<.004){spikeUntil=t+rng.nextDouble(.2,1.2);spike=rng.nextDouble(5.0,9.0)}
                if(t<=spikeUntil)spike else level
            }
            assertEquals("run $run",emptyList<Double>(),changes)
        }
    }

    @Test fun pullingAwayLocksWithinAFewSecondsAndStaysLocked() {
        // 1.5 m/s² (5.4 km/h each second) from rest at 5 s, up to 30 km/h.
        val lock=MovingLock()
        val changes=lock.drive(0.0,60.0) {t->if(t<5)0.0 else minOf(30.0,(t-5)*5.4)}
        assertEquals(1,changes.size)
        // 6 km/h is reached 1.1 s after pulling away; locked 1.5 s later.
        assertEquals(5+6/5.4+MovingLock.MOVING_HOLD_S,changes[0],.11)
        assertTrue(lock.moving)
    }

    @Test fun creepingAtWalkingPaceLocks() {
        val lock=MovingLock()
        assertEquals(1,lock.drive(0.0,10.0) {7.0}.size)
        assertTrue(lock.moving)
    }

    @Test fun aShortStopKeepsItLockedAndParkingUnlocks() {
        val lock=MovingLock();lock.drive(0.0,20.0) {30.0}
        assertTrue(lock.moving)
        // Two seconds at a junction, then on again: still locked.
        assertEquals(emptyList<Double>(),lock.drive(20.0,40.0) {t->if(t<22)0.0 else 30.0})
        // Parked: unlocked after three seconds stopped.
        val changes=lock.drive(40.0,50.0) {1.5}
        assertEquals(1,changes.size)
        assertEquals(40+MovingLock.STOPPED_HOLD_S,changes[0],.06)
        assertFalse(lock.moving)
    }

    @Test fun noSpeedNeverLocksAndUnlocksAMovingCar() {
        val lock=MovingLock()
        assertEquals(emptyList<Double>(),lock.drive(0.0,30.0) {null})
        lock.drive(30.0,40.0) {40.0};assertTrue(lock.moving)
        lock.drive(40.0,44.0) {null};assertFalse(lock.moving)
    }

    @Test fun aPauseInUpdatesStartsTheHoldAgain() {
        val lock=MovingLock()
        lock.update(0.0,true,20.0)
        assertFalse(lock.update(60.0,true,20.0))      // a minute in the background is not 60 s of driving
        assertFalse(lock.update(61.0,true,20.0))      // 1.0 s of evidence since
        lock.drive(61.0+tick,62.0) {20.0}
        assertTrue(lock.moving)
    }

    @Test fun aBrokenClockChangesNothing() {
        val lock=MovingLock()
        assertFalse(lock.update(Double.NaN,true,50.0))
        assertFalse(lock.update(Double.POSITIVE_INFINITY,true,50.0))
        assertFalse(lock.update(1.0,true,Double.NaN))
        assertFalse(lock.update(1.1,true,-5.0))
    }
}
