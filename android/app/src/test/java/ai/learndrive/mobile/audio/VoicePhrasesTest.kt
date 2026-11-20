package ai.learndrive.mobile.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** Every phrase and sentence the coach can say has Malayalam words, joined the way the English is. */
class VoicePhrasesTest {
    // The core's own English (ld::fragmentText and ld::promptText), read from its header.
    private val header=listOf("../../core/include/learndrive/types.hpp","../core/include/learndrive/types.hpp","core/include/learndrive/types.hpp")
        .map(::File).first {it.isFile}.readText()
    private fun texts(name:String):List<String> {
        val start=header.indexOf("inline const char* $name(");val end=header.indexOf("default:",start)
        return Regex("return\\s+\"([^\"]*)\"").findAll(header.substring(start,end)).map {it.groupValues[1]}.toList()
    }
    private val fragments=texts("fragmentText")
    private val prompts=texts("promptText")
    private fun ml(english:String)=VoicePhrases.spoken(VoicePhrases.MALAYALAM,english)

    @Test fun everyPhraseTheCoreCanSayHasMalayalamWords() {
        assertEquals(77,fragments.size);assertEquals(95,prompts.size)
        for(english in fragments+prompts) {
            val words=ml(english)
            assertNotNull("No Malayalam for \"$english\"",words)
            assertTrue("Not in Malayalam script: $words",Regex("[ഀ-ൿ]").containsMatchIn(words!!))
            // English words are written as they sound, in Malayalam script, so the voice reads them as Malayalam.
            assertFalse("Latin letters in \"$words\"",Regex("[A-Za-z]").containsMatchIn(words))
        }
        assertEquals("Words for phrases the core no longer says",(fragments+prompts).toSet(),VoicePhrases.MALAYALAM_WORDS.keys)
    }

    @Test fun phrasesEndLikeTheEnglishSoTheyArePausedTheSame() {
        // AudioWorker pauses briefly after a comma ("In 50 metres,") and longer after a full stop.
        for(english in fragments)assertEquals("Ending of \"$english\"",english.last(),ml(english)!!.last())
    }

    @Test fun differentInstructionsNeverSoundTheSame() {
        for(group in listOf(fragments,prompts)) {
            val words=group.map(::ml)
            assertEquals(words.size,words.toSet().size)
        }
        assertNotEquals(ml("Change up to second gear"),ml("Change down to second gear"))
        assertNotEquals(ml("Signal left."),ml("Signal right."))
    }

    @Test fun aComposedSentenceIsItsPhrasesInOrder() {
        // The core joins phrases with spaces ("In 50 metres, turn right."); Malayalam keeps the order.
        assertEquals("അമ്പത് മീറ്റർ കഴിഞ്ഞ്, വലത്തേക്ക് തിരിയുക.",listOf("In 50 metres,","turn right.").joinToString(" ") {ml(it)!!})
        assertEquals("Brake gently.",VoicePhrases.spoken(VoicePhrases.ENGLISH," Brake gently. "))
        assertNull(VoicePhrases.spoken("ta","Brake gently."))
        assertTrue(VoicePhrases.supported("ml"));assertFalse(VoicePhrases.supported("ta"))
    }
}
