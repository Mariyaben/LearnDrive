package ai.learndrive.mobile

import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject

/** App-owned aggregate practice records. No route, imagery, inferred controls or skill score. */
class LearningRecordStore(private val preferences:SharedPreferences) {
    private val counters=listOf("opportunities","planning_deliveries","approach_deliveries","observed_speed_reductions","unassessed_outcomes")
    @Synchronized fun clear(){preferences.edit().remove("sessions").apply()}
    @Synchronized fun record(lesson:Boolean,duration:Double,interrupted:Boolean,before:JSONObject,after:JSONObject) {
        if(!duration.isFinite()||duration<0)return
        val old=runCatching {JSONArray(preferences.getString("sessions","[]"))}.getOrDefault(JSONArray())
        val row=JSONObject().put("at",System.currentTimeMillis()).put("lesson",if(lesson)"junction_preparation" else "general_practice")
            .put("duration_seconds",duration).put("interrupted",interrupted).put("driving_outcome","UNASSESSED")
        for(key in counters)row.put(key,(after.optInt(key)-before.optInt(key)).coerceIn(0,1000000))
        val saved=JSONArray();for(i in maxOf(0,old.length()-19) until old.length())saved.put(old.get(i));saved.put(row)
        preferences.edit().putString("sessions",saved.toString()).apply()
    }
    fun text(junctionLesson:Boolean=false):String {
        val rows=runCatching {JSONArray(preferences.getString("sessions","[]"))}.getOrDefault(JSONArray())
        val out=StringBuilder("Current practice goal\n")
        out.append(if(junctionLesson)"Prepare observation, signal/position planning and approach before a junction.\n\n"
            else "Review useful, missed and distracting cues with your instructor. Select a focused lesson before the next session.\n\n")
        out.append("The app cannot verify mirror checks, signalling, position, pedal use or successful negotiation. Measured slowing is an observation, not a driving grade.\n\n")
        if(rows.length()==0)out.append("No saved practice records yet.")
        for(i in rows.length()-1 downTo maxOf(0,rows.length()-5)){
            val r=rows.optJSONObject(i)?:continue
            out.append(if(r.optBoolean("interrupted"))"Interrupted practice" else "Ended practice").append(" · ").append(r.optDouble("duration_seconds").toInt()).append(" sec\n")
            if(r.optString("lesson")=="junction_preparation")out.append("Junction opportunities: ${r.optInt("opportunities")} · planning deliveries: ${r.optInt("planning_deliveries")} · approach deliveries: ${r.optInt("approach_deliveries")} · observed speed reductions: ${r.optInt("observed_speed_reductions")}\n")
            out.append("Driving outcome unassessed. Discuss the sequence with your instructor.\n\n")
        }
        return out.toString()
    }
}
