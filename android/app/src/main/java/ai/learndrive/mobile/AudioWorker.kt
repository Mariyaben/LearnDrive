package ai.learndrive.mobile

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioFocusRequest
import android.media.AudioTrack
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

fun interface AudioPlaybackGate {
    /** Acquire the native lock, then call play with a predicate checked at the
     * AudioTrack.play boundary, after the audio monitor has been acquired. */
    fun start(play:(()->Boolean)->Boolean):Boolean
    /** Called outside the audio monitor: native bookkeeping may take core lock. */
    fun terminal(stage:Int) {}
}

class AudioWorker(context:Context,private val mayInterrupt:(Int,Int)->Boolean,private val clipCount:Int,
    private val priority:(Int)->Int={Int.MAX_VALUE},private val status:(String)->Unit): AutoCloseable {
    private data class Clip(val bytes:ByteArray,val sampleRate:Int,val channels:Int)
    private val cache=java.util.concurrent.ConcurrentHashMap<Int,Clip>()
    private val worker=Executors.newSingleThreadExecutor { r -> Thread(r,"pcm-audio") }
    private val generation=AtomicInteger()
    private val audioManager=context.getSystemService(AudioManager::class.java)
    private val attributes=AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
    private val focus=AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK).setAudioAttributes(attributes).setOnAudioFocusChangeListener { if(it<0)cancel() }.build()
    internal var requestFocus:()->Int={audioManager.requestAudioFocus(focus)}
    @Volatile private var active=0
    @Volatile private var pending=0
    private var activeWarning=false
    private var pendingWarning=false
    @Volatile private var track:AudioTrack?=null
    @Volatile private var closed=false
    @Volatile private var pcmReady=false
    @Volatile private var voiceAllowed=true
    val voiceEnabled get()=voiceAllowed
    val ready get() = cache.size==clipCount&&pcmReady&&!closed
    fun durationSeconds(id:Int)=cache[id]?.let {it.bytes.size.toDouble()/(it.sampleRate*it.channels*2)}?:3.0
    init {
        worker.execute {
            try {
                for(id in 1..clipCount)cache[id]=readWave(context.assets.open("audio/prompt-$id.wav").use {it.readBytes()})
                val clip=cache.getValue(1)
                check(cache.values.all {it.sampleRate==clip.sampleRate&&it.channels==clip.channels}) {"Voice formats differ between prompts"}
                val channel=if(clip.channels==1)AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
                val minimum=AudioTrack.getMinBufferSize(clip.sampleRate,channel,AudioFormat.ENCODING_PCM_16BIT);check(minimum>0)
                val player=AudioTrack.Builder().setAudioAttributes(attributes).setAudioFormat(AudioFormat.Builder().setSampleRate(clip.sampleRate).setChannelMask(channel).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                    .setBufferSizeInBytes(minimum*2).setTransferMode(AudioTrack.MODE_STREAM).build()
                check(player.state==AudioTrack.STATE_INITIALIZED) {"PCM output did not initialize"}
                synchronized(this) {if(closed)player.release() else {track=player;pcmReady=true}}
                if(ready)status("Offline voice ready")
            }catch(e:Exception){status("Audio unavailable: ${e.message}")}
        }
    }
    private fun readWave(bytes:ByteArray):Clip {
        val buffer=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        fun tag(offset:Int)=String(bytes,offset,4,Charsets.US_ASCII)
        require(bytes.size>=12&&tag(0)=="RIFF"&&tag(8)=="WAVE") {"Invalid warning WAV"}
        var offset=12;var rate=0;var channels=0;var data:ByteArray?=null
        while(offset+8<=bytes.size) {
            val length=buffer.getInt(offset+4)
            require(length>=0&&length<=bytes.size-offset-8) {"Truncated warning WAV"}
            val start=offset+8
            when(tag(offset)) {
                "fmt " -> {
                    require(length>=16&&buffer.getShort(start).toInt()==1&&buffer.getShort(start+14).toInt()==16) {"Warning WAV must be PCM16"}
                    channels=buffer.getShort(start+2).toInt();rate=buffer.getInt(start+4)
                }
                "data" -> data=bytes.copyOfRange(start,start+length)
            }
            offset=start+length+(length and 1)
        }
        val pcm=requireNotNull(data) {"Warning WAV has no PCM"}
        require(channels in 1..2&&rate in 8000..48000&&pcm.isNotEmpty()&&pcm.size%(channels*2)==0) {"Invalid warning PCM format"}
        return Clip(pcm,rate,channels)
    }
    /** The optional onset gate acquires the native lock before taking this worker's
     * monitor. Calling it outside this monitor preserves core -> audio lock order. */
    fun submit(id:Int,onset:AudioPlaybackGate?=null):Boolean=submitClip(id,onset,false)
    /** Only NativeCore calls this after pending(seq,prompt) validates the exact
     * source/revision and native mode. Test voice has no warning-authority flag. */
    internal fun submitCoaching(id:Int,validatedWarning:Boolean,onset:AudioPlaybackGate):Boolean=
        submitClip(id,onset,validatedWarning)
    @Synchronized private fun submitClip(id:Int,onset:AudioPlaybackGate?,validatedWarning:Boolean):Boolean {
        val clip=cache[id]?:return false
        fun canReplace(current:Int,warning:Boolean)=AudioInterruptionPolicy.mayReplace(validatedWarning,warning,
            priority(current),mayInterrupt(id,current))
        if(!voiceEnabled||!ready||!canReplace(active,activeWarning)||!canReplace(pending,pendingWarning))return false
        if(requestFocus()!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {status("Audio focus unavailable; retry Test voice");return false}
        val token=generation.incrementAndGet();pending=id;pendingWarning=validatedWarning;track?.pause();track?.flush()
        worker.execute {
            var outcome=4 // interrupted unless a later boundary proves otherwise
            val player=track
            try {
                if(generation.get()!=token||closed||player==null)return@execute
                val play:(()->Boolean)->Boolean={valid->synchronized(this) {
                    if(generation.get()!=token||closed||!voiceEnabled||!valid())false
                    else {active=id;activeWarning=validatedWarning;pending=0;pendingWarning=false;player.play();true}
                }}
                if(!(onset?.start(play)?:play {true})){outcome=if(generation.get()!=token||closed)4 else 5;return@execute}
                var offset=0
                val chunk=(clip.sampleRate/100).coerceAtLeast(1)*clip.channels*2 // Whole PCM frames, about 10 ms.
                while(offset<clip.bytes.size&&generation.get()==token&&!closed) {
                    // Cancellation flushes under the same monitor. An old
                    // worker must never append a chunk after that flush.
                    val count=writeCurrent(token) {
                        player.write(clip.bytes,offset,minOf(chunk,clip.bytes.size-offset),AudioTrack.WRITE_NON_BLOCKING)
                    }?:break
                    check(count>=0) {"PCM write failed: $count"}
                    offset+=count
                    if(count==0)Thread.sleep(2)
                }
                val frames=offset/(2*clip.channels)
                while(player.playbackHeadPosition<frames&&generation.get()==token&&!closed)Thread.sleep(5)
                outcome=if(generation.get()==token&&!closed&&offset==clip.bytes.size)8 else 4
            } catch(e:Exception) {outcome=6;status("Audio output: ${e.message}")}
            finally {
                synchronized(this) {if(generation.get()==token){player?.pause();player?.flush();active=0;pending=0;activeWarning=false;pendingWarning=false;audioManager.abandonAudioFocusRequest(focus)}}
                onset?.terminal(outcome)
            }
        }
        return true
    }
    @Synchronized private fun writeCurrent(token:Int,write:()->Int):Int? {
        if(generation.get()!=token||closed||!voiceEnabled)return null
        return write()
    }
    @Synchronized fun setVoiceEnabled(enabled:Boolean) {
        voiceAllowed=enabled
        if(!enabled)cancel()
    }
    @Synchronized fun cancel() {generation.incrementAndGet();pending=0;active=0;activeWarning=false;pendingWarning=false;track?.pause();track?.flush();audioManager.abandonAudioFocusRequest(focus)}
    /** Cancel outdated context without cutting off a newer, higher-priority cue. */
    @Synchronized fun cancelIfPrompt(matches:(Int)->Boolean):Boolean {
        val current=if(pending!=0)pending else active
        if(current==0||!matches(current))return false
        cancel();return true
    }
    @Synchronized fun preempt() {
        generation.incrementAndGet();pending=0;active=0;activeWarning=false;pendingWarning=false
        track?.pause();track?.flush();track?.stop()
    }
    @Synchronized override fun close() {
        if(closed)return
        closed=true;pcmReady=false;cancel()
        worker.execute {synchronized(this){track?.release();track=null}}
        worker.shutdown()
    }
}
