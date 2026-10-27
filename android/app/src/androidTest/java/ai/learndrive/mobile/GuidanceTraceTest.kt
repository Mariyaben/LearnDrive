package ai.learndrive.mobile

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class GuidanceTraceTest {
    @Test fun googleRouteContentIsOmittedWhileOfflineAndRuntimeContextRemainIntact() {
        val context=DoubleArray(17).also {it[0]=97.0;it[1]=98.0;it[5]=123.0;it[6]=456.0;it[10]=1.0;it[11]=2.0}
        val decision=GuidanceDecision(1,2,51,5,0,0,0,0,false,true,0,1.0,.1,false,false,1.1)
        fun encoded(google:Boolean)=JSONObject(GuidanceTrace.decision(decision,1.1,"Camera","measured_navigation_gps",null,"app cue",false,
            navigation=GuidanceTrace.navigationForExport(context,google)))
        assertTrue("Google maneuver/progress/distance must not be retained",encoded(true).isNull("navigation"))
        assertEquals(456.0,encoded(false).getJSONObject("navigation").getDouble("distance_m"),0.0)
        assertEquals(123.0,context[5],0.0);assertEquals(456.0,context[6],0.0)
    }
    @Test fun visualAcceptanceDoesNotInventAnAudioSubmissionOrHeardPlayback() {
        val decision=GuidanceDecision(1,2,3,4,0,0,0,0,false,true,0,1.0,.1,true,false,1.1,settingsRevision=7)
        val visual=JSONObject(GuidanceTrace.coachingAccepted(decision,1.1,false))
        assertEquals("coaching_accepted",visual.getString("event"))
        assertFalse(visual.getBoolean("voice_submitted"));assertFalse(visual.getBoolean("audible_playback_confirmed"))
        assertEquals(7L,visual.getLong("settings_revision"))
        val submitted=JSONObject(GuidanceTrace.accepted(decision,1.1))
        assertEquals("accepted",submitted.getString("event"));assertTrue(submitted.getBoolean("voice_submitted"))
        assertFalse(submitted.getBoolean("audible_playback_confirmed"))
    }
    @Test fun persistentNavigationRedactsProviderSpeedLimit() {
        val context=doubleArrayOf(1.0,2.0,3.0,1.0,12.0,15.0,20.0,30.0,40.0,91.25,1.0,2.0,3.0,50.0,25.0,0.5,4.0)
        val encoded=GuidanceTrace.navigation(context,12.5)
        val navigation=JSONObject(encoded).getJSONObject("navigation")
        assertTrue(navigation.isNull("speed_limit_kmh"))
        assertTrue(navigation.getBoolean("speed_limit_available"))
        assertFalse(encoded.contains("91.25"))
        assertFalse(navigation.has("road_name"))
        assertEquals(25.0,navigation.getDouble("gps_speed_kmh"),0.0)
        assertEquals(12.0,navigation.getDouble("fix_time"),0.0)
        assertEquals(91.25,context[9],0.0) // Persistence cannot mutate live policy input.
    }

    @Test fun unavailableProviderLimitRemainsUnavailable() {
        val context=DoubleArray(17);context[9]=Double.NaN
        val navigation=JSONObject(GuidanceTrace.navigation(context,0.0)).getJSONObject("navigation")
        assertTrue(navigation.isNull("speed_limit_kmh"))
        assertFalse(navigation.getBoolean("speed_limit_available"))
    }
}
