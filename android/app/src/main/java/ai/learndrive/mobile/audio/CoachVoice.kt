package ai.learndrive.mobile.audio

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sqrt

/** 16-bit little-endian PCM of one phrase. */
class Pcm(val bytes:ByteArray,val sampleRate:Int,val channels:Int)

/**
 * A voice the coach speaks with instead of the bundled English recordings: the phone's own
 * text-to-speech voice ([PhoneVoice]) or a person's recorded voice ([RecordedVoice]).
 */
interface CoachVoice {
    /** The language of its words ([VoicePhrases]). */
    val language:String
    /** True once it can say every phrase; the coach switches to it only then, so one drive never mixes voices. */
    val complete:Boolean
    /** The clip for these words (already in [language]), or null when it has none. */
    fun clip(text:String):Pcm?
}

/** Clip processing shared by the voices: reading, trimming, levelling and storing PCM. Pure functions. */
object VoiceClips {
    private val MAGIC=byteArrayOf('L'.code.toByte(),'D'.code.toByte(),'V'.code.toByte(),'1'.code.toByte())
    /** Voiced RMS of the bundled clips (about -17.5 dBFS); peaks stay below -1 dBFS. */
    private const val TARGET_RMS=0.1334*32768
    private const val PEAK_LIMIT=29200.0

    fun samples(pcm:Pcm):ShortArray=ShortArray(pcm.bytes.size/2).also {ByteBuffer.wrap(pcm.bytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(it)}
    fun pcm(samples:ShortArray,rate:Int):Pcm {
        val out=ByteBuffer.allocate(samples.size*2).order(ByteOrder.LITTLE_ENDIAN);out.asShortBuffer().put(samples)
        return Pcm(out.array(),rate,1)
    }

    /** Trimmed to the speech (30 ms before, 60 ms after) and levelled to the bundled clips. Mono only. */
    fun polish(raw:Pcm):Pcm? {
        val pcm=if(raw.channels==2)downmix(raw) else raw
        if(pcm.channels!=1||pcm.bytes.size<400)return null
        val s=samples(pcm)
        var a=0;while(a<s.size&&abs(s[a].toInt())<328)a++
        var b=s.size-1;while(b>a&&abs(s[b].toInt())<328)b--
        if(a>=b)return null
        a=maxOf(0,a-pcm.sampleRate*30/1000);b=minOf(s.size-1,b+pcm.sampleRate*60/1000)
        return level(s.copyOfRange(a,b+1),pcm.sampleRate,655)
    }

    /** Gain so the voiced samples (above [voiced]) sit at the bundled clips' loudness, peaks limited. */
    private fun level(s:ShortArray,rate:Int,voiced:Int):Pcm {
        var sum=0.0;var n=0;var peak=1
        for(v in s){val m=abs(v.toInt());if(m>voiced){sum+=m.toDouble()*m;n++};if(m>peak)peak=m}
        var gain=if(n>0)TARGET_RMS/sqrt(sum/n) else 1.0
        if(peak*gain>PEAK_LIMIT)gain=PEAK_LIMIT/peak
        return pcm(ShortArray(s.size) {(s[it]*gain).roundToInt().coerceIn(-32768,32767).toShort()},rate)
    }

    /** Voiced RMS every voice is spoken at: louder than the recordings (about -20 dBFS) and the old
     * built-in voice (-17.5 dBFS), so the coach is heard over a running car. */
    const val SPOKEN_RMS_DBFS=-13.0
    private const val CEILING=29200.0   // -1 dBFS

    /**
     * [pcm] at the coach's spoken loudness ([SPOKEN_RMS_DBFS] voiced RMS), its peaks held below -1 dBFS
     * by a look-ahead limiter (3 ms ahead, 60 ms release) so nothing clips or crackles. Mono only.
     */
    fun loud(pcm:Pcm):Pcm {
        if(pcm.channels!=1||pcm.bytes.size<4)return pcm
        val s=samples(pcm)
        var sum=0.0;var n=0
        for(v in s){val m=abs(v.toInt());if(m>400){sum+=m.toDouble()*m;n++}}
        if(n==0)return pcm
        val gain=32768*Math.pow(10.0,SPOKEN_RMS_DBFS/20)/sqrt(sum/n)
        val y=DoubleArray(s.size) {s[it]*gain}
        val reduction=limiterGains(y,pcm.sampleRate)
        return pcm(ShortArray(y.size) {(y[it]*reduction[it]).roundToInt().coerceIn(-32768,32767).toShort()},pcm.sampleRate)
    }

    /** Per-sample gains that keep |y| below [CEILING]: each peak's reduction is reached 3 ms before
     * it (a sliding minimum), eased back over 60 ms, and smoothed so the gain itself never clicks. */
    internal fun limiterGains(y:DoubleArray,rate:Int):DoubleArray {
        val count=y.size;val look=maxOf(2,rate*3/1000)
        val need=DoubleArray(count) {val m=abs(y[it]);if(m>CEILING)CEILING/m else 1.0}
        // Sliding minimum over [i-look, i+look] (monotonic queue of indices).
        val least=DoubleArray(count);val queue=IntArray(count);var head=0;var tail=0;var next=0
        for(i in 0 until count) {
            while(next<count&&next<=i+look){while(tail>head&&need[queue[tail-1]]>=need[next])tail--;queue[tail++]=next;next++}
            while(queue[head]<i-look)head++
            least[i]=need[queue[head]]
        }
        val release=Math.exp(-1.0/(.06*rate));var held=1.0
        for(i in 0 until count){held=minOf(least[i],1.0-(1.0-held)*release);least[i]=held}
        // Moving average over ±look/2: every value in it is at most the peak's own need.
        val half=look/2;val prefix=DoubleArray(count+1);for(i in 0 until count)prefix[i+1]=prefix[i]+least[i]
        return DoubleArray(count) {i->val a=maxOf(0,i-half);val b=minOf(count,i+half+1);(prefix[b]-prefix[a])/(b-a)}
    }

    fun downmix(pcm:Pcm):Pcm {
        val input=ByteBuffer.wrap(pcm.bytes).order(ByteOrder.LITTLE_ENDIAN)
        val frames=pcm.bytes.size/4;val out=ByteBuffer.allocate(frames*2).order(ByteOrder.LITTLE_ENDIAN)
        for(i in 0 until frames)out.putShort(((input.getShort(i*4).toInt()+input.getShort(i*4+2).toInt())/2).toShort())
        return Pcm(out.array(),pcm.sampleRate,1)
    }

    /**
     * [s] from [from] Hz to [to] Hz: windowed-sinc interpolation (16 input samples each side, Hann
     * window), low-passed below the lower rate's Nyquist. Polyphase: the filter for each of the
     * rates' fractional positions is computed once (48 kHz to 22.05 kHz has 147 of them).
     */
    fun resample(s:ShortArray,from:Int,to:Int):ShortArray {
        if(from==to||s.isEmpty())return s.copyOf()
        val g=gcd(from,to);val up=to/g;val down=from/g
        val outLength=(s.size.toLong()*up/down).toInt()
        val half=16;val taps=2*half;val cut=.95*minOf(1.0,to.toDouble()/from)
        if(up>4096)return ShortArray(outLength) {n->s[minOf(s.size-1,(n.toLong()*down/up).toInt())]}
        // Phase p: the output falls p/up of an input sample after the input sample it starts from.
        val table=Array(up) {p->
            val frac=p.toDouble()/up
            val c=DoubleArray(taps) {j->
                val x=(j-half+1)-frac
                val sinc=if(abs(x)<1e-9)1.0 else kotlin.math.sin(Math.PI*x*cut)/(Math.PI*x*cut)
                val window=if(abs(x)>=half)0.0 else .5+.5*kotlin.math.cos(Math.PI*x/half)
                sinc*window
            }
            val sum=c.sum();DoubleArray(taps) {c[it]/sum}
        }
        return ShortArray(outLength) {n->
            val position=n.toLong()*down;val base=(position/up).toInt();val coefficients=table[(position%up).toInt()]
            var acc=0.0
            for(j in 0 until taps){val k=base+j-half+1;if(k>=0&&k<s.size)acc+=s[k]*coefficients[j]}
            acc.roundToInt().coerceIn(-32768,32767).toShort()
        }
    }
    private tailrec fun gcd(a:Int,b:Int):Int=if(b==0)a else gcd(b,a%b)

    fun readWave(bytes:ByteArray):Pcm? {
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

    /** Stores [pcm] in the app's own format, replacing [target] in one step. */
    fun write(target:File,pcm:Pcm) {
        val header=ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN).put(MAGIC).putInt(pcm.sampleRate).putInt(pcm.channels).array()
        val temporary=File(target.parentFile,target.name+".tmp")
        temporary.outputStream().use {it.write(header);it.write(pcm.bytes)}
        if(!temporary.renameTo(target)){target.delete();if(!temporary.renameTo(target))temporary.delete()}
    }

    fun read(source:File):Pcm? = runCatching {
        val bytes=source.readBytes()
        val buffer=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val magic=ByteArray(4).also {buffer.get(it)}
        if(!magic.contentEquals(MAGIC))return@runCatching null
        val rate=buffer.getInt();val channels=buffer.getInt()
        if(rate !in 8000..48000||channels!=1||bytes.size<=12)null else Pcm(bytes.copyOfRange(12,bytes.size),rate,channels)
    }.getOrNull()

    fun sha1(text:String):String=MessageDigest.getInstance("SHA-1").digest(text.toByteArray(Charsets.UTF_8)).joinToString("") {"%02x".format(it)}

    /** A standard 16-bit PCM WAV file of [pcm], playable anywhere. */
    fun wav(pcm:Pcm):ByteArray {
        val header=ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        header.put("RIFF".toByteArray(Charsets.US_ASCII)).putInt(36+pcm.bytes.size).put("WAVE".toByteArray(Charsets.US_ASCII))
        header.put("fmt ".toByteArray(Charsets.US_ASCII)).putInt(16).putShort(1).putShort(pcm.channels.toShort()).putInt(pcm.sampleRate)
            .putInt(pcm.sampleRate*pcm.channels*2).putShort((pcm.channels*2).toShort()).putShort(16)
        header.put("data".toByteArray(Charsets.US_ASCII)).putInt(pcm.bytes.size)
        return header.array()+pcm.bytes
    }
}
