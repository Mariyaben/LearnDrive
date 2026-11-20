package ai.learndrive.mobile.audio

import android.content.Context
import android.media.MediaCodec
import android.media.MediaDataSource
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.SystemClock
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.concurrent.Executors

/**
 * The coach's recorded voice, built into the app (assets/voice/<language>, put there by
 * tools/import_voice_pack.py): Opus takes, unpacked once on the phone into app storage as PCM and
 * then spoken like any clip. The coach speaks with it once it is unpacked; a phrase it has no take
 * of (a newer phrase, or a take that would not unpack) is said by the artificial voice instead.
 */
class AppVoice private constructor(context:Context,override val language:String,private val takes:List<Take>,pack:String,
    private val status:(String)->Unit):CoachVoice,AutoCloseable {
    private class Take(val file:String,val words:String)
    private val app=context.applicationContext
    private val folder="app-voice-$language-$pack"
    private val store=RecordedVoice(File(app.noBackupFilesDir,folder),language)
    private val io=Executors.newSingleThreadExecutor {r->Thread(r,"app-voice")}
    @Volatile private var unpacked=false
    @Volatile private var closed=false
    /** Ready once unpacked; phrases without a take fall back to the artificial voice. */
    override val complete:Boolean get()=unpacked
    override fun clip(text:String):Pcm?=if(unpacked)store.clip(text) else null
    /** The words the coach can say in [language]; [missing] counts those without a take. */
    fun require(words:Collection<String>)=store.require(words)
    val missing get()=store.total-store.recorded

    init {
        // Takes of an earlier version of this voice are of no use any more.
        app.noBackupFilesDir.listFiles()?.filter {it.isDirectory&&it.name.startsWith("app-voice-$language-")&&it.name!=folder}
            ?.forEach {it.deleteRecursively()}
        io.execute {unpack()}
    }

    private fun unpack() {
        var failed=0;var done=0
        for(take in takes) {
            if(closed)return
            if(!store.has(take.words)) {
                val pcm=runCatching {OpusClips.decode(app.assets.open("voice/$language/${take.file}").use {it.readBytes()})}.getOrNull()
                if(pcm==null)failed++ else store.save(take.words,pcm)
            }
            if(++done%40==0)status("Coach voice: preparing $done of ${takes.size}")
        }
        unpacked=true
        status(if(failed==0)"Coach voice ready" else "Coach voice ready · $failed takes would not unpack, the artificial voice says those")
    }

    override fun close(){closed=true;io.shutdown()}

    companion object {
        /** The voice built into the app for [language], or null when the app has none. */
        fun load(context:Context,language:String,status:(String)->Unit):AppVoice?=runCatching {
            val text=context.assets.open("voice/$language/manifest.json").use {it.readBytes().toString(Charsets.UTF_8)}
            val list=JSONObject(text).getJSONArray("takes")
            val takes=(0 until list.length()).map {val t=list.getJSONObject(it);Take(t.getString("file"),t.getString("words"))}
            if(takes.isEmpty())null else AppVoice(context,language,takes,VoiceClips.sha1(text).take(10),status)
        }.getOrNull()
    }
}

/** Opus takes to the coach's PCM (mono, 22.05 kHz), with the phone's own decoder. */
internal object OpusClips {
    const val RATE=22050

    fun decode(bytes:ByteArray):Pcm? {
        val extractor=MediaExtractor()
        var codec:MediaCodec?=null
        try {
            extractor.setDataSource(object:MediaDataSource() {
                override fun readAt(position:Long,buffer:ByteArray,offset:Int,size:Int):Int {
                    if(position>=bytes.size)return -1
                    val n=minOf(size.toLong(),bytes.size-position).toInt()
                    System.arraycopy(bytes,position.toInt(),buffer,offset,n);return n
                }
                override fun getSize()=bytes.size.toLong()
                override fun close()=Unit
            })
            val track=(0 until extractor.trackCount).firstOrNull {extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/")==true}?:return null
            extractor.selectTrack(track)
            val format=extractor.getTrackFormat(track)
            var rate=format.getInteger(MediaFormat.KEY_SAMPLE_RATE);var channels=format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val decoder=MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME)!!).also {codec=it}
            decoder.configure(format,null,null,0);decoder.start()
            val out=ByteArrayOutputStream();val info=MediaCodec.BufferInfo()
            var inputDone=false;var outputDone=false
            val deadline=SystemClock.elapsedRealtime()+10_000
            while(!outputDone&&SystemClock.elapsedRealtime()<deadline) {
                if(!inputDone) {
                    val i=decoder.dequeueInputBuffer(10_000)
                    if(i>=0) {
                        val n=extractor.readSampleData(decoder.getInputBuffer(i)!!,0)
                        if(n<0){decoder.queueInputBuffer(i,0,0,0,MediaCodec.BUFFER_FLAG_END_OF_STREAM);inputDone=true}
                        else {decoder.queueInputBuffer(i,0,n,extractor.sampleTime,0);extractor.advance()}
                    }
                }
                val o=decoder.dequeueOutputBuffer(info,10_000)
                if(o>=0) {
                    val buffer=decoder.getOutputBuffer(o)!!
                    val chunk=ByteArray(info.size);buffer.position(info.offset);buffer.get(chunk);out.write(chunk)
                    decoder.releaseOutputBuffer(o,false)
                    if(info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM!=0)outputDone=true
                } else if(o==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    rate=decoder.outputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);channels=decoder.outputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                }
            }
            if(!outputDone||out.size()==0)return null
            val pcm=Pcm(out.toByteArray(),rate,channels)
            val mono=if(channels==2)VoiceClips.downmix(pcm) else if(channels==1)pcm else return null
            return VoiceClips.pcm(VoiceClips.resample(VoiceClips.samples(mono),rate,RATE),RATE)
        } finally {
            runCatching {codec?.stop()};runCatching {codec?.release()};extractor.release()
        }
    }
}
