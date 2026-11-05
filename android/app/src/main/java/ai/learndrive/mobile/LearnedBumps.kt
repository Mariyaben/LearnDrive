package ai.learndrive.mobile

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Speed bumps this phone has felt (motion sensor), kept between drives in app-private
 * storage only. Never exported with diagnostics; removed with the app's data or [forget].
 * A bump is coached only after it was felt on two separate passes (native rule).
 */
class LearnedBumps(dir:File,private val core:NativeCore) {
    private val file=File(dir,"learned-bumps.json")
    @Volatile var count=0;private set
    @Volatile var inUse=0;private set

    fun load() {
        val json=runCatching {JSONObject(file.readText())}.getOrNull()?:return
        val list=json.optJSONArray("bumps")?:return
        var restored=0
        for(i in 0 until list.length()) {
            val b=list.optJSONObject(i)?:continue
            if(core.bumpRestore(b.optDouble("lat"),b.optDouble("lon"),b.optDouble("heading"),b.optInt("felt"),b.optInt("missed")))restored++
        }
        refreshCounts()
    }

    fun save() {
        val values=core.bumpsLearned()
        val list=JSONArray()
        for(i in 0 until values.size/5) {
            list.put(JSONObject().put("lat",values[5*i]).put("lon",values[5*i+1]).put("heading",values[5*i+2])
                .put("felt",values[5*i+3].toInt()).put("missed",values[5*i+4].toInt()))
        }
        val tmp=File(file.parentFile,file.name+".tmp")
        tmp.writeText(JSONObject().put("version",1).put("bumps",list).toString())
        if(!tmp.renameTo(file)){file.delete();tmp.renameTo(file)}
        refreshCounts(values)
    }

    fun forget() {file.delete();count=0;inUse=0}

    private fun refreshCounts(values:DoubleArray=core.bumpsLearned()) {
        val n=values.size/5;count=n
        inUse=(0 until n).count {values[5*it+3]>=2&&values[5*it+4]<values[5*it+3]}
    }
}
