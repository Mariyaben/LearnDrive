package ai.learndrive.mobile

import ai.learndrive.mobile.StartRoutine.Part
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StartRoutineTest {
    private fun steps(manual:Boolean,level:CoachingProfile,language:String="en")=StartRoutine.steps(language,manual,level)
    private fun words(l:List<StartRoutine.Step>)=l.sumOf {it.text.split(' ').size}
    /** Spoken time at about 2.5 words a second, plus the time given to do each step. */
    private fun seconds(l:List<StartRoutine.Step>)=words(l)/2.5+l.sumOf {it.pause}

    @Test fun guidedManualSaysTheMostImportantStepsInOrder() {
        val s=steps(true,CoachingProfile.GUIDED)
        assertEquals(listOf("Before you start","First gear","Look, then move off"),s.map {it.title})
        assertEquals(listOf(Part.CHECKS,Part.MOVE_OFF,Part.MOVE_OFF),s.map {it.part})
        val text=s.joinToString(" ") {it.text}
        for(must in listOf("seat belt","mirrors","handbrake on","neutral","first gear","biting point","right shoulder","release the handbrake"))
            assertTrue(must,text.contains(must))
        // Prepare, observe, then move: the handbrake comes off only after the look over the shoulder.
        assertTrue(text.indexOf("neutral")<text.indexOf("first gear"))
        assertTrue(text.indexOf("first gear")<text.indexOf("biting point"))
        assertTrue(text.indexOf("biting point")<text.indexOf("right shoulder"))
        assertTrue(text.indexOf("right shoulder")<text.indexOf("release the handbrake"))
    }

    @Test fun anAutomaticHasNoClutchAndUsesParkAndDrive() {
        for(level in CoachingProfile.entries) {
            val text=steps(false,level).joinToString(" ") {it.text}
            assertFalse(text.contains("clutch",ignoreCase=true));assertFalse(text.contains("neutral"));assertFalse(text.contains("first gear"))
        }
        val text=steps(false,CoachingProfile.GUIDED).joinToString(" ") {it.text}
        assertTrue(text.contains("in P"));assertTrue(text.contains("select D"))
        // Foot on the brake before selecting a gear; the handbrake off after the look.
        assertTrue(text.indexOf("on the brake")<text.indexOf("select D"))
        assertTrue(text.indexOf("right shoulder")<text.indexOf("release the handbrake"))
    }

    @Test fun briefAtEveryLevelWithTheMostImportantStepsKept() {
        for(manual in listOf(true,false)) {
            val guided=steps(manual,CoachingProfile.GUIDED);val balanced=steps(manual,CoachingProfile.BALANCED);val minimal=steps(manual,CoachingProfile.MINIMAL)
            assertEquals(3,guided.size);assertEquals(2,balanced.size);assertEquals(2,minimal.size)
            assertTrue(words(guided)>words(balanced));assertTrue(words(balanced)>words(minimal))
            // Said in full before every drive it ran to about two minutes; now about half a minute at most.
            assertTrue("guided takes ${seconds(guided)} s",seconds(guided)<=35)
            assertTrue(seconds(balanced)<=20);assertTrue(seconds(minimal)<=12)
            for(l in listOf(guided,balanced,minimal)) {
                assertEquals(Part.CHECKS,l.first().part);assertEquals(Part.MOVE_OFF,l.last().part)
                val text=l.joinToString(" ") {it.text}
                // Always: the seat belt and mirrors, and the look over the right shoulder (traffic keeps left).
                assertTrue(text.contains("eat belt"));assertTrue(text.contains("mirrors"));assertTrue(text.contains("right shoulder"))
            }
            // Guided and Balanced: the handbrake on before the engine, and off only when it is clear.
            for(l in listOf(guided,balanced))assertTrue(l.joinToString(" ") {it.text}.let {it.contains("handbrake on")&&it.contains("release the handbrake")})
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

    @Test fun theHandbookHasTheWholeRoutine() {
        val sections=Handbook.sections
        assertEquals(listOf("Before you start the engine","Moving off in a manual car","Moving off in an automatic","On the road: what the coach asks","Where this comes from"),
            sections.map {it.title})
        val text=sections.flatMap {it.lines}.joinToString(" ")
        // Everything the spoken checks leave out is here.
        for(must in listOf("Doors","Head restraint","Steering wheel","Seat belt","Mirrors","neutral","warning lights","biting point","blind spot",
            "Signal right","release the handbrake","foot fully off the clutch","until the car is moving","stalls","select D","creeps"))
            assertTrue(must,text.contains(must))
        // The coach's advice for each skill, in the brief's own words.
        val road=sections.first {it.title.startsWith("On the road")}.lines
        assertEquals(DriveFocus.Topic.entries.size,road.size)
        assertTrue(road[0].startsWith("Hills: Before a climb"));assertTrue(road[0].contains("descent"))
    }
}
