package ai.learndrive.mobile

/**
 * What a learner meets while parked (0.10.0): the first-open questions, the coaching level put as
 * how much they have driven, the hint that they may be ready for less coaching, when "Who's
 * driving?" is asked, and the developer tools kept out of a learner's way. No Android here, so the
 * rules are tested on the desk.
 */
object ParkedFlow {
    /** First open, once: one question per screen, in this order. */
    enum class Step(val title:String) {
        WELCOME("Welcome to LearnDrive"),
        LANGUAGE("Which language should the coach speak?"),
        NAME("What's your name?"),
        CAR("Which car are you learning in?"),
        LEVEL("How much have you driven?"),
        MOUNT("Mount the phone");
        val number get()=ordinal+1
        val next:Step? get()=entries.getOrNull(ordinal+1)
        val previous:Step? get()=entries.getOrNull(ordinal-1)
    }

    /** The coaching levels as a learner reads them: most help first. */
    val LEVELS=listOf(CoachingProfile.GUIDED,CoachingProfile.BALANCED,CoachingProfile.MINIMAL)

    /** The coaching level, asked on first open as how much the learner has driven. */
    enum class Experience(val label:String,val profile:CoachingProfile) {
        STARTING("Just starting",CoachingProfile.GUIDED),
        SOME("Some practice",CoachingProfile.BALANCED),
        NEARLY("Nearly test-ready",CoachingProfile.MINIMAL);
        companion object {fun of(profile:CoachingProfile)=entries.first {it.profile==profile}}
    }

    /** What the learner hears at [profile], in one line. */
    fun levelLine(profile:CoachingProfile)=when(profile) {
        CoachingProfile.GUIDED->"The most coaching: routine guidance and extra reminders."
        CoachingProfile.BALANCED->"Routine coaching, spaced out so it isn't constant."
        CoachingProfile.MINIMAL->"Essential cues only."
    }

    /** "Who's driving?" comes first, before a drive, only when the phone has more than one learner
     * and nobody has been asked since the last drive. */
    fun askWhoIsDriving(learners:Int,askedSinceLastDrive:Boolean)=learners>1&&!askedSinceLastDrive

    /** The developer tools stay hidden from learners: tapping the version [TAPS] times in a row, each
     * within [GAP] seconds of the last, shows them. */
    class DeveloperUnlock {
        private var taps=0
        private var last=Double.NEGATIVE_INFINITY
        /** Taps still needed after this one; 0 means this tap unlocked them. */
        fun tap(t:Double):Int {
            taps=if(t-last in 0.0..GAP)taps+1 else 1;last=t
            val left=(TAPS-taps).coerceAtLeast(0)
            if(left==0)taps=0
            return left
        }
        companion object {const val TAPS=7;const val GAP=1.5}
    }

    /** How many focused drives at one level the hint looks at. */
    const val DRIVES=3

    /** The next level down in coaching when the learner's last [DRIVES] focused drives since the level
     * was set ([since], ms) each had at least two things measured and at least two thirds of them
     * done as the coach asks; null otherwise. A hint only: the learner decides. */
    fun nextLevel(profile:CoachingProfile,history:List<DriveFocus.Past>,since:Long):CoachingProfile? {
        val less=when(profile) {
            CoachingProfile.GUIDED->CoachingProfile.BALANCED
            CoachingProfile.BALANCED->CoachingProfile.MINIMAL
            CoachingProfile.MINIMAL->return null
        }
        val recent=history.filter {it.at>=since}.takeLast(DRIVES)
        return less.takeIf {recent.size==DRIVES&&recent.all {it.measured>=2&&3*it.good>=2*it.measured}}
    }

    /** Each practised skill for Progress: how many focused drives, and how the last one went. */
    fun skillLines(history:List<DriveFocus.Past>):List<String> = DriveFocus.Topic.entries.mapNotNull {topic->
        val drives=history.filter {it.topic==topic}
        val last=drives.lastOrNull()?:return@mapNotNull null
        val name=topic.noun.replaceFirstChar {it.titlecase()}
        val count=if(drives.size==1)"1 drive" else "${drives.size} drives"
        "$name · $count · last time "+if(last.measured>0)"${last.good} of ${last.measured} as the coach asks" else "nothing measured"
    }
}
