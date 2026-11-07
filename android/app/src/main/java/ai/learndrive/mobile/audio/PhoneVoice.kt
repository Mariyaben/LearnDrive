package ai.learndrive.mobile.audio

import android.content.Context
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.speech.tts.Voice
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * The coach's phrases in the phone's own natural text-to-speech voice, rendered once and kept as
 * PCM in app storage. Only offline voices are used (never one that needs the network), so no
 * text leaves the phone. Whole composed sentences are rendered too, so they keep natural
 * intonation instead of joined phrases. Until every phrase is rendered the bundled recordings
 * speak, so one drive never mixes two voices.
 */
class PhoneVoice(context:Context,private val status:(String)->Unit):AutoCloseable {
    class Pcm(val bytes:ByteArray,val sampleRate:Int,val channels:Int)
    private val directory=File(context.noBackupFilesDir,"phone-voice").apply {mkdirs()}
    private val scratch=File(context.cacheDir,"phone-voice").apply {mkdirs()}
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
    /** True once every required phrase has a rendered clip. */
    val complete:Boolean get() {
        if(ready)return true
        if(voiceKey==null||required.isEmpty())return false
        ready=required.all {memory.containsKey(it)}
        if(ready)status("Natural voice ready · ${description?:"phone voice"}")
        return ready
    }

    init {
        tts=runCatching {TextToSpeech(context.applicationContext,{code->initCode=code;io.execute {setUp()}},GOOGLE)}.getOrNull()
        if(tts==null)status("Natural voice unavailable · built-in voice") else io.execute {setUp()}
    }

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
    fun clip(text:String):Pcm? {
        val key=text.trim();if(key.isEmpty())return null
        memory[key]?.let {return it}
        request(key);return null
    }

    private fun request(text:String) {
        if(closed||memory.containsKey(text)||(failures[text]?:0)>=2||!requested.add(text))return
        if(voiceKey!=null)io.execute {prepare(text)}
    }

    private fun configure(code:Int) {
        val engine=tts?:return
        if(closed)return
        if(code!=TextToSpeech.SUCCESS){status("Natural voice unavailable · built-in voice");return}
        val voice=runCatching {choose(engine.voices.orEmpty())}.getOrNull()
        if(voice==null||engine.setVoice(voice)!=TextToSpeech.SUCCESS){status("No offline English voice on this phone · built-in voice");return}
        engine.setSpeechRate(1f);engine.setPitch(1f)
        engine.setOnUtteranceProgressListener(object:UtteranceProgressListener() {
            override fun onStart(utteranceId:String?)=Unit
            override fun onDone(utteranceId:String?) {utteranceId?.let {id->io.execute {finished(id,true)}}}
            @Deprecated("Deprecated in Java")
            override fun onError(utteranceId:String?) {utteranceId?.let {id->io.execute {finished(id,false)}}}
            override fun onError(utteranceId:String?,errorCode:Int) {utteranceId?.let {id->io.execute {finished(id,false)}}}
        })
        val engineName=if(runCatching {engine.engines.any {it.name==GOOGLE}}.getOrDefault(false))GOOGLE else engine.defaultEngine?:"default"
        description=voice.locale.displayName+(if(engineName==GOOGLE)" · Google" else "")
        voiceKey="v1|$engineName|${voice.name}"
        status("Natural voice: preparing ${voice.locale.displayName}")
        for(text in requested)io.execute {prepare(text)}
    }

    /** An offline English voice: India first (the learners' accent), then UK, then US. */
    private fun choose(voices:Set<Voice>):Voice? {
        val usable=voices.filter {v->v.locale.language=="en"&&!v.isNetworkConnectionRequired&&
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
        if(cached.isFile)read(cached)?.let {memory[text]=it;progress();return}
        val id="u${counter.incrementAndGet()}"
        pending[id]=text
        val result=runCatching {engine.synthesizeToFile(text,Bundle(),File(scratch,"$id.wav"),id)}.getOrDefault(TextToSpeech.ERROR)
        if(result!=TextToSpeech.SUCCESS){pending.remove(id);failed(text)}
    }

    private fun finished(id:String,ok:Boolean) {
        val text=pending.remove(id)?:return
        val wav=File(scratch,"$id.wav")
        try {
            val pcm=if(ok&&wav.isFile)readWave(wav.readBytes())?.let(::polish) else null
            if(pcm==null){failed(text);return}
            memory[text]=pcm
            // Fixed phrases always persist; whole composed sentences up to a bounded count.
            if(required.contains(text)||(directory.list()?.size?:0)<required.size+400)write(file(text),pcm)
            progress()
        } catch(e:Exception){failed(text)} finally {wav.delete()}
    }

    private fun failed(text:String) {failures[text]=(failures[text]?:0)+1;requested.remove(text)}

    private fun progress() {
        if(ready)return
        val done=required.count {memory.containsKey(it)}
        if(done==required.size)complete else if(done%20==0)status("Natural voice: preparing $done of ${required.size} phrases")
    }

    /** Trimmed to the speech (30 ms before, 60 ms after) and levelled to the bundled clips:
     * voiced RMS about -17.5 dBFS, peaks below -1 dBFS. Mono only. */
    private fun polish(raw:Pcm):Pcm? {
        val pcm=if(raw.channels==2)downmix(raw) else raw
        if(pcm.channels!=1||pcm.bytes.size<400)return null
        val s=ShortArray(pcm.bytes.size/2);ByteBuffer.wrap(pcm.bytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(s)
        var a=0;while(a<s.size&&abs(s[a].toInt())<328)a++
        var b=s.size-1;while(b>a&&abs(s[b].toInt())<328)b--
        if(a>=b)return null
        a=maxOf(0,a-pcm.sampleRate*30/1000);b=minOf(s.size-1,b+pcm.sampleRate*60/1000)
        var sum=0.0;var n=0;var peak=1
        for(i in a..b){val v=abs(s[i].toInt());if(v>655){sum+=v.toDouble()*v;n++};if(v>peak)peak=v}
        var gain=if(n>0)0.1334*32768/sqrt(sum/n) else 1.0
        if(peak*gain>29200)gain=29200.0/peak
        val out=ByteBuffer.allocate((b-a+1)*2).order(ByteOrder.LITTLE_ENDIAN)
        for(i in a..b)out.putShort((s[i]*gain).roundToInt().coerceIn(-32768,32767).toShort())
        return Pcm(out.array(),pcm.sampleRate,1)
    }

    private fun downmix(pcm:Pcm):Pcm {
        val input=ByteBuffer.wrap(pcm.bytes).order(ByteOrder.LITTLE_ENDIAN)
        val frames=pcm.bytes.size/4;val out=ByteBuffer.allocate(frames*2).order(ByteOrder.LITTLE_ENDIAN)
        for(i in 0 until frames)out.putShort(((input.getShort(i*4).toInt()+input.getShort(i*4+2).toInt())/2).toShort())
        return Pcm(out.array(),pcm.sampleRate,1)
    }

    private fun readWave(bytes:ByteArray):Pcm? {
        if(bytes.size<44)return null
        val buffer=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        fun tag(offset:Int)=String(bytes,offset,4,Charsets.US_ASCII)
        if(tag(0)!="RIFF"||tag(8)!="WAVE")return null
        var offset=12;var rate=0;var channels=0;var bits=0;var format=0
        while(offset+8<=bytes.size) {
            val id=tag(offset);var length=buffer.getInt(offset+4);val start=offset+8
            if(id=="data") {
                // Some engines write the header before the length is known.
                if(length<=0||length>bytes.size-start)length=bytes.size-start
                if((format!=1&&format!=0xFFFE)||bits!=16||rate !in 8000..48000||channels !in 1..2)return null
                val usable=length-length%(2*channels)
                return if(usable>0)Pcm(bytes.copyOfRange(start,start+usable),rate,channels) else null
            }
            if(length<0||length>bytes.size-start)return null
            if(id=="fmt "&&length>=16){format=buffer.getShort(start).toInt() and 0xffff;channels=buffer.getShort(start+2).toInt()
                rate=buffer.getInt(start+4);bits=buffer.getShort(start+14).toInt()}
            offset=start+length+(length and 1)
        }
        return null
    }

    private fun write(target:File,pcm:Pcm) {
        val header=ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN).put(MAGIC).putInt(pcm.sampleRate).putInt(pcm.channels).array()
        val temporary=File(target.parentFile,target.name+".tmp")
        temporary.outputStream().use {it.write(header);it.write(pcm.bytes)}
        if(!temporary.renameTo(target))temporary.delete()
    }

    private fun read(source:File):Pcm? = runCatching {
        val bytes=source.readBytes()
        val buffer=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val magic=ByteArray(4).also {buffer.get(it)}
        if(!magic.contentEquals(MAGIC))return@runCatching null
        val rate=buffer.getInt();val channels=buffer.getInt()
        if(rate !in 8000..48000||channels!=1||bytes.size<=12)null else Pcm(bytes.copyOfRange(12,bytes.size),rate,channels)
    }.getOrNull()

    override fun close() {
        closed=true
        io.execute {runCatching {tts?.stop();tts?.shutdown()};tts=null}
        io.shutdown()
    }

    private companion object {
        const val GOOGLE="com.google.android.tts"
        val MAGIC=byteArrayOf('L'.code.toByte(),'D'.code.toByte(),'V'.code.toByte(),'1'.code.toByte())
    }
}
