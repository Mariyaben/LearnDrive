package ai.learndrive.mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** "You first": the after-drive words never claim more than the measured speed supports. */
class LearnerReviewTest {
    // drive before/after/none, wait level x4, recent outcomes kept x4
    private fun summary(before:Int,after:Int,none:Int,levels:IntArray=IntArray(4),kept:IntArray=IntArray(4))=
        intArrayOf(before,after,none)+levels+kept

    @Test fun aDriveWithoutMeasuredSituationsSaysSo() {
        assertTrue(LearnerReview.drive(summary(0,0,0)).contains("No road-user situations were measured"))
        assertTrue(LearnerReview.drive(null).contains("No road-user situations were measured"))
        assertTrue(LearnerReview.drive(intArrayOf(1,2)).contains("No road-user situations were measured"))
    }

    @Test fun countsAreReportedAsObservationsOfSpeed() {
        val text=LearnerReview.drive(summary(5,9,3))
        assertTrue(text.contains("You slowed before being told: 5 of 17"))
        assertTrue(text.contains("You slowed after being told: 9"))
        assertTrue(text.contains("No slowing seen: 3"))
        assertTrue(text.contains("not a driving grade"))
        // Speed is all that is measured: no claim about safety, looking, or pedals.
        assertFalse(text.lowercase().contains("safe"))
        assertTrue(text.contains("cannot see where you looked"))
    }

    @Test fun aNewLearnerIsToldTheCoachStillSpeaksInFull() {
        assertTrue(LearnerReview.drive(summary(1,2,0)).contains("still tells you everything in full"))
        assertEquals("The coach has no record yet of how you respond to road users.",LearnerReview.status(summary(0,0,0)))
        assertEquals("",LearnerReview.status(null))
    }

    @Test fun earnedLevelsAreNamedPerKindOfSituation() {
        val text=LearnerReview.status(summary(0,0,0,intArrayOf(1,0,0,2),intArrayOf(14,3,0,30)))
        assertTrue(text.contains("Road-user situations on record: 47"))
        assertTrue(text.contains("leaves the slowing out of its instruction for: people ahead."))
        assertTrue(text.contains("stays quiet and shows on screen what it has seen for: oncoming traffic."))
        assertFalse(text.contains("two-wheelers"))
        assertFalse(text.contains("still tells you everything in full"))
    }

    @Test fun aHeldInstructionIsShownNotHidden() {
        assertEquals("Person ahead · seen. You slowed first.",LearnerReview.holdCaption(0))
        assertEquals("Oncoming traffic · seen. You slowed first.",LearnerReview.holdCaption(3))
        assertEquals("Road user ahead · seen. You slowed first.",LearnerReview.holdCaption(9))
    }
}
