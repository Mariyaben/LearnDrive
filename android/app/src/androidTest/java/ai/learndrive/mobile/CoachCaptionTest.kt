package ai.learndrive.mobile

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CoachCaptionTest {
    private fun route(id:Double=7.0,progress:Double=20.0,valid:Boolean=true)=DoubleArray(17).also {
        it[0]=id;it[5]=progress;it[6]=50.0;it[10]=if(valid)1.0 else 0.0
    }
    @Test fun captionsExpireAndDoNotReturnAfterSourceChanges() {
        val caption=CoachCaption();caption.accept("Give space",2,10.0,2.0)
        assertEquals("Give space",caption.visible(14.9,2,true))
        assertNull(caption.visible(15.0,2,true));assertNull(caption.visible(14.0,2,true))
        caption.accept("Give space",2,20.0,2.0)
        assertNull(caption.visible(20.1,3,true));assertNull(caption.visible(20.2,2,true))
        caption.accept("Give space",2,30.0,2.0)
        assertNull(caption.visible(30.1,2,false))
        caption.accept("Ease off",2,40.0,2.0)
        caption.clear() // An interrupted instruction must leave the screen too.
        assertNull(caption.visible(40.1,2,true))
    }
    @Test fun navigationCaptionsRetireWhenPassedReroutedOrPositionIsLost() {
        val caption=CoachCaption()
        caption.accept("Left turn ahead",2,10.0,4.0,route())
        assertEquals("Left turn ahead",caption.visible(11.0,2,true,route(progress=60.0)))
        assertNull(caption.visible(11.1,2,true,route(progress=65.0)))
        caption.accept("Left turn ahead",2,20.0,4.0,route())
        assertNull(caption.visible(20.1,2,true,route(id=8.0)))
        caption.accept("Left turn ahead",2,30.0,4.0,route())
        assertNull(caption.visible(30.1,2,true,route(valid=false)))
    }
    @Test fun providerTurnWithoutSpatialProgressUsesValidityAndExpiry() {
        val context=route(progress=Double.POSITIVE_INFINITY)
        val caption=CoachCaption()
        caption.accept("Prepare for a left turn",2,10.0,4.0,context)
        assertEquals("Prepare for a left turn",caption.visible(11.0,2,true,context))
        assertNull(caption.visible(11.1,2,true,route(valid=false)))
        caption.accept("Prepare for a left turn",2,20.0,4.0,context)
        assertNull(caption.visible(26.0,2,true,context))
        caption.accept("Prepare for a left turn",2,30.0,4.0,context)
        caption.clearNavigation();assertNull(caption.visible(30.1,2,true,context))
        caption.accept("Give the pedestrian space",2,40.0,4.0)
        caption.clearNavigation()
        assertEquals("Give the pedestrian space",caption.visible(40.1,2,true))
    }
    @Test fun acceptedCuesShowTheirEvidenceAndAgeInsteadOfImplyingCurrentAssessment() {
        val caption=CoachCaption()
        caption.accept("Give the pedestrian space",2,10.0,4.0,origin=4)
        val recent=caption.presentation(12.8,2,true)
        assertEquals("Give the pedestrian space",recent?.text)
        assertEquals("Recent camera cue · 2s ago",recent?.context)
        caption.accept("Steep uphill",2,20.0,4.0,origin=3)
        assertEquals("Recent road preparation cue · 0s ago",caption.presentation(20.2,2,true)?.context)
        assertNull("Evidence context must expire together with the caption",caption.presentation(26.0,2,true))
        caption.accept("Prepare for a left turn",2,30.0,4.0,route(),5)
        assertEquals("Recent navigation cue · 1s ago",caption.presentation(31.0,2,true,route())?.context)
        assertNull("A lost route cannot leave a recent instruction or context",caption.presentation(31.1,2,true,route(valid=false)))
        caption.accept("Right bend ahead",2,40.0,4.0,origin=3)
        assertEquals("Recent road preparation cue · 0s ago",caption.presentation(40.2,2,true)?.context)
    }
}
