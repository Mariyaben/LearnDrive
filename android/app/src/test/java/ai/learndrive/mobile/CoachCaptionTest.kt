package ai.learndrive.mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

/** The caption under the coach card: what was just said, for as long as it still stands. */
class CoachCaptionTest {
    @Test fun aLateCameraClearsOnlyWhatTheCameraSaid() {
        for(origin in listOf(1,4,7)) {
            val c=CoachCaption();c.accept("Person ahead. Brake gently.",1,10.0,3.0,null,origin)
            c.clearCamera();assertNull("origin $origin",c.visible(11.0,1,true))
        }
        // Navigation (5), road preparation such as map hills (3), driving (2) and gear advice (6) stay.
        for(origin in listOf(2,3,5,6)) {
            val c=CoachCaption();c.accept("In 100 metres, turn left.",1,10.0,3.0,null,origin)
            c.clearCamera();assertEquals("origin $origin","In 100 metres, turn left.",c.visible(11.0,1,true))
        }
    }

    @Test fun aCaptionExpiresAndNeverOutlivesItsSource() {
        val c=CoachCaption();c.accept("Change down to second gear",1,10.0,2.0,null,6)
        assertNotNull(c.visible(14.9,1,true))
        assertNull(c.visible(15.1,1,true))                       // 5 s minimum on screen, then gone
        c.accept("Change down to second gear",1,20.0,2.0,null,6)
        assertNull(c.visible(21.0,2,true))                       // another camera/session source
        c.accept("Change down to second gear",1,30.0,2.0,null,6)
        assertNull(c.visible(31.0,1,false))                      // coaching not running
    }

    @Test fun theContextNamesWhereTheCueCameFrom() {
        val c=CoachCaption();c.accept("In 50 metres, turn right.",1,10.0,3.0,null,5)
        assertEquals("Recent navigation cue · 2s ago",c.presentation(12.4,1,true)?.context)
        c.accept("Person on the left.",1,20.0,3.0,null,7)
        assertEquals("Recent coach cue · 0s ago",c.presentation(20.5,1,true)?.context)
    }
}
