package ai.learndrive.mobile

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class GuidanceTraceTest {
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
