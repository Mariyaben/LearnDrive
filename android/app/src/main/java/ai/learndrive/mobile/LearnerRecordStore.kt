package ai.learndrive.mobile

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * "You first": what the learner's measured speed did about road users (slowed before the
 * instruction, after it, or not at all), per kind of situation, kept between drives in
 * app-private storage only. Never exported with diagnostics; removed with the app's data or
 * [forget]. The native rule decides from it how the coach speaks (ld_learner_* in c_api.h).
 */
class LearnerRecordStore(dir:File,private val core:NativeCore) {
    private val file=File(dir,"learner-record.json")

    fun load() {
        val json=runCatching {JSONObject(file.readText())}.getOrNull()?:return
        val skills=json.optJSONArray("skills")?:return
        for(i in 0 until minOf(skills.length(),SKILLS)) {
            val s=skills.optJSONObject(i)?:continue
            val list=s.optJSONArray("outcomes")?:continue
            core.learnerRestore(i,s.optInt("drives"),IntArray(minOf(list.length(),30)) {list.optInt(it)})
        }
    }

    fun save() {
        val skills=JSONArray()
        for(i in 0 until SKILLS) {
            val values=core.learnerOutcomes(i) // drives, then outcomes oldest first
            val list=JSONArray();for(k in 1 until values.size)list.put(values[k])
            skills.put(JSONObject().put("drives",if(values.isEmpty())0 else values[0]).put("outcomes",list))
        }
        val tmp=File(file.parentFile,file.name+".tmp")
        tmp.writeText(JSONObject().put("version",1).put("skills",skills).toString())
        if(!tmp.renameTo(file)){file.delete();tmp.renameTo(file)}
    }

    fun forget() {file.delete();core.learnerForget()}

    companion object {const val SKILLS=4}
}
