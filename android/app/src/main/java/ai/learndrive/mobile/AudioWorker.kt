package ai.learndrive.mobile

import ai.learndrive.mobile.audio.PhoneVoice
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioFocusRequest
import android.media.AudioTrack
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

fun interface AudioPlaybackGate {
    /** Acquire the native lock, then call play with a predicate checked at the
     * AudioTrack.play boundary, after the audio monitor has been acquired. */
    fun start(play:(()->Boolean)->Boolean):Boolean
    /** Called outside the audio monitor: native bookkeeping may take core lock. */
    fun terminal(stage:Int) {}
    /** Called outside the audio monitor, at most ten times a second. */
    fun continuePlaying():Boolean=true
}

/** PCM shape analysis shared by the bundled and the phone voice (pure functions). */
internal object AudioClips {
    private fun sample(bytes:ByteArray,i:Int)=(bytes[i+1].toInt() shl 8) or (bytes[i].toInt() and 0xff)
    /** Frame offsets in the middle of natural pauses (>= 70 ms below 2% of full scale, after the
     * first sound), then the clip end: where a non-urgent stop ends between words. */
    fun pauses(bytes:ByteArray,sampleRate:Int,channels:Int):IntArray {
        val frameBytes=2*channels;val frames=bytes.size/frameBytes;val window=(sampleRate/100).coerceAtLeast(1)
        val result=ArrayList<Int>();var run=0;var heard=false
        var w=0
        while(w*window<frames) {
            var peak=0
            for(f in w*window until minOf(frames,(w+1)*window)){val v=kotlin.math.abs(sample(bytes,f*frameBytes));if(v>peak)peak=v}
            if(peak<655){run++} else {
                if(heard&&run>=7)result.add((w-run/2)*window)
                heard=true;run=0
            }
            w++
        }
        result.add(frames)
        return result.toIntArray()
    }
    /** Byte range of the sound itself: 20 ms before the first sample above 1% and 50 ms after the last. */
    fun sound(bytes:ByteArray,sampleRate:Int,channels:Int):Pair<Int,Int> {
        val frameBytes=2*channels;val frames=bytes.size/frameBytes
        var first=0;while(first<frames&&kotlin.math.abs(sample(bytes,first*frameBytes))<328)first++
        var last=frames-1;while(last>first&&kotlin.math.abs(sample(bytes,last*frameBytes))<328)last--
        if(first>=last)return 0 to bytes.size
        val start=maxOf(0,first-sampleRate*20/1000);val end=minOf(frames,last+1+sampleRate*50/1000)
        return start*frameBytes to end*frameBytes
    }
}

class AudioWorker(context:Context,private val mayInterrupt:(Int,Int)->Boolean,private val clipCount:Int,
    private val priority:(Int)->Int={Int.MAX_VALUE},private val fragmentCount:Int=0,
    private val fragments:(Int)->IntArray={IntArray(0)},private val fragmentText:(Int)->String={""},
    private val promptText:(Int)->String={""},private val phoneVoice:PhoneVoice?=null,
    private val status:(String)->Unit): AutoCloseable {
    private class Clip(val bytes:ByteArray,val sampleRate:Int,val channels:Int) {
        val frames get()=bytes.size/(2*channels)
        val pauses:IntArray by lazy {AudioClips.pauses(bytes,sampleRate,channels)}
    }
    private val cache=ConcurrentHashMap<Int,Clip>()
    // Recorded instructor fragments, joined per composed utterance (id >= 1000).
    private val fragmentClips=ConcurrentHashMap<Int,Clip>()
    private val composed=ConcurrentHashMap<String,Clip>()
    private val natural=ConcurrentHashMap<String,Clip>()
    /** Speak with the phone's own natural voice once every phrase is rendered (the setting). */
    @Volatile var naturalVoice=true
    private fun clipFor(id:Int):Clip? {
        val voice=phoneVoice?.takeIf {naturalVoice&&it.complete}
        if(voice!=null)naturalClip(id,voice)?.let {return it}
        return bundledClip(id)
    }
    private fun bundledClip(id:Int):Clip? {
        cache[id]?.let {return it}
        if(id<1000||fragmentCount<=0)return null
        val parts=fragments(id);if(parts.isEmpty())return null
        return composed.getOrPut(parts.joinToString(",")) {join(parts.map {fragmentClips[it]?:return null},parts)?:return null}
    }
    private fun naturalClip(id:Int,voice:PhoneVoice):Clip? {
        fun clip(text:String)=voice.clip(text)?.let {p->natural.getOrPut(text) {Clip(p.bytes,p.sampleRate,p.channels)}}
        if(id<1000)return clip(promptText(id))
        // A composed sentence rendered whole keeps natural intonation; until that render exists
        // (requested by this lookup) its phrases are joined.
        clip(promptText(id))?.let {return it}
        val parts=fragments(id);if(parts.isEmpty())return null
        val key="natural:"+parts.joinToString(",")
        composed[key]?.let {return it}
        return join(parts.map {clip(fragmentText(it))?:return null},parts)?.also {composed[key]=it}
    }
    /** Phrases joined like speech: each trimmed to its sound, then a short breath after a comma
     * (about 0.19 s in all) or a sentence pause after a full stop (about 0.31 s). The old join
     * left about 0.38 s of dead air between every phrase, which sounded stilted. */
    private fun join(clips:List<Clip>,parts:IntArray):Clip? {
        val first=clips.firstOrNull()?:return null
        if(clips.any {it.sampleRate!=first.sampleRate||it.channels!=first.channels})return null
        val frameBytes=2*first.channels
        val out=java.io.ByteArrayOutputStream()
        clips.forEachIndexed {i,c->
            val (start,end)=AudioClips.sound(c.bytes,c.sampleRate,c.channels)
            out.write(c.bytes,start,end-start)
            if(i<clips.size-1)out.write(ByteArray((first.sampleRate*(if(fragmentText(parts[i]).trimEnd().endsWith(","))0.12 else 0.24)).toInt()*frameBytes))
        }
        return Clip(out.toByteArray(),first.sampleRate,first.channels)
    }
    // Audio priority: the camera and detector keep a warm phone busy; a starved stream crackles.
    private val worker=Executors.newSingleThreadExecutor {r->Thread({android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_AUDIO);r.run()},"pcm-audio")}
    private val generation=AtomicInteger()
    private val audioManager=context.getSystemService(AudioManager::class.java)
    private val attributes=AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE).setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
    private val focus=AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK).setAudioAttributes(attributes).setOnAudioFocusChangeListener {
        // A notification that only ducks other audio must not cut the instruction off.
        if(it==AudioManager.AUDIOFOCUS_LOSS||it==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)cancel() }.build()
    internal var requestFocus:()->Int={audioManager.requestAudioFocus(focus)}
    @Volatile private var active=0
    @Volatile private var pending=0
    // The generation whose sentence should end at its next natural pause.
    @Volatile private var gentleStop=-1
    private var activeWarning=false
    private var pendingWarning=false
    @Volatile private var track:AudioTrack?=null
    @Volatile private var closed=false
    @Volatile private var pcmReady=false
    @Volatile private var voiceAllowed=true
    val voiceEnabled get()=voiceAllowed
    val ready get() = cache.size==clipCount&&fragmentClips.size==fragmentCount&&pcmReady&&!closed
    fun durationSeconds(id:Int)=clipFor(id)?.let {it.bytes.size.toDouble()/(it.sampleRate*it.channels*2)}?:3.0
    init {
        worker.execute {
            try {
                for(id in 1..clipCount)cache[id]=readWave(context.assets.open("audio/prompt-$id.wav").use {it.readBytes()})
                for(id in 1..fragmentCount)fragmentClips[id]=readWave(context.assets.open("audio/fragment-$id.wav").use {it.readBytes()})
                val clip=cache.getValue(1)
                check((cache.values+fragmentClips.values).all {it.sampleRate==clip.sampleRate&&it.channels==clip.channels}) {"Voice formats differ between prompts"}
                val channel=if(clip.channels==1)AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
                val minimum=AudioTrack.getMinBufferSize(clip.sampleRate,channel,AudioFormat.ENCODING_PCM_16BIT);check(minimum>0)
                // About half a second queued ahead; stops still discard whatever is queued.
                val size=maxOf(minimum*2,clip.sampleRate*clip.channels)
                val player=AudioTrack.Builder().setAudioAttributes(attributes).setAudioFormat(AudioFormat.Builder().setSampleRate(clip.sampleRate).setChannelMask(channel).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                    .setBufferSizeInBytes(size).setTransferMode(AudioTrack.MODE_STREAM).build()
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
        val clip=clipFor(id)?:return false
        fun canReplace(current:Int,warning:Boolean)=AudioInterruptionPolicy.mayReplace(validatedWarning,warning,
            priority(current),mayInterrupt(id,current))
        if(!voiceEnabled||!ready||!canReplace(active,activeWarning)||!canReplace(pending,pendingWarning))return false
        if(requestFocus()!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {status("Audio focus unavailable; retry Test voice");return false}
        val token=generation.incrementAndGet();pending=id;pendingWarning=validatedWarning;stopNow()
        worker.execute {
            var outcome=4 // interrupted unless a later boundary proves otherwise
            var heardFrames=0
            val player=track
            try {
                if(generation.get()!=token||closed||player==null)return@execute
                val frameBytes=2*clip.channels
                // The phone voice may use another sample rate than the bundled recordings.
                if(player.playbackRate!=clip.sampleRate)player.setPlaybackRate(clip.sampleRate)
                // Queue the start before play(): no underrun at the first syllable.
                var offset=writeCurrent(token) {player.write(clip.bytes,0,clip.bytes.size,AudioTrack.WRITE_NON_BLOCKING)}?:return@execute
                check(offset>=0) {"PCM write failed: $offset"}
                val play:(()->Boolean)->Boolean={valid->synchronized(this) {
                    if(generation.get()!=token||closed||!voiceEnabled||!valid())false
                    else {active=id;activeWarning=validatedWarning;pending=0;pendingWarning=false;player.setVolume(1f);player.play();true}
                }}
                if(!(onset?.start(play)?:play {true})){outcome=if(generation.get()!=token||closed)4 else 5;return@execute}
                var contextCheckAt=0L
                fun contextCurrent():Boolean {
                    if(gentleStop==token)return false
                    val t=android.os.SystemClock.elapsedRealtime()
                    if(t-contextCheckAt<100)return true
                    contextCheckAt=t;return onset?.continuePlaying()?:true
                }
                // A stop that is not urgent ends the sentence at its next natural pause, never mid-word.
                var stopAt=clip.frames
                fun endAtPause() {if(stopAt==clip.frames){val head=player.playbackHeadPosition;stopAt=clip.pauses.firstOrNull {it>=head}?:clip.frames}}
                // Last observed playback position: a flush by the next clip resets it to zero.
                fun heard(){heardFrames=maxOf(heardFrames,player.playbackHeadPosition)}
                val chunk=(clip.sampleRate/50).coerceAtLeast(1)*frameBytes // Whole PCM frames, about 20 ms.
                while(offset<stopAt*frameBytes&&generation.get()==token&&!closed) {
                    if(!contextCurrent())endAtPause()
                    val limit=stopAt*frameBytes
                    if(offset>=limit)break
                    // Cancellation flushes under the same monitor. An old
                    // worker must never append a chunk after that flush.
                    val count=writeCurrent(token) {
                        player.write(clip.bytes,offset,minOf(chunk,limit-offset),AudioTrack.WRITE_NON_BLOCKING)
                    }?:break
                    check(count>=0) {"PCM write failed: $count"}
                    offset+=count
                    heard()
                    if(count==0)Thread.sleep(4)
                }
                while(player.playbackHeadPosition<stopAt&&generation.get()==token&&!closed){if(!contextCurrent())endAtPause();heard();Thread.sleep(5)}
                heard()
                outcome=if(generation.get()==token&&!closed&&stopAt==clip.frames&&offset==clip.bytes.size)8 else 4
            } catch(e:Exception) {outcome=6;status("Audio output: ${e.message}")}
            finally {
                // 85 % heard is delivered: interrupting the last syllables must not repeat the sentence.
                if(outcome==4&&heardFrames>=clip.frames*85L/100)outcome=8
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
    /** Stop at once without a click: a ~25 ms fade, then discard whatever is queued. */
    private fun stopNow() {
        val player=track?:return
        try {
            if(player.playState==AudioTrack.PLAYSTATE_PLAYING){player.setVolume(0f);Thread.sleep(25)}
            player.pause();player.flush();player.setVolume(1f)
        } catch(_:Exception) {}
    }
    /** End the current sentence at its next natural pause; a clip not yet audible is dropped. */
    @Synchronized private fun finishGently() {
        if(active==0){cancel();return}
        gentleStop=generation.get()
    }
    @Synchronized fun setVoiceEnabled(enabled:Boolean) {
        voiceAllowed=enabled
        if(!enabled)cancel()
    }
    @Synchronized fun cancel() {generation.incrementAndGet();pending=0;active=0;activeWarning=false;pendingWarning=false;stopNow();audioManager.abandonAudioFocusRequest(focus)}
    /** Cancel outdated context without cutting off a newer, higher-priority cue: the sentence
     * ends at its next natural pause. */
    @Synchronized fun cancelIfPrompt(matches:(Int)->Boolean):Boolean {
        val current=if(pending!=0)pending else active
        if(current==0||!matches(current))return false
        finishGently();return true
    }
    /** A task workload boundary must preserve independently validated warnings. */
    @Synchronized fun cancelRoutine():Boolean {
        if((pending!=0&&pendingWarning)||(active!=0&&activeWarning)||(pending==0&&active==0))return false
        finishGently();return true
    }
    @Synchronized fun preempt() {
        generation.incrementAndGet();pending=0;active=0;activeWarning=false;pendingWarning=false
        stopNow();runCatching {track?.stop()}
    }
    @Synchronized override fun close() {
        if(closed)return
        closed=true;pcmReady=false;cancel()
        worker.execute {synchronized(this){track?.release();track=null}}
        worker.shutdown()
    }
}
