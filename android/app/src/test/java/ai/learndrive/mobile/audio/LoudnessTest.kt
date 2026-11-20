package ai.learndrive.mobile.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.sin
import kotlin.math.sqrt

/** Every voice is spoken at one, louder level, and never clips. */
class LoudnessTest {
    private val rate=22050
    private fun tone(seconds:Double,peak:Double)=VoiceClips.pcm(ShortArray((seconds*rate).toInt()) {(peak*sin(2*PI*220.0*it/rate)).toInt().toShort()},rate)
    private fun voicedDb(p:Pcm):Double {
        var sum=0.0;var n=0
        for(v in VoiceClips.samples(p)){val m=abs(v.toInt());if(m>400){sum+=m.toDouble()*m;n++}}
        return 20*log10(sqrt(sum/n)/32768)
    }
    private fun peak(p:Pcm)=VoiceClips.samples(p).maxOf {abs(it.toInt())}

    @Test fun aQuietRecordingIsRaisedToTheSpokenLevel() {
        val quiet=tone(1.0,3300.0)                       // about -20 dBFS RMS, like the recordings
        val out=VoiceClips.loud(quiet)
        assertEquals(VoiceClips.SPOKEN_RMS_DBFS,voicedDb(out),.5)
        assertEquals(quiet.bytes.size,out.bytes.size)
        assertTrue(voicedDb(out)-voicedDb(quiet)>6)      // clearly louder
    }

    @Test fun peaksAreHeldBelowFullScaleWithoutClipping() {
        // Speech-like: a short, loud consonant over quiet vowels needs the limiter.
        val s=VoiceClips.samples(tone(1.0,2500.0))
        for(i in 8000 until 8400)s[i]=(s[i]*9).coerceIn(-32767,32767).toShort()
        val out=VoiceClips.loud(VoiceClips.pcm(s,rate))
        assertTrue("peak ${peak(out)}",peak(out)<=29201)
        val o=VoiceClips.samples(out)
        assertTrue(o.count {abs(it.toInt())>=32767}==0)
        // Away from the burst the vowel keeps the same gain before and after it (the limiter only acts around peaks).
        fun gainNear(i:Int)=(i-110 until i+110).maxOf {abs(o[it].toInt())}.toDouble()/(i-110 until i+110).maxOf {abs(s[it].toInt())}
        assertEquals(gainNear(3000),gainNear(15000),gainNear(3000)*.02)
        // In the burst it is held down just enough: the peak sits at the ceiling, not far below it.
        assertTrue(gainNear(8200)<gainNear(3000)*.6)
        assertTrue("peak ${peak(out)}",peak(out)>=29200*.95)
    }
}
