package ai.learndrive.mobile

import android.content.Context
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class CoachingPreferencesTest {
    @Test fun balancedVoiceDefaultsRoundTripAndUnknownModeFallsBack() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val preferences=context.getSharedPreferences("coaching-test-${UUID.randomUUID()}",Context.MODE_PRIVATE)
        try {
            val store=CoachingPreferences(preferences)
            assertEquals(CoachingSettings(),store.load())
            for((index,mode) in CoachingProfile.entries.withIndex()) {
                val value=CoachingSettings(mode,false,index+2L);store.save(value)
                assertEquals(value,CoachingPreferences(preferences).load())
            }
            repeat(2) {
                NativeCore().use {core->
                    core.configureCoaching(CoachingPreferences(preferences).load())
                    val restored=core.coachingDiagnostics(now())
                    assertEquals("Fresh engine must restore persisted revision, not require revision one",4.0,restored[3],0.0)
                    assertEquals(2.0,restored[1],0.0);assertEquals(0.0,restored[2],0.0)
                }
            }
            preferences.edit().putString("coachingProfile","obsolete").putLong("coachingRevision",-1).commit()
            assertEquals(CoachingProfile.BALANCED,store.load().profile)
            assertFalse("Unknown profile cannot silently enable muted voice",store.load().voiceEnabled)
            assertEquals(1L,store.load().revision)
        }finally {preferences.edit().clear().commit()}
    }
}
