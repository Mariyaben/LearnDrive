package ai.learndrive.mobile

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test

/** Explicit Desk input exercises JNI coordination only. It never supplies road
 * sensor evidence, starts a supervised session or plays a spoken clip. */
class NativeCoachingContractTest {
    @Test fun focusRefusalWithEnabledVoiceDoesNotAcknowledgeEncounter() {
        val audio=AudioWorker(InstrumentationRegistry.getInstrumentation().targetContext,{_,_->true},55) {}
        try {
            val end=android.os.SystemClock.elapsedRealtime()+15000
            while(!audio.ready&&android.os.SystemClock.elapsedRealtime()<end)Thread.sleep(20)
            assertTrue(audio.ready)
            audio.requestFocus={android.media.AudioManager.AUDIOFOCUS_REQUEST_FAILED}
            NativeCore().use {core->
                core.configureCoaching(CoachingSettings(voiceEnabled=true))
                val decision=urgent(core)
                assertTrue(decision.eligible)
                assertNull(core.deliverCoaching(decision,audio))
                assertEquals(0.0,core.coachingDiagnostics(now())[12],0.0)
                assertEquals(0.0,core.coachingDiagnostics(now())[13],0.0)
                assertEquals(1,org.json.JSONObject(core.coachingDiagnosticsJson(now())).getInt("submission_failed"))
            }
        }finally {audio.close()}
    }
    private fun urgent(core:NativeCore):GuidanceDecision {
        val t=now();core.beginSource(1,CaptureSource.SIMULATION,t)
        val measured=Telemetry(t,30.0,0.0,0.0,1.0,true,true,true,false,false,false)
        return core.guidance(measured,t,1,true,false,true,false,true,t,.5,.9,true)
    }
    @Test fun mutedUnreadyPcmStillAcceptsVisualCueAndSettingsRetirePendingCue() {
        val audio=AudioWorker(InstrumentationRegistry.getInstrumentation().targetContext,{_,_->true},55) {}
        audio.setVoiceEnabled(false)
        try {
            NativeCore().use {core->
                core.configureCoaching(CoachingSettings(voiceEnabled=false))
                val decision=urgent(core)
                assertTrue("Logical eligibility cannot depend on audio_ready=false",decision.eligible)
                val delivery=core.deliverCoaching(decision,audio)
                assertNotNull("Fresh logical cue should be accepted with voice muted",delivery)
                assertFalse(delivery!!.voiceSubmitted)
                val counts=core.coachingDiagnostics(now())
                assertEquals(1.0,counts[12],0.0);assertEquals(0.0,counts[13],0.0);assertEquals(0.0,counts[14],0.0)
                assertNull("A logical cue can be accepted only once",core.deliverCoaching(decision,audio))
            }
            NativeCore().use {core->
                core.configureCoaching(CoachingSettings(voiceEnabled=false))
                val old=urgent(core);assertTrue(old.eligible)
                core.configureCoaching(CoachingSettings(CoachingProfile.GUIDED,false,2))
                assertNull("Changed preference revision must retire queued decisions immediately",core.deliverCoaching(old,audio))
                assertEquals(0.0,core.coachingDiagnostics(now())[12],0.0)
                core.close()
                assertEquals("Closed core must retire posted source-bound work",0L,core.sourceId)
                assertNull("A closed native engine cannot deliver an old source",core.deliverCoaching(old,audio))
            }
        }finally {audio.close()}
    }
}
