package ai.learndrive.mobile

import android.app.ActivityManager
import android.content.Context
import android.net.Uri
import android.os.Process
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/** Explicit experimental shadow mode. Neural decisions never call audio/accept.
 * One worker, no queued summaries, pinned GGUF/tokenizer, background load/decode. */
class PretrainedShadow(context:Context,private val core:NativeCore,private val status:(String)->Unit):AutoCloseable {
    private val app=context.applicationContext
    private val preferences=app.getSharedPreferences("pretrained_shadow",Context.MODE_PRIVATE)
    private val worker=Executors.newSingleThreadExecutor {r->Thread({Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);r.run()},"local-reasoning-shadow")}
    private val busy=AtomicBoolean(false)
    private val closed=AtomicBoolean(false)
    private val handle=AtomicLong(0)
    private val handleLock=Any()
    private val folder=File(app.filesDir,"intelligence")
    private val model=File(folder,"model.gguf")
    private val alphabet="ABCEFGHI"
    private val systemPrompt=app.assets.open("intelligence-choice-system.txt").bufferedReader().use {it.readText().trim()}
    @Volatile var enabled=false;private set
    @Volatile private var lastKey=""
    private var lastOffer=Double.NEGATIVE_INFINITY
    @Volatile private var loaded=false
    private var consecutiveSlow=0
    val canOffer get()=enabled&&!closed.get()&&!busy.get()
    private val allowed=mapOf(
        "9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031" to 0,
        "48ab3034d0dd401fbc721eb1df3217902fee7dab9078992d66431f09b7750201" to 1)

    private fun digest(file:File):String {
        val hash=MessageDigest.getInstance("SHA-256")
        file.inputStream().use {input->val buffer=ByteArray(1024*1024);while(true){val n=input.read(buffer);if(n<0)break;hash.update(buffer,0,n)}}
        return hash.digest().joinToString(""){"%02x".format(it)}
    }
    @Synchronized fun setEnabled(value:Boolean) {
        if(closed.get())return
        enabled=value&&!closed.get();lastKey=""
        if(enabled)consecutiveSlow=0
        if(!enabled){cancelModel();worker.execute {unload()};status("Local AI shadow off")}
        else status(if(model.isFile)"Local AI shadow enabled · never speaks" else "Import a supported local model first")
        // Intentionally session-scoped opt-in; never silently re-enable on launch.
    }
    @Synchronized fun importModel(uri:Uri) {
        if(closed.get()||!busy.compareAndSet(false,true)){status("Local model worker is busy");return}
        worker.execute {
            val temp=File(folder,"import.partial")
            try {
                setEnabled(false);folder.mkdirs()
                require(folder.usableSpace>1400L*1024*1024){"At least1.4GB free storage is needed"}
                app.contentResolver.openInputStream(uri).use {input->requireNotNull(input);temp.outputStream().use {out->
                    val buffer=ByteArray(1024*1024);var count=0L
                    while(true){if(closed.get())error("Import cancelled");val n=input.read(buffer);if(n<0)break;count+=n;require(count<=700L*1024*1024){"Model exceeds the supported size"};out.write(buffer,0,n)}
                }}
                val sha=digest(temp);require(sha in allowed){"Unsupported model hash. Import the pinned Qwen3-0.6B Q8 or SmolLM2-360M Q8"}
                unload();require(temp.renameTo(model)){"Could not store the model"}
                preferences.edit().putString("sha256",sha).apply()
                status("Verified local model imported · shadow remains off")
            }catch(error:Exception){temp.delete();status("Model import: ${error.message}")}
            finally {busy.set(false)}
        }
    }
    private fun cancelModel()=synchronized(handleLock){handle.get().takeIf {it!=0L}?.let {PretrainedNative.cancel(it)}}
    private fun unload(){val previous=synchronized(handleLock){handle.getAndSet(0)};if(previous!=0L)PretrainedNative.close(previous);loaded=false}
    private fun loadIfNeeded():Boolean {
        if(loaded)return true
        if(!PretrainedNative.linked||!model.isFile){status("Compatible local runtime/model unavailable; native coaching continues");return false}
        if(app.getSystemService(android.os.PowerManager::class.java).isPowerSaveMode||app.getSystemService(android.os.BatteryManager::class.java).getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY) in 0..19){status("AI shadow needs battery above20% with Battery Saver off");return false}
        val memory=ActivityManager.MemoryInfo();app.getSystemService(ActivityManager::class.java).getMemoryInfo(memory)
        if(memory.lowMemory||memory.availMem<2L*1024*1024*1024){status("Not enough available memory for AI shadow; native coaching continues");return false}
        val sha=digest(model);val kind=allowed[sha]?:run {status("Local model integrity check failed");return false}
        val h=PretrainedNative.create();if(h==0L)return false
        synchronized(handleLock){if(closed.get()){PretrainedNative.close(h);return false};handle.set(h)}
        val started=now()
        if(!PretrainedNative.load(h,model.absolutePath,kind)){status("Local model: ${PretrainedNative.error(h)}");unload();return false}
        loaded=true;Log.i("LearnDriveReasoning",JSONObject().put("event","model_loaded").put("load_ms",(now()-started)*1000).put("sha256",sha).put("live_authority",false).toString())
        return true
    }
    @Synchronized fun offer(encoded:String,thermal:Int) {
        if(!enabled||closed.get())return
        if(thermal>=2){cancelModel();if(busy.compareAndSet(false,true))worker.execute {try{unload()}finally{busy.set(false)}};status("Local AI shadow paused while warm");return}
        val t=now();if(t-lastOffer<1.5)return
        val scene=runCatching {JSONObject(encoded)}.getOrNull()?:return
        if(!scene.optBoolean("model_eligible")||(scene.optJSONArray("candidates")?.length()?:0)==0)return
        val key="${scene.optLong("source_id")}:${scene.optLong("revision")}:${scene.optLong("generation")}"
        if(key==lastKey||!busy.compareAndSet(false,true))return
        lastKey=key;lastOffer=t
        worker.execute {
            try {
                if(!enabled||closed.get()||!loadIfNeeded())return@execute
                val current=JSONObject(core.temporalContext())
                if(!current.optBoolean("model_eligible")||current.optLong("source_id")!=scene.optLong("source_id")||current.optLong("revision")!=scene.optLong("revision"))return@execute
                // Loading can take seconds. Use a new native snapshot after load,
                // never the old pre-load observation as current model evidence.
                val workScene=current
                val candidates=workScene.optJSONArray("candidates")?:JSONArray()
                if(candidates.length()==0)return@execute
                val choices=JSONObject().put("S","SILENCE").put("D","DEFER")
                var letters="SD"
                for(i in 0 until minOf(8,candidates.length())){val letter=alphabet[i].toString();choices.put(letter,candidates.getJSONObject(i));letters+=letter}
                val events=JSONArray();val originalEvents=workScene.getJSONArray("events")
                for(i in 0 until minOf(8,originalEvents.length())){val event=JSONObject();val source=originalEvents.getJSONObject(i)
                    for(key in listOf("space","id","scope","category","state","fresh","observations","relevant_region","supported_path","optical_closing","coached","image_delta"))event.put(key,source.get(key));events.put(event)}
                val compact=JSONObject().put("vehicle",workScene.getJSONObject("vehicle")).put("navigation",workScene.getJSONObject("navigation"))
                    .put("events",events).put("history",workScene.getJSONArray("history")).put("choices",choices)
                val started=now();val token=PretrainedNative.choose(handle.get(),compact.toString(),letters,5000,systemPrompt)
                val latest=JSONObject(core.temporalContext());val finished=now();val elapsed=(finished-started)*1000
                val thermalNow=app.getSystemService(android.os.PowerManager::class.java).currentThermalStatus
                val valid=token.length==1&&token in letters&&ReasoningGate.current(enabled,closed.get(),thermalNow,started,finished,latest.optDouble("time",Double.NaN),
                    workScene.optLong("source_id"),workScene.optLong("revision"),workScene.optLong("generation"),
                    latest.optLong("source_id"),latest.optLong("revision"),latest.optLong("generation"),latest.optBoolean("model_eligible"))
                val chosen=if(token.length==1&&alphabet.contains(token[0]))candidates.optJSONObject(alphabet.indexOf(token[0])) else null
                Log.i("LearnDriveReasoning",JSONObject().put("event","shadow_proposal").put("source_id",workScene.optLong("source_id"))
                    .put("revision",workScene.optLong("revision")).put("generation",workScene.optLong("generation"))
                    .put("neural_choice",token).put("wall_ms",elapsed).put("current_and_within_budget",valid)
                    .put("decision",if(chosen!=null)if(chosen.optBoolean("urgent"))"WARN" else "COACH" else if(token=="D")"DEFER" else "SILENCE")
                    .put("instruction_id",chosen?.optInt("instruction_id")?:JSONObject.NULL)
                    .put("metadata_source","trusted allowlist binding; no generated voice")
                    .put("live_authority",false).put("spoken",false).toString())
                status(if(valid)"Local AI shadow evaluated context · native coaching controls speech" else "Late or changed AI context discarded · native coaching continues")
                if(elapsed>2000)consecutiveSlow++ else consecutiveSlow=0
                if(consecutiveSlow>=3){enabled=false;unload();status("Local AI exceeded its budget repeatedly · native coaching continues")}
            }catch(error:Exception){status("AI shadow unavailable · native coaching continues")}
            finally {busy.set(false)}
        }
    }
    fun cancel(){lastKey="";cancelModel()}
    @Synchronized fun pause(){if(closed.get())return;cancel();worker.execute {unload()}}
    @Synchronized override fun close(){if(!closed.compareAndSet(false,true))return;enabled=false;cancel();worker.execute {unload()};worker.shutdown()}
}
