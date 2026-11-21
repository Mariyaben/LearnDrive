package ai.learndrive.mobile

import java.util.UUID

/**
 * The learners who practise with this phone, and which one is driving now. Each learner has their
 * own "You first" record, practice records and coach settings (level, voice, language), so one
 * learner's record never decides how the coach speaks to another. What belongs to the phone or the
 * car stays shared: learned speed bumps, offline maps, the car's gear and slope settings. The
 * activity offers changes only while parked and not in a session.
 *
 * Up to 0.9.7 a phone had one learner. That learner keeps the storage that existed then
 * ([storage], legacy), so updating moves nothing; each learner added later gets storage of their
 * own. Ids are random, so a learner can later be linked to an account.
 */
class Learners(private val store:KeyValue,private val newId:()->String=::randomId) {
    interface KeyValue {
        fun get(key:String):String?
        fun put(key:String,value:String)
        fun remove(key:String)
    }
    data class Learner(val id:String,val name:String)
    /** Where one learner's data is kept: SharedPreferences names, and a path under the app's files. */
    data class Storage(val settings:String,val records:String,val recordPath:String,val legacy:Boolean)

    private val ids=ArrayList<String>()
    private val names=HashMap<String,String>()
    var current:Learner
        private set

    init {
        store.get(IDS)?.split(',')?.filter {it.matches(ID)}?.distinct()?.let(ids::addAll)
        ids.forEachIndexed {i,id->names[id]=store.get(NAME+id)?.let(::cleanName)?:"Learner ${i+1}"}
        if(ids.isEmpty()) {
            // The phone's first learner: whoever practised before learners existed.
            val id=freshId()?:randomId()
            ids+=id;names[id]=FIRST_NAME
            store.put(NAME+id,FIRST_NAME);store.put(LEGACY,id);save()
        }
        val chosen=store.get(CURRENT)?.takeIf {it in ids}?:ids[0]
        current=Learner(chosen,names.getValue(chosen))
        store.put(CURRENT,chosen)
    }

    val all:List<Learner> get()=ids.map {Learner(it,names.getValue(it))}
    val count:Int get()=ids.size

    fun storage(id:String)=if(store.get(LEGACY)==id)Storage("driving_advice","learning_records","learner-record.json",true)
        else Storage("learner_$id","learner_$id","learners/$id/learner-record.json",false)

    /** The name to keep for [raw], or null: 1-30 characters once trimmed, and no other learner's. */
    fun acceptableName(raw:String,except:String?=null):String? {
        val name=cleanName(raw)?:return null
        return name.takeIf {n->ids.none {it!=except&&names[it].equals(n,ignoreCase=true)}}
    }

    /** A new learner (not selected), or null when the name is not acceptable or the list is full. */
    fun add(raw:String):Learner? {
        if(ids.size>=MAX_LEARNERS)return null
        val name=acceptableName(raw)?:return null
        val id=freshId()?:return null
        ids+=id;names[id]=name;store.put(NAME+id,name);save()
        return Learner(id,name)
    }

    fun rename(id:String,raw:String):Boolean {
        if(id !in ids)return false
        val name=acceptableName(raw,except=id)?:return false
        names[id]=name;store.put(NAME+id,name)
        if(current.id==id)current=Learner(id,name)
        return true
    }

    fun select(id:String):Boolean {
        if(id !in ids)return false
        current=Learner(id,names.getValue(id));store.put(CURRENT,id)
        return true
    }

    /** Takes [id] off the list, never the last learner; removing the one driving selects the first
     * other. Returns where their data was kept, for the caller to delete; null when not removed. */
    fun remove(id:String):Storage? {
        if(id !in ids||ids.size==1)return null
        val where=storage(id)
        ids.remove(id);names.remove(id);store.remove(NAME+id);save()
        if(where.legacy)store.remove(LEGACY)
        if(current.id==id)select(ids[0])
        return where
    }

    private fun freshId()=newId().takeIf {it.matches(ID)&&it !in ids}
    private fun save()=store.put(IDS,ids.joinToString(","))

    companion object {
        const val FIRST_NAME="Learner 1"
        const val MAX_NAME=30
        const val MAX_LEARNERS=30
        /** The learner's own keys in the shared pre-0.9.8 settings (the rest belong to the phone or car). */
        val LEGACY_SETTINGS=listOf("coachingProfile","coachingVoice","coachingRevision","youFirst","naturalVoice","voiceLanguage","driveFocus","paidUntil","paidCheckedAt")
        private const val IDS="ids"
        private const val NAME="name."
        private const val CURRENT="current"
        private const val LEGACY="legacy"
        private val ID=Regex("[0-9a-z]{6,32}")
        private val SPACES=Regex("\\s+")
        fun randomId()=UUID.randomUUID().toString().replace("-","").take(16)
        fun cleanName(raw:String):String?=raw.trim().replace(SPACES," ").takeIf {it.length in 1..MAX_NAME}
    }
}
