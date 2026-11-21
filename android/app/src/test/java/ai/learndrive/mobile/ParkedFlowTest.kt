package ai.learndrive.mobile

import ai.learndrive.mobile.DriveFocus.Outcome
import ai.learndrive.mobile.DriveFocus.Past
import ai.learndrive.mobile.DriveFocus.Topic
import ai.learndrive.mobile.ParkedFlow.Step
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ParkedFlowTest {
    private fun past(topic:Topic,at:Long,vararg outcomes:Pair<Outcome,Int>)=Past(topic,outcomes.toMap(),at)

    @Test fun firstOpenAsksSixQuestionsInOrder() {
        assertEquals(listOf(Step.WELCOME,Step.LANGUAGE,Step.NAME,Step.CAR,Step.LEVEL,Step.MOUNT),Step.entries)
        assertNull(Step.WELCOME.previous);assertEquals(Step.LANGUAGE,Step.WELCOME.next)
        assertEquals(Step.LEVEL,Step.MOUNT.previous);assertNull(Step.MOUNT.next)
        assertEquals(6,Step.MOUNT.number)
    }

    @Test fun experienceSetsTheCoachingLevel() {
        assertEquals(CoachingProfile.GUIDED,ParkedFlow.Experience.STARTING.profile)
        assertEquals(CoachingProfile.BALANCED,ParkedFlow.Experience.SOME.profile)
        assertEquals(CoachingProfile.MINIMAL,ParkedFlow.Experience.NEARLY.profile)
        // Each level has exactly one answer, so the question always shows the learner's current level.
        for(profile in CoachingProfile.entries)assertEquals(profile,ParkedFlow.Experience.of(profile).profile)
        assertEquals(CoachingProfile.entries.toSet(),ParkedFlow.LEVELS.toSet())
        assertEquals(CoachingProfile.GUIDED,ParkedFlow.LEVELS.first())
    }

    @Test fun whoIsDrivingIsAskedOncePerDriveOnlyWithSeveralLearners() {
        assertFalse(ParkedFlow.askWhoIsDriving(1,false))
        assertTrue(ParkedFlow.askWhoIsDriving(2,false))
        assertFalse(ParkedFlow.askWhoIsDriving(2,true))
    }

    @Test fun developerToolsNeedSevenQuickTaps() {
        val unlock=ParkedFlow.DeveloperUnlock()
        assertEquals(6,unlock.tap(0.0))
        for(i in 1..5)unlock.tap(i*1.0)
        assertEquals(0,unlock.tap(6.0))
        // A pause starts the count again.
        assertEquals(6,unlock.tap(7.0))
        assertEquals(5,unlock.tap(8.0))
        assertEquals(6,unlock.tap(20.0))
    }

    @Test fun lessCoachingIsSuggestedAfterThreeGoodDrivesAtTheLevel() {
        val good=listOf(past(Topic.BENDS,10,Outcome.BEND_BEFORE to 3),past(Topic.HILLS,20,Outcome.CLIMB_STEADY to 2,Outcome.DESCENT_BUILT to 1),
            past(Topic.TURNS,30,Outcome.TURN_EARLY to 4))
        assertEquals(CoachingProfile.BALANCED,ParkedFlow.nextLevel(CoachingProfile.GUIDED,good,0))
        assertEquals(CoachingProfile.MINIMAL,ParkedFlow.nextLevel(CoachingProfile.BALANCED,good,0))
        assertNull("already the least coaching",ParkedFlow.nextLevel(CoachingProfile.MINIMAL,good,0))
        // Drives from before the level was set do not count.
        assertNull(ParkedFlow.nextLevel(CoachingProfile.GUIDED,good,15))
        // Fewer than two thirds as asked, or too little measured, in any of the last three: no hint.
        assertNull(ParkedFlow.nextLevel(CoachingProfile.GUIDED,good+past(Topic.BENDS,40,Outcome.BEND_BEFORE to 1,Outcome.BEND_IN to 1),0))
        assertNull(ParkedFlow.nextLevel(CoachingProfile.GUIDED,good+past(Topic.TURNS,40,Outcome.TURN_EARLY to 1),0))
        assertNull(ParkedFlow.nextLevel(CoachingProfile.GUIDED,good.take(2),0))
    }

    @Test fun anAllDriveCountsOnceForTheHint() {
        fun all(at:Long)=listOf(past(Topic.BENDS,at,Outcome.BEND_BEFORE to 2).copy(all=true),past(Topic.TURNS,at,Outcome.TURN_EARLY to 2).copy(all=true))
        assertNull("two drives, not four",ParkedFlow.nextLevel(CoachingProfile.GUIDED,all(1)+all(2),0))
        assertEquals(CoachingProfile.BALANCED,ParkedFlow.nextLevel(CoachingProfile.GUIDED,all(1)+all(2)+all(3),0))
    }

    @Test fun progressSaysHowEachSkillWentLastTime() {
        val history=listOf(past(Topic.HILLS,1,Outcome.CLIMB_STEADY to 1),past(Topic.BENDS,2),
            past(Topic.HILLS,3,Outcome.CLIMB_STEADY to 2,Outcome.DESCENT_BUILT to 1))
        assertEquals(listOf("Hills · 2 drives · last time 2 of 3 as the coach asks","Bends · 1 drive · last time nothing measured"),
            ParkedFlow.skillLines(history))
        assertTrue(ParkedFlow.skillLines(emptyList()).isEmpty())
    }
}
