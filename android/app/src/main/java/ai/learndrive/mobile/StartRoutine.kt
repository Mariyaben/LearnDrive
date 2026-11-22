package ai.learndrive.mobile

import ai.learndrive.mobile.audio.VoicePhrases

/**
 * What the coach says at the start of a drive, parked, before the car moves: the most important
 * checks before starting (seat belt, seat and mirrors, handbrake on and the gear in neutral or P)
 * and moving off (first gear and the biting point, or the brake and D; the mirrors and a look over
 * the right shoulder; handbrake off when it is clear). Today's focus is said between the two. The
 * full routine, every check with its detail, is in the [Handbook], to read parked: said in full
 * before every drive it ran to about two minutes (the user, 2026-11-22).
 *
 * The routine is the one taught to learners where traffic keeps left and cars are right-hand drive
 * (UK DVSA syllabus, units 1 and 2, and Safe Driving for Life, "Moving off safely and smoothly"):
 * the gears disengaged when starting the engine; moving off by preparing, observing (over the right
 * shoulder when moving off from the left of the road) and moving.
 *
 * The coaching level sets how much is said: Guided three short steps with time to do each (about
 * half a minute), Balanced one sentence each, Minimal a reminder each. The car is never assumed to be
 * in any state: each step says what to check or do, and the phone cannot see whether it was done.
 */
object StartRoutine {
    enum class Part {CHECKS,MOVE_OFF}
    /** One step: [title] for the coach card, [text] said in English (and shown), [say] in the voice
     * language, then [pause] seconds to do it before the next step. */
    data class Step(val part:Part,val title:String,val text:String,val say:String,val pause:Double)

    fun steps(language:String,manual:Boolean,level:CoachingProfile):List<Step> {
        val ml=language==VoicePhrases.MALAYALAM
        fun step(part:Part,title:String,en:String,malayalam:String,pause:Double)=Step(part,title,en,if(ml)malayalam else en,pause)
        val checks=Part.CHECKS;val move=Part.MOVE_OFF
        return when(level) {
            CoachingProfile.GUIDED->if(manual)listOf(
                step(checks,"Before you start","Before you start: seat belt on, seat and mirrors set, handbrake on, and the gear in neutral.",
                    "തുടങ്ങുന്നതിന് മുമ്പ്: സീറ്റ് ബെൽറ്റ് ഇടുക, സീറ്റും മിററുകളും ശരിയാക്കുക, ഹാൻഡ്ബ്രേക്ക് ഇട്ട് ഗിയർ ന്യൂട്രലിലാക്കുക.",4.0),
                step(move,"First gear","To move off: clutch down and first gear, then bring the clutch up slowly to the biting point.",
                    "വണ്ടി എടുക്കാൻ: ക്ലച്ച് ചവിട്ടി ഫസ്റ്റ് ഗിയറിടുക, എന്നിട്ട് ക്ലച്ച് പതുക്കെ ബൈറ്റിംഗ് പോയിന്റ് വരെ ഉയർത്തുക.",3.0),
                step(move,"Look, then move off","Check your mirrors and look over your right shoulder. When it's clear, release the handbrake and move off gently.",
                    "മിററുകൾ നോക്കി, വലത് തോളിന് മുകളിലൂടെ തിരിഞ്ഞുനോക്കുക. വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്ത് പതുക്കെ മുന്നോട്ട് എടുക്കുക.",0.0),
            ) else listOf(
                step(checks,"Before you start","Before you start: seat belt on, seat and mirrors set, handbrake on, and the gear in P.",
                    "തുടങ്ങുന്നതിന് മുമ്പ്: സീറ്റ് ബെൽറ്റ് ഇടുക, സീറ്റും മിററുകളും ശരിയാക്കുക, ഹാൻഡ്ബ്രേക്ക് ഇട്ട് ഗിയർ പി-യിലാക്കുക.",4.0),
                step(move,"Drive","To move off: keep your foot firmly on the brake and select D.",
                    "വണ്ടി എടുക്കാൻ: ബ്രേക്കിൽ കാൽ അമർത്തിപ്പിടിച്ച് ഗിയർ ഡി-യിലേക്ക് മാറ്റുക.",3.0),
                step(move,"Look, then move off","Check your mirrors and look over your right shoulder. When it's clear, release the handbrake and ease off the brake gently.",
                    "മിററുകൾ നോക്കി, വലത് തോളിന് മുകളിലൂടെ തിരിഞ്ഞുനോക്കുക. വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്ത് ബ്രേക്കിൽ നിന്ന് പതുക്കെ കാൽ എടുക്കുക.",0.0),
            )
            CoachingProfile.BALANCED->if(manual)listOf(
                step(checks,"Before you start","Before you start: seat belt, mirrors, handbrake on and neutral.",
                    "തുടങ്ങുന്നതിന് മുമ്പ് സീറ്റ് ബെൽറ്റ്, മിററുകൾ, ഹാൻഡ്ബ്രേക്ക്, ന്യൂട്രൽ എന്നിവ ഉറപ്പാക്കുക.",3.0),
                step(move,"Moving off","To move off: first gear and the biting point, then mirrors and right shoulder, and release the handbrake when it's clear.",
                    "വണ്ടി എടുക്കാൻ: ഫസ്റ്റ് ഗിയറിട്ട് ബൈറ്റിംഗ് പോയിന്റിൽ പിടിക്കുക, മിററുകളും വലത് തോളിന് മുകളിലൂടെയും നോക്കി, വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്യുക.",0.0),
            ) else listOf(
                step(checks,"Before you start","Before you start: seat belt, mirrors, handbrake on and P.",
                    "തുടങ്ങുന്നതിന് മുമ്പ് സീറ്റ് ബെൽറ്റ്, മിററുകൾ, ഹാൻഡ്ബ്രേക്ക്, ഗിയർ പി എന്നിവ ഉറപ്പാക്കുക.",3.0),
                step(move,"Moving off","To move off: foot on the brake and select D, then mirrors and right shoulder, and release the handbrake when it's clear.",
                    "വണ്ടി എടുക്കാൻ: ബ്രേക്ക് ചവിട്ടിപ്പിടിച്ച് ഗിയർ ഡി-യിലാക്കുക, മിററുകളും വലത് തോളിന് മുകളിലൂടെയും നോക്കി, വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്യുക.",0.0),
            )
            CoachingProfile.MINIMAL->listOf(
                step(checks,"Before you start","Seat belt on and mirrors set before you start.",
                    "തുടങ്ങുന്നതിന് മുമ്പ് സീറ്റ് ബെൽറ്റ് ഇടുക, മിററുകൾ ശരിയാക്കുക.",2.0),
                step(move,"Moving off","Check your mirrors and look over your right shoulder before you move off.",
                    "വണ്ടി എടുക്കുന്നതിന് മുമ്പ് മിററുകളും വലത് തോളിന് മുകളിലൂടെയും നോക്കുക.",0.0),
            )
        }
    }

    /** Every line the routine can say in [language] (both cars, every level): rendered ahead of a drive. */
    fun lines(language:String):List<String> =listOf(true,false).flatMap {manual->CoachingProfile.entries.flatMap {steps(language,manual,it).map {s->s.say}}}.distinct()
}

/**
 * The full routines and the coach's advice for each skill, to read parked (Get ready › Handbook):
 * everything the spoken starting checks ([StartRoutine]) leave out. Taught as in the UK DVSA learner
 * syllabus (units 1 and 2) and Safe Driving for Life, where traffic also keeps left in right-hand-drive
 * cars. In English, as the screens are.
 */
object Handbook {
    data class Section(val title:String,val lines:List<String>)

    val sections:List<Section> get()=listOf(
        Section("Before you start the engine",listOf(
            "Doors: all shut properly.",
            "Seat: you can press the clutch (in an automatic, the brake) right down with your knee still slightly bent, and hold the wheel with your arms slightly bent.",
            "Head restraint: its top at least as high as your eyes or the top of your ears, and close to the back of your head.",
            "Steering wheel: if it adjusts, set it so you can hold it at quarter to three and still see the instruments.",
            "Seat belt: fastened, flat and not twisted. Everyone in the car wears theirs.",
            "Mirrors: the inside mirror shows the whole back window; each side mirror shows the road behind, with a little of your own car.",
            "Handbrake on, and the gear in neutral (in an automatic, P).",
            "Start the engine with the clutch down (in an automatic, your foot on the brake). Check that no warning lights stay on.")),
        Section("Moving off in a manual car",listOf(
            "Prepare: clutch right down and first gear. Press the accelerator a little and hold it steady, then bring the clutch up slowly to the biting point, where the engine sound drops slightly. Keep your feet still.",
            "Look: the inside mirror, the right mirror, then over your right shoulder for the blind spot, as you are moving off from the left of the road. Signal right if it would help anyone. If someone is coming, wait.",
            "Move: when it's clear, release the handbrake, bring the clutch up a little more, add a little accelerator, and move off gently. Then take your foot fully off the clutch: resting it there wears the clutch.",
            "Don't turn the steering wheel until the car is moving.",
            "If the engine stalls: handbrake on, clutch down, neutral, start the engine again, and begin again from Prepare.")),
        Section("Moving off in an automatic",listOf(
            "Prepare: foot firmly on the brake, then select D.",
            "Look: the inside mirror, the right mirror, then over your right shoulder. Signal right if it would help anyone.",
            "Move: when it's clear, release the handbrake and ease off the brake: the car creeps forward. Add a little accelerator if it needs it.")),
        Section("On the road: what the coach asks",DriveFocus.Topic.entries.map {"${it.noun.replaceFirstChar {c->c.titlecase()}}: ${DriveFocus.advice(it)}"}),
        Section("Where this comes from",listOf(
            "As taught in the UK DVSA learner syllabus, where traffic also keeps left in right-hand-drive cars. Your instructor's way comes first.")))
}
