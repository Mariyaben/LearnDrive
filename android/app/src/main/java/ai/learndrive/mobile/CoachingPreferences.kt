package ai.learndrive.mobile

import android.content.SharedPreferences

enum class CoachingProfile(val nativeId:Int,val label:String) {
    MINIMAL(0,"Minimal"), BALANCED(1,"Balanced"), GUIDED(2,"Guided");
    companion object {
        fun stored(value:String?)=entries.firstOrNull {it.name==value}?:BALANCED
    }
}

data class CoachingSettings(val profile:CoachingProfile=CoachingProfile.BALANCED,
    val voiceEnabled:Boolean=true,val revision:Long=1)

/** Local presentation preferences contain no route, location or provider content. */
class CoachingPreferences(private val preferences:SharedPreferences) {
    fun load()=CoachingSettings(CoachingProfile.stored(preferences.getString("coachingProfile",null)),
        preferences.getBoolean("coachingVoice",true),preferences.getLong("coachingRevision",1).coerceAtLeast(1))
    fun save(value:CoachingSettings) {
        preferences.edit().putString("coachingProfile",value.profile.name)
            .putBoolean("coachingVoice",value.voiceEnabled).putLong("coachingRevision",value.revision).apply()
    }
}
