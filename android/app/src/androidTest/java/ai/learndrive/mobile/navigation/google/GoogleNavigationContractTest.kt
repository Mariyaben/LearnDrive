package ai.learndrive.mobile.navigation.google

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.android.libraries.mapsplatform.turnbyturn.model.Maneuver
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Pure adapter contracts: no network, route requests, simulator or terms acceptance. */
@RunWith(AndroidJUnit4::class)
class GoogleNavigationContractTest {
    @Test fun anOldServiceBindingCannotPublishOrDetachANewerSession() {
        var oldCalls=0;var newCalls=0
        GoogleNavigationFeed.attach(101) {_,_->oldCalls++}
        val oldBinding=GoogleNavigationFeed.ownerToken()
        GoogleNavigationFeed.publish(oldBinding,null,1.0)
        GoogleNavigationFeed.detach(oldBinding)
        GoogleNavigationFeed.attach(102) {_,_->newCalls++}
        try {
            GoogleNavigationFeed.publish(oldBinding,null,2.0)
            GoogleNavigationFeed.detach(oldBinding)
            GoogleNavigationFeed.publish(102,null,2.1)
            assertEquals(1,oldCalls);assertEquals(1,newCalls)
        }finally {GoogleNavigationFeed.detach(102)}
    }

    @Test fun rampRoundaboutForkAndUturnAreNotOrdinaryTurns() {
        assertEquals(1,normalizeGoogleManeuver(Maneuver.TURN_LEFT).nativeValue)
        assertEquals(2,normalizeGoogleManeuver(Maneuver.TURN_SHARP_RIGHT).nativeValue)
        assertEquals(3,normalizeGoogleManeuver(Maneuver.TURN_KEEP_LEFT).nativeValue)
        assertEquals(4,normalizeGoogleManeuver(Maneuver.ON_RAMP_LEFT).nativeValue)
        assertEquals(4,normalizeGoogleManeuver(Maneuver.OFF_RAMP_RIGHT).nativeValue)
        assertEquals(6,normalizeGoogleManeuver(Maneuver.TURN_U_TURN_CLOCKWISE).nativeValue)
        assertEquals(7,normalizeGoogleManeuver(Maneuver.ROUNDABOUT_SHARP_LEFT_CLOCKWISE).nativeValue)
        assertEquals(0,normalizeGoogleManeuver(Int.MAX_VALUE).nativeValue)
    }
}
