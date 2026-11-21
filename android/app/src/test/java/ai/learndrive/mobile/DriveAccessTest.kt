package ai.learndrive.mobile

import ai.learndrive.mobile.DriveAccess.Decision
import ai.learndrive.mobile.DriveAccess.Paid
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Starting a drive: paid, or a renewal not yet confirmed for want of signal. Never stopping one. */
class DriveAccessTest {
    private val day=24L*60*60*1000
    private val until=100*day

    @Test fun offUntilBillingExists() {
        assertEquals(Decision.NOT_REQUIRED,DriveAccess.decide(false,until*2,until*2,null))
        assertTrue(Decision.NOT_REQUIRED.allowed)
    }

    @Test fun noSubscriptionNoStart() {
        val d=DriveAccess.decide(true,until,until,null)
        assertEquals(Decision.NOT_SUBSCRIBED,d);assertFalse(d.allowed)
    }

    @Test fun paidUpToTheLastMoment() {
        val paid=Paid(until,until-10*day)
        assertEquals(Decision.PAID,DriveAccess.decide(true,until-1,0,paid))
    }

    @Test fun aRenewalWithoutSignalHasThreeDays() {
        // Last confirmed before the paid time ran out: it has probably renewed, but there has been no signal to check.
        val paid=Paid(until,until-day)
        assertEquals(Decision.RENEWAL_UNCONFIRMED,DriveAccess.decide(true,until,0,paid))
        assertEquals(Decision.RENEWAL_UNCONFIRMED,DriveAccess.decide(true,until+DriveAccess.RENEWAL_GRACE_MS-1,0,paid))
        assertEquals(Decision.LAPSED,DriveAccess.decide(true,until+DriveAccess.RENEWAL_GRACE_MS,0,paid))
    }

    @Test fun aConfirmedLapseHasNoGrace() {
        val paid=Paid(until,until+60_000)
        assertEquals(Decision.LAPSED,DriveAccess.decide(true,until+120_000,0,paid))
    }

    @Test fun settingTheClockBackRestoresNothing() {
        val paid=Paid(until,until-day)
        // The phone has shown the app well after the grace; the clock now says the subscription is current.
        val seen=until+10*day
        assertEquals(Decision.LAPSED,DriveAccess.decide(true,until-5*day,seen,paid))
        // Nor can a check be backdated by the clock: one made after the end confirms the lapse.
        assertEquals(Decision.LAPSED,DriveAccess.decide(true,until-5*day,0,Paid(until,until+day)))
    }
}
