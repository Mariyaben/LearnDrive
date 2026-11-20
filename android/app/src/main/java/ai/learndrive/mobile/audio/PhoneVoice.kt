package ai.learndrive.mobile.audio

import android.content.Context
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

/**
 * The coach's phrases in the phone's own natural text-to-speech voice, rendered once and kept as
 * PCM in app storage. Only offline voices are used (never one that needs the network), so no
 * text leaves the phone. Whole composed sentences are rendered too, so they keep natural
 * intonation instead of joined phrases. Until every phrase is rendered the bundled recordings
 * speak, so one drive never mixes two voices.
 *
 * [language] picks the voice ("en", or "ml" for Malayalam); the texts given are already in it
 * ([VoicePhrases]). Each language keeps its own rendered clips.
 */
class PhoneVoice(context:Context,override val language:String=VoicePhrases.ENGLISH,private val status:(String)->Unit):CoachVoice,AutoCloseable {
    private val folder=if(language==VoicePhrases.ENGLISH)"phone-voice" else "phone-voice-$language"
    private val directory=File(context.noBackupFilesDir,folder).apply {mkdirs()}
    private val scratch=File(context.cacheDir,folder).apply {mkdirs()}
    // Render files of a voice being replaced (language change) never collide with this one's.
    private val tag=java.lang.Long.toHexString(System.nanoTime())
    private val languageName=VoicePhrases.name(language)
    private val label=if(language==VoicePhrases.ENGLISH)"Natural voice" else "$languageName voice"
    private val io=Executors.newSingleThreadExecutor {r->Thread(r,"phone-voice")}
    private val memory=ConcurrentHashMap<String,Pcm>()
    private val requested=ConcurrentHashMap.newKeySet<String>()
    private val failures=ConcurrentHashMap<String,Int>()
    private val pending=ConcurrentHashMap<String,String>()
    private val required=ConcurrentHashMap.newKeySet<String>()
    private val counter=AtomicInteger()
    @Volatile private var tts:TextToSpeech?=null
    // The engine's init result; setup runs once both it and the engine exist (Android may report
    // the result before its constructor has returned).
    @Volatile private var initCode:Int?=null
    private var configured=false
    @Volatile private var voiceKey:String?=null
    @Volatile private var closed=false
    @Volatile private var ready=false
    @Volatile var description:String?=null;private set
    /** True when the phone has no offline voice for [language] (it can be installed, then retried). */
    @Volatile var missingVoice=false;private set
    /** True once every required phrase has a rendered clip. */
    override val complete:Boolean get() {
        if(ready)return true
        if(voiceKey==null||required.isEmpty())return false
        ready=required.all {memory.containsKey(it)}
        if(ready)status("$label ready · ${description?:"phone voice"}")
        return ready
    }

    init {
        tts=runCatching {TextToSpeech(context.applicationContext,{code->initCode=code;post {setUp()}},GOOGLE)}.getOrNull()
        if(tts==null){missingVoice=true;status(unavailable())} else post {setUp()}
    }
    private fun unavailable()=if(language==VoicePhrases.ENGLISH)"Natural voice unavailable · built-in voice"
        else "No $languageName voice on this phone · English recordings speak until it is installed"

    /** Queue work on the voice thread while this voice is open; never throws (the engine's
     * callbacks can still arrive after [close]). */
    private fun post(task:()->Unit){if(!closed)runCatching {io.execute(task)}}

    private fun setUp() {
        val code=initCode?:return
        if(tts==null||configured)return
        configured=true;configure(code)
    }

    /** Render (or load) every phrase the coach can say; the voice switches only when all exist. */
    fun require(texts:Collection<String>) {
        val usable=texts.map {it.trim()}.filter {it.isNotEmpty()}
        required.addAll(usable);usable.forEach {request(it)}
    }

    /** The rendered clip for [text], or null while it is not ready (a render is then queued). */
    override fun clip(text:String):Pcm? {
        val key=text.trim();if(key.isEmpty())return null
        memory[key]?.let {return it}
        request(key);return null
    }

    private fun request(text:String) {
        if(closed||memory.containsKey(text)||(failures[text]?:0)>=2||!requested.add(text))return
        if(voiceKey!=null)post {prepare(text)}
    }

    private fun configure(code:Int) {
        val engine=tts?:return
        if(closed)return
        if(code!=TextToSpeech.SUCCESS){missingVoice=true;status(unavailable());return}
        val voice=runCatching {choose(engine.voices.orEmpty())}.getOrNull()
        if(voice==null||engine.setVoice(voice)!=TextToSpeech.SUCCESS) {
            missingVoice=true
            status(if(language==VoicePhrases.ENGLISH)"No offline English voice on this phone · built-in voice"
                else "No offline $languageName voice on this phone · install it, English recordings speak meanwhile")
            return
        }
        engine.setSpeechRate(1f);engine.setPitch(1f)
        engine.setOnUtteranceProgressListener(object:UtteranceProgressListener() {
            override fun onStart(utteranceId:String?)=Unit
            override fun onDone(utteranceId:String?) {utteranceId?.let {id->post {finished(id,true)}}}
            @Deprecated("Deprecated in Java")
            override fun onError(utteranceId:String?) {utteranceId?.let {id->post {finished(id,false)}}}
            override fun onError(utteranceId:String?,errorCode:Int) {utteranceId?.let {id->post {finished(id,false)}}}
        })
        val engineName=if(runCatching {engine.engines.any {it.name==GOOGLE}}.getOrDefault(false))GOOGLE else engine.defaultEngine?:"default"
        description=voice.locale.displayName+(if(engineName==GOOGLE)" · Google" else "")
        voiceKey="v1|$engineName|${voice.name}"
        status("$label: preparing ${voice.locale.displayName}")
        for(text in requested)post {prepare(text)}
    }

    /** An offline voice in [language]: India first (the learners' accent), then UK, then US. */
    private fun choose(voices:Set<Voice>):Voice? {
        val usable=voices.filter {v->v.locale.language==language&&!v.isNetworkConnectionRequired&&
            !v.features.orEmpty().contains(TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED)}
        fun region(v:Voice)=when(v.locale.country){"IN"->0;"GB"->1;"US"->2;else->3}
        return usable.minWithOrNull(compareBy<Voice>({region(it)},{-it.quality},{it.latency},{it.name}))
    }

    private fun file(text:String):File {
        val digest=MessageDigest.getInstance("SHA-1").digest("$voiceKey\n$text".toByteArray(Charsets.UTF_8))
        return File(directory,digest.joinToString("") {"%02x".format(it)}+".pcm")
    }

    private fun prepare(text:String) {
        if(closed||memory.containsKey(text))return
        val engine=tts?:return
        val cached=file(text)
        if(cached.isFile)VoiceClips.read(cached)?.let {memory[text]=it;progress();return}
        val id="u$tag-${counter.incrementAndGet()}"
        pending[id]=text
        val result=runCatching {engine.synthesizeToFile(text,Bundle(),File(scratch,"$id.wav"),id)}.getOrDefault(TextToSpeech.ERROR)
        if(result!=TextToSpeech.SUCCESS){pending.remove(id);failed(text)}
    }

    private fun finished(id:String,ok:Boolean) {
        val text=pending.remove(id)?:return
        val wav=File(scratch,"$id.wav")
        try {
            val pcm=if(ok&&wav.isFile)VoiceClips.readWave(wav.readBytes())?.let(VoiceClips::polish) else null
            if(pcm==null){failed(text);return}
            memory[text]=pcm
            // Fixed phrases always persist; whole composed sentences up to a bounded count.
            if(required.contains(text)||(directory.list()?.size?:0)<required.size+400)VoiceClips.write(file(text),pcm)
            progress()
        } catch(e:Exception){failed(text)} finally {wav.delete()}
    }

    private fun failed(text:String) {failures[text]=(failures[text]?:0)+1;requested.remove(text)}

    private fun progress() {
        if(ready)return
        val done=required.count {memory.containsKey(it)}
        if(done==required.size)complete else if(done%20==0)status("$label: preparing $done of ${required.size} phrases")
    }

    override fun close() {
        if(closed)return
        closed=true
        runCatching {io.execute {runCatching {tts?.stop();tts?.shutdown()};tts=null}}
        io.shutdown()
    }

    private companion object {
        const val GOOGLE="com.google.android.tts"
    }
}
