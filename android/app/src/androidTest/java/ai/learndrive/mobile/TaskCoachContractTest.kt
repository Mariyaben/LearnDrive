package ai.learndrive.mobile

import android.content.Context
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class TaskCoachContractTest {
    @Test fun lessonIsExplicitAndDoesNotFabricateVehicleOrLearnerEvidence() {
        NativeCore().use { core ->
            assertEquals(59,core.promptCount())
            assertFalse(JSONObject(core.taskContext()).getBoolean("enabled"))
            core.junctionLesson(true)
            val state=JSONObject(core.taskContext())
            assertTrue(state.getBoolean("enabled"))
            assertEquals("WAITING_FOR_ROUTE",state.getString("phase"))
            assertFalse(core.taskSpeechAllowed())
            for(key in listOf("mirror_check","signal","position","controls"))assertEquals("UNOBSERVED",state.getString(key))
            assertEquals("UNASSESSED",state.getString("driving_outcome"))
            core.junctionLesson(false)
            assertEquals("OFF",JSONObject(core.taskContext()).getString("phase"))
        }
    }

    @Test fun recordsAreBoundedAggregateDeltasAndRetainInterruptionsWithoutSkillScores() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val prefs=context.getSharedPreferences("task-test-${UUID.randomUUID()}",Context.MODE_PRIVATE)
        try {
            val store=LearningRecordStore(prefs)
            val before=JSONObject().put("opportunities",4).put("planning_deliveries",2)
            val after=JSONObject().put("opportunities",6).put("planning_deliveries",3)
                .put("latitude",10.0).put("routeId",123).put("controls","clutch pressed").put("grade",100)
            repeat(35){store.record(true,60.0,true,before,after)}
            val raw=prefs.getString("sessions","[]")!!
            val records=JSONArray(raw)
            assertEquals(20,records.length())
            val latest=records.getJSONObject(19)
            assertEquals(2,latest.getInt("opportunities"))
            assertEquals(1,latest.getInt("planning_deliveries"))
            assertTrue(latest.getBoolean("interrupted"))
            assertEquals("UNASSESSED",latest.getString("driving_outcome"))
            for(key in listOf("latitude","routeId","controls","grade"))assertFalse(latest.has(key))
            assertTrue(store.text().contains("Interrupted practice"))
            assertTrue(store.text().contains("not a driving grade"))
            store.clear();assertEquals(0,JSONArray(prefs.getString("sessions","[]")).length())
            store.record(true,Double.NaN,false,before,after)
            assertEquals(0,JSONArray(prefs.getString("sessions","[]")).length())
        }finally {prefs.edit().clear().commit()}
    }
}
