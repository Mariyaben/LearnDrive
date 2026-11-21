package ai.learndrive.mobile

import ai.learndrive.mobile.DriveFocus.Outcome
import ai.learndrive.mobile.DriveFocus.Past
import ai.learndrive.mobile.DriveFocus.Topic
import ai.learndrive.mobile.DriveFocus.Why
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** "All": today's focus on every skill, as the coach was before drives had one focus. */
class DriveFocusAllTest {
    private val hilly=DriveFocus.Offer(true,climbs=2,descents=1,bends=7,turns=5)

    @Test fun theBriefNamesEverySkillAndWhatTheRouteHolds() {
        val choice=DriveFocus.Choice.everything()
        assertTrue(choice.all);assertEquals(Topic.entries,choice.topics)
        assertTrue(DriveFocus.brief(choice,hilly,"en").startsWith(
            "Today's focus is everything: hills, bends, turns and road users. Your route has two climbs, a descent, seven bends and five turns."))
        assertTrue(DriveFocus.brief(choice,hilly,"ml").contains("നിങ്ങളുടെ റൂട്ടിൽ രണ്ട് കയറ്റങ്ങളും ഒരു ഇറക്കവും ഏഴ് വളവുകളും അഞ്ച് തിരിവുകളും ഉണ്ട്."))
        assertEquals("Today's focus: everything",DriveFocus.card(choice,hilly).first)
        // Counted only when known for the whole route; without a destination, it says what can be measured.
        val part=DriveFocus.Offer(true,climbs=2,descents=0,hillsKm=3,bends=4,bendsKm=3,turns=2)
        assertTrue(!DriveFocus.brief(choice,part,"en").contains("Your route has"))
        assertTrue(DriveFocus.brief(choice,DriveFocus.Offer(false),"en").contains("Without a destination, the review can only measure road users"))
        // A single word count is said plainly in Malayalam, without "-ഉം".
        assertTrue(DriveFocus.brief(choice,DriveFocus.Offer(true,climbs=0,descents=0,bends=0,turns=3),"ml").contains("നിങ്ങളുടെ റൂട്ടിൽ മൂന്ന് തിരിവുകൾ ഉണ്ട്."))
    }

    @Test fun everySkillIsMeasuredReviewedAndKeptAsOneDrive() {
        val drive=DriveFocus.Session(DriveFocus.Choice.everything(),hilly).finish(intArrayOf(3,1,0,0,0,0,0,4,0,0,0))
        assertTrue(drive.all);assertEquals(Topic.entries,drive.topics);assertEquals("everything",drive.title)
        val review=DriveFocus.review(drive)
        for(name in listOf("HILLS","BENDS","TURNS","ROAD USERS"))assertTrue(name,review.contains(name+"\n"))
        assertTrue(review.contains("You slowed before being told: 3 of 4"))
        assertTrue(review.contains("Next time: "))
        // Kept for the learner: each skill that had something measured, marked as one "All" drive.
        val kept=drive.past(42)
        assertEquals(listOf(Topic.ROAD_USERS),kept.map {it.topic});assertTrue(kept.all {it.all&&it.at==42L})
        assertEquals(kept.first(),Past.decode(kept.first().encode()))
        // A one-skill drive is reviewed as before.
        val one=DriveFocus.Session(DriveFocus.Choice(Topic.ROAD_USERS,Why.FIRST),hilly).finish(intArrayOf(3,1,0))
        assertEquals(DriveFocus.review(one.results.first()),DriveFocus.review(one))
    }

    @Test fun afterAnAllDriveTheSkillThatWentWorstComesNext() {
        val all=listOf(Past(Topic.BENDS,mapOf(Outcome.BEND_BEFORE to 2,Outcome.BEND_IN to 1),7,true),
            Past(Topic.TURNS,mapOf(Outcome.TURN_EARLY to 1,Outcome.TURN_LATE to 3),7,true),
            Past(Topic.ROAD_USERS,mapOf(Outcome.USER_BEFORE to 4),7,true))
        assertEquals(1,DriveFocus.drives(all).size)
        val c=DriveFocus.choose(hilly,all,null)
        assertEquals(Topic.TURNS,c.topic);assertEquals(Why.AGAIN,c.why)
        // Nothing went more wrong than right: a skill the "All" drive did not practise.
        val good=all.map {if(it.topic==Topic.TURNS)Past(Topic.TURNS,mapOf(Outcome.TURN_EARLY to 4),7,true) else it}
        assertEquals(Topic.HILLS,DriveFocus.choose(hilly,good,null).topic)
        // One-skill drives stay one drive each, whatever their times.
        assertEquals(2,DriveFocus.drives(listOf(Past(Topic.BENDS,emptyMap(),0),Past(Topic.TURNS,emptyMap(),0))).size)
    }
}
