package ai.learndrive.mobile.audio

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import kotlin.math.PI
import kotlin.math.sin
import kotlin.math.sqrt

/** The recorded voice built into the app: complete for every phrase, unpacked at the right rate, kept as takes. */
class AppVoiceTest {
    private val header=listOf("../../core/include/learndrive/types.hpp","../core/include/learndrive/types.hpp","core/include/learndrive/types.hpp")
        .map(::File).first {it.isFile}.readText()
    private fun texts(name:String):List<String> {
        val start=header.indexOf("inline const char* $name(");val end=header.indexOf("default:",start)
        return Regex("return\\s+\"([^\"]*)\"").findAll(header.substring(start,end)).map {it.groupValues[1]}.toList()
    }

    /** The voice built into the app is whole (every take it lists is there) and says which phrases
     * it has no take of: the artificial voice says those. Missing phrases never stop a build. */
    @Test fun theBuiltInVoiceListsWhatTheArtificialVoiceSays() {
        val folder=listOf("src/voice/assets/voice","app/src/voice/assets/voice","android/app/src/voice/assets/voice").map(::File).firstOrNull {it.isDirectory}
        assumeTrue("No recorded voice in this checkout (it is kept out of the repository)",folder!=null)
        for(language in folder!!.listFiles()!!.filter {it.isDirectory}) {
            val manifest=File(language,"manifest.json").readText()
            val takes=Regex("\"file\":\\s*\"([^\"]+)\",\\s*\"words\":\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").findAll(manifest)
                .map {it.groupValues[1] to it.groupValues[2].replace("\\\"","\"").replace("\\\\","\\")}.toList()
            assertTrue("${language.name}: no takes",takes.isNotEmpty())
            for((file,_) in takes)assertTrue("${language.name}/$file is missing",File(language,file).isFile)
            val recorded=takes.map {VoiceScript.key(it.second)}.toSet()
            val missing=VoiceScript.lines(language.name,texts("fragmentText"),texts("promptText")).filter {VoiceScript.key(it.words) !in recorded}
            println("${language.name} voice: ${takes.size} takes; ${missing.size} phrases in the artificial voice"+
                (if(missing.isEmpty())"" else ": "+missing.joinToString(" | ") {it.words}))
        }
    }

    @Test fun theDecodersFortyEightKilohertzBecomesTheCoachsRateUnchanged() {
        val tone=ShortArray(48000) {(10000*sin(2*PI*1000.0*it/48000)).toInt().toShort()}
        val out=VoiceClips.resample(tone,48000,22050)
        assertEquals(22050,out.size)
        // Still 1 kHz: about 2000 zero crossings a second.
        val crossings=(1 until out.size).count {(out[it-1]<0)!=(out[it]<0)}
        assertEquals(2000.0,crossings.toDouble(),4.0)
        // Same loudness (RMS of a 10 000 sine: 7071), away from the ends.
        var sum=0.0;for(i in 1000 until 21000)sum+=out[i].toDouble()*out[i]
        assertEquals(7071.0,sqrt(sum/20000),7071*.03)
        assertArrayEquals(tone,VoiceClips.resample(tone,48000,48000))
    }

    @Test fun eachDistinctPhraseNeedsOneTake() {
        val lines=VoiceScript.lines(VoicePhrases.ENGLISH,listOf("In 50 metres,","turn right.","Brake gently."),listOf("Brake gently","Keep a safe gap"))
        assertEquals(listOf("In 50 metres,","turn right.","Brake gently.","Keep a safe gap"),lines.map {it.words})
        assertEquals("അമ്പത് മീറ്റർ കഴിഞ്ഞ്,",VoiceScript.lines(VoicePhrases.MALAYALAM,listOf("In 50 metres,"),emptyList()).single().words)
    }

    @Test fun takesAreKeptAndTheVoiceIsCompleteOnlyWithEveryPhrase() {
        val folder=Files.createTempDirectory("app-voice").toFile()
        try {
            val first=VoiceClips.pcm(ShortArray(2205) {(it%100).toShort()},22050)
            val second=VoiceClips.pcm(ShortArray(2205) {(it%50).toShort()},22050)
            val voice=RecordedVoice(folder,VoicePhrases.ENGLISH)
            voice.require(listOf("Brake gently.","Keep a safe gap"))
            assertFalse(voice.complete)
            voice.save("Brake gently",first)
            assertTrue(voice.has("Brake gently."));assertFalse(voice.complete)
            voice.save("Keep a safe gap",first);voice.save("Keep a safe gap",second)
            assertTrue(voice.complete)
            assertArrayEquals(second.bytes,voice.clip("Keep a safe gap.")!!.bytes)
            assertNull(voice.clip("Brake gently. Keep a safe gap."))   // composed sentences are joined from phrases
            val again=RecordedVoice(folder,VoicePhrases.ENGLISH).apply {require(listOf("Brake gently.","Keep a safe gap"))}
            assertTrue(again.complete);assertNotNull(again.clip("brake gently"))
            again.clear();assertFalse(again.complete)
        } finally {folder.deleteRecursively()}
    }
}
