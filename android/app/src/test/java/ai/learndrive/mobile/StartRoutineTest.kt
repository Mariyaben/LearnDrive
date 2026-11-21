package ai.learndrive.mobile

import ai.learndrive.mobile.StartRoutine.Part
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StartRoutineTest {
    private fun steps(manual:Boolean,level:CoachingProfile,language:String="en")=StartRoutine.steps(language,manual,level)

    @Test fun guidedManualFollowsTheTaughtOrder() {
        val s=steps(true,CoachingProfile.GUIDED)
        assertEquals(listOf("Doors","Seat","Steering","Seat belt","Mirrors","Handbrake","Engine","First gear","Biting point","Look","Signal","Move off"),s.map {it.title})
        assertEquals(7,s.takeWhile {it.part==Part.CHECKS}.size)
        assertTrue(s.drop(7).all {it.part==Part.MOVE_OFF})
        val text=s.joinToString(" ") {it.text}
        for(must in listOf("doors are shut","seat belt","inside mirror","neutral","warning lights","first gear","biting point",
            "over your right shoulder","signal right","release the handbrake","foot fully off the clutch"))assertTrue(must,text.contains(must))
        // Prepare, observe, then move: the handbrake comes off only after the looks.
        assertTrue(text.indexOf("first gear")<text.indexOf("biting point"))
        assertTrue(text.indexOf("biting point")<text.indexOf("over your right shoulder"))
        assertTrue(text.indexOf("over your right shoulder")<text.indexOf("release the handbrake"))
        // The gears are disengaged before the engine is started.
        assertTrue(text.indexOf("neutral")<text.indexOf("start it"))
    }

    @Test fun anAutomaticHasNoClutchAndUsesParkAndDrive() {
        val text=steps(false,CoachingProfile.GUIDED).joinToString(" ") {it.text}
        assertFalse(text.contains("clutch",ignoreCase=true));assertFalse(text.contains("neutral"));assertFalse(text.contains("first gear"))
        assertTrue(text.contains("in P"));assertTrue(text.contains("select D"))
        // Foot on the brake before selecting a gear.
        assertTrue(text.indexOf("firmly on the brake")<text.indexOf("select D"))
        assertTrue(text.indexOf("over your right shoulder")<text.indexOf("release the handbrake"))
    }

    @Test fun theCoachingLevelSetsHowMuchIsSaid() {
        fun words(l:List<StartRoutine.Step>)=l.sumOf {it.text.split(' ').size}
        for(manual in listOf(true,false)) {
            val guided=steps(manual,CoachingProfile.GUIDED);val balanced=steps(manual,CoachingProfile.BALANCED);val minimal=steps(manual,CoachingProfile.MINIMAL)
            assertEquals(2,balanced.size);assertEquals(2,minimal.size)
            assertTrue(words(guided)>words(balanced));assertTrue(words(balanced)>words(minimal))
            for(l in listOf(guided,balanced,minimal)) {
                assertEquals(Part.CHECKS,l.first().part);assertEquals(Part.MOVE_OFF,l.last().part)
                // Traffic keeps left: every level looks over the right shoulder before moving off.
                assertTrue(l.any {it.part==Part.MOVE_OFF&&it.text.contains("right shoulder")})
            }
        }
    }

    @Test fun malayalamSaysEveryStepInMalayalamAndTheScreenStaysEnglish() {
        for(manual in listOf(true,false))for(level in CoachingProfile.entries) {
            val en=steps(manual,level,"en");val ml=steps(manual,level,"ml")
            assertEquals(en.map {it.title},ml.map {it.title})
            assertEquals(en.map {it.text},ml.map {it.text})
            assertEquals(en.map {it.pause},ml.map {it.pause})
            for(s in ml) {
                assertNotEquals(s.text,s.say)
                assertTrue(s.say,s.say.any {it in 'ഀ'..'ൿ'})
                // P and D are written as Malayalam speakers say them, so the phone's voice reads them right.
                assertFalse(s.say,s.say.any {it in 'A'..'Z'||it in 'a'..'z'})
            }
        }
        for(s in steps(true,CoachingProfile.GUIDED,"en"))assertEquals(s.text,s.say)
    }

    @Test fun everyLineIsRenderedAhead() {
        for(language in listOf("en","ml")) {
            val lines=StartRoutine.lines(language)
            assertEquals(lines.size,lines.distinct().size)
            for(manual in listOf(true,false))for(level in CoachingProfile.entries)assertTrue(lines.containsAll(steps(manual,level,language).map {it.say}))
        }
    }
}
