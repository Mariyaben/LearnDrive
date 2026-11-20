package ai.learndrive.mobile.audio

import java.io.File
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

/** Every distinct thing the coach can say in a voice language: what a recorded voice needs a take of. */
object VoiceScript {
    /** One thing to record: the words, what they mean in English, and how to say them when it matters. */
    data class Line(val words:String,val english:String,val hint:String?)
    /** The same words with or without a closing full stop (or capital) share one take. */
    fun key(words:String)=words.trim().trimEnd('.','!','?',' ').trim().lowercase(Locale.ROOT)
    /** The phrases ([fragments], ld::fragmentText) then the sentences ([prompts], ld::promptText), each once. */
    fun lines(language:String,fragments:List<String>,prompts:List<String>):List<Line> {
        val seen=HashSet<String>();val out=ArrayList<Line>()
        for(english in fragments+prompts) {
            val words=VoicePhrases.spoken(language,english)?.trim()?.takeIf {it.isNotEmpty()}?:continue
            if(seen.add(key(words)))out.add(Line(words,english.trim(),hint(english.trim())))
        }
        return out
    }
    /** Phrases the coach joins into sentences need the voice of a sentence's start or end. */
    private fun hint(english:String)=when {
        english.endsWith(",")->"The start of a sentence: say it as if more follows."
        english.firstOrNull()?.isLowerCase()==true->"Said after a distance, as in “In 100 metres, …”."
        else->null
    }
}

/**
 * A recorded voice's takes in app storage (never backed up), one per phrase: where [AppVoice]
 * unpacks the voice built into the app. Complete once every phrase the coach can say has a take;
 * sentences the coach composes are joined from the phrases.
 */
class RecordedVoice(private val directory:File,override val language:String):CoachVoice {
    private val present=ConcurrentHashMap.newKeySet<String>()
    private val memory=ConcurrentHashMap<String,Pcm>()
    @Volatile private var required:List<String> = emptyList()

    init {
        directory.mkdirs()
        directory.list()?.filter {it.endsWith(".pcm")}?.forEach {present.add(it.removeSuffix(".pcm"))}
    }

    /** The words to record (VoiceScript lines); [complete] needs a take of each. Kept as file stems. */
    fun require(words:Collection<String>){required=words.map(VoiceScript::key).filter {it.isNotEmpty()}.distinct().map(::stem)}
    val total get()=required.size
    val recorded get()=required.count {it in present}
    fun has(words:String)=stem(VoiceScript.key(words)) in present
    override val complete:Boolean get()=required.isNotEmpty()&&required.all {it in present}

    override fun clip(text:String):Pcm? {
        val key=VoiceScript.key(text);if(key.isEmpty())return null
        memory[key]?.let {return it}
        val stem=stem(key);if(stem !in present)return null
        return VoiceClips.read(File(directory,"$stem.pcm"))?.also {memory[key]=it}
    }

    /** Keeps [pcm] as the take for [words], replacing any earlier one. */
    fun save(words:String,pcm:Pcm) {
        val key=VoiceScript.key(words);val stem=stem(key)
        VoiceClips.write(File(directory,"$stem.pcm"),pcm)
        memory[key]=pcm;present.add(stem)
    }

    /** Deletes every take of this language from the phone. */
    fun clear() {
        directory.listFiles()?.forEach {it.delete()}
        present.clear();memory.clear()
    }

    private fun stem(key:String)=VoiceClips.sha1(key)
}
