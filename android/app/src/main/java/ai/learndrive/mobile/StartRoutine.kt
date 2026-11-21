package ai.learndrive.mobile

import ai.learndrive.mobile.audio.VoicePhrases

/**
 * What the coach says at the start of a drive, parked, before the car moves: the checks before
 * starting (doors, seat, steering, seat belt, mirrors, handbrake and neutral or P, then the engine)
 * and moving off. Today's focus is said between the two.
 *
 * The routine is the one taught to learners where traffic keeps left and cars are right-hand drive
 * (UK DVSA syllabus, units 1 and 2, and Safe Driving for Life, "Moving off safely and smoothly"):
 * seat, head restraint, steering and mirrors set; the gears disengaged when starting the engine;
 * moving off by preparing (first gear, a little accelerator, the clutch to its biting point),
 * observing (mirrors, then over the right shoulder when moving off from the left of the road;
 * a signal if it would help anyone) and moving (handbrake off, clutch up a little more, gently;
 * then the foot fully off the clutch). An automatic: foot on the brake before selecting D, then
 * the same looks, handbrake off and the brake eased off.
 *
 * The coaching level sets how much is said: Guided every step with time to do it, Balanced the
 * checks and moving off in one sentence each, Minimal a reminder each. The car is never assumed to be
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
            CoachingProfile.GUIDED->listOf(
                step(checks,"Doors","Before you start, a few checks. First, make sure all the doors are shut properly.",
                    "തുടങ്ങുന്നതിന് മുമ്പ് കുറച്ച് കാര്യങ്ങൾ ഉറപ്പാക്കാം. ആദ്യം, എല്ലാ ഡോറുകളും ശരിക്കും അടഞ്ഞിട്ടുണ്ടോ എന്ന് നോക്കുക.",2.0),
                if(manual)step(checks,"Seat","Adjust your seat so you can press the clutch right down with your knee still slightly bent.",
                    "ക്ലച്ച് മുഴുവനായി ചവിട്ടുമ്പോഴും കാൽമുട്ട് അല്പം മടങ്ങിയിരിക്കുന്ന രീതിയിൽ സീറ്റ് ക്രമീകരിക്കുക.",3.0)
                else step(checks,"Seat","Adjust your seat so you can press the brake pedal right down with your knee still slightly bent.",
                    "ബ്രേക്ക് പെഡൽ മുഴുവനായി ചവിട്ടുമ്പോഴും കാൽമുട്ട് അല്പം മടങ്ങിയിരിക്കുന്ന രീതിയിൽ സീറ്റ് ക്രമീകരിക്കുക.",3.0),
                step(checks,"Steering","Hold the steering wheel with both hands at quarter to three, with your arms slightly bent.",
                    "രണ്ടു കൈകൊണ്ടും സ്റ്റിയറിംഗ് പിടിക്കുക, ക്ലോക്കിലെ ഒമ്പതിന്റെയും മൂന്നിന്റെയും സ്ഥാനത്ത്. കൈകൾ അല്പം മടങ്ങിയിരിക്കണം.",2.0),
                step(checks,"Seat belt","Fasten your seat belt, and check that everyone else has theirs on.",
                    "സീറ്റ് ബെൽറ്റ് ഇടുക. വണ്ടിയിലുള്ള എല്ലാവരും ബെൽറ്റ് ഇട്ടിട്ടുണ്ടോ എന്നും നോക്കുക.",3.0),
                step(checks,"Mirrors","Set the inside mirror to show the whole back window, and each side mirror to show the road behind, with a little of your own car.",
                    "ഉള്ളിലെ മിററിൽ പിന്നിലെ ഗ്ലാസ് മുഴുവനായി കാണണം. സൈഡ് മിററുകളിൽ പിന്നിലെ റോഡും നമ്മുടെ വണ്ടിയുടെ ഒരു ചെറിയ ഭാഗവും കാണണം.",4.0),
                if(manual)step(checks,"Handbrake","Check that the handbrake is on and the gear lever is in neutral.",
                    "ഹാൻഡ്ബ്രേക്ക് ഇട്ടിട്ടുണ്ടോ എന്നും ഗിയർ ന്യൂട്രലിലാണോ എന്നും ഉറപ്പാക്കുക.",2.0)
                else step(checks,"Handbrake","Check that the handbrake is on and the gear selector is in P.",
                    "ഹാൻഡ്ബ്രേക്ക് ഇട്ടിട്ടുണ്ടോ എന്നും ഗിയർ സെലക്ടർ പി-യിലാണോ എന്നും ഉറപ്പാക്കുക.",2.0),
                if(manual)step(checks,"Engine","If the engine isn't running yet, press the clutch down and start it. Check that no warning lights stay on.",
                    "എഞ്ചിൻ ഓഫാണെങ്കിൽ, ക്ലച്ച് ചവിട്ടി സ്റ്റാർട്ട് ചെയ്യുക. വാണിംഗ് ലൈറ്റുകൾ ഒന്നും കത്തിനിൽക്കുന്നില്ലെന്ന് നോക്കുക.",4.0)
                else step(checks,"Engine","If the engine isn't running yet, keep your foot on the brake and start it. Check that no warning lights stay on.",
                    "എഞ്ചിൻ ഓഫാണെങ്കിൽ, ബ്രേക്ക് ചവിട്ടിപ്പിടിച്ച് സ്റ്റാർട്ട് ചെയ്യുക. വാണിംഗ് ലൈറ്റുകൾ ഒന്നും കത്തിനിൽക്കുന്നില്ലെന്ന് നോക്കുക.",4.0),
            )+if(manual)listOf(
                step(move,"First gear","When you're ready to move off: press the clutch right down and select first gear.",
                    "വണ്ടി എടുക്കാൻ തയ്യാറാകുമ്പോൾ: ക്ലച്ച് മുഴുവനായി ചവിട്ടി ഫസ്റ്റ് ഗിയറിടുക.",2.0),
                step(move,"Biting point","Press the accelerator gently and hold it. Then bring the clutch up slowly until the engine sound drops slightly. That's the biting point: keep your feet still there.",
                    "ആക്സിലറേറ്റർ പതുക്കെ ചവിട്ടിപ്പിടിക്കുക. എന്നിട്ട് എഞ്ചിന്റെ ശബ്ദം അല്പം താഴുന്നതുവരെ ക്ലച്ച് പതുക്കെ ഉയർത്തുക. അതാണ് ബൈറ്റിംഗ് പോയിന്റ്: അവിടെ കാലുകൾ അനക്കാതെ വെക്കുക.",3.0),
                LOOK.on(language),SIGNAL.on(language),
                step(move,"Move off","When it's clear, release the handbrake. Bring the clutch up a little more, add a little accelerator, and move off gently. Then take your foot fully off the clutch.",
                    "വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്യുക. ക്ലച്ച് കുറച്ചുകൂടി ഉയർത്തി, അല്പം ആക്സിലറേറ്റർ കൊടുത്ത്, പതുക്കെ മുന്നോട്ട് എടുക്കുക. പിന്നെ ക്ലച്ചിൽ നിന്ന് കാൽ പൂർണ്ണമായി എടുക്കുക.",0.0),
            ) else listOf(
                step(move,"Drive","When you're ready to move off: keep your right foot firmly on the brake and select D.",
                    "വണ്ടി എടുക്കാൻ തയ്യാറാകുമ്പോൾ: വലതുകാൽ ബ്രേക്കിൽ അമർത്തിപ്പിടിച്ച് ഗിയർ ഡി-യിലേക്ക് മാറ്റുക.",2.0),
                LOOK.on(language),SIGNAL.on(language),
                step(move,"Move off","When it's clear, release the handbrake, then lift your foot off the brake slowly. The car will start to creep forward; add a little accelerator if it needs it.",
                    "വഴി ഒഴിയുമ്പോൾ ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്ത്, ബ്രേക്കിൽ നിന്ന് പതുക്കെ കാൽ എടുക്കുക. വണ്ടി പതുക്കെ നീങ്ങിത്തുടങ്ങും; ആവശ്യമെങ്കിൽ അല്പം ആക്സിലറേറ്റർ കൊടുക്കുക.",0.0),
            )
            CoachingProfile.BALANCED->listOf(
                if(manual)step(checks,"Before you start","Before you start: doors shut, seat and mirrors set, seat belt on. Handbrake on and neutral before you start the engine.",
                    "തുടങ്ങുന്നതിന് മുമ്പ്: ഡോറുകൾ അടച്ചിട്ടുണ്ടോ, സീറ്റും മിററുകളും ശരിയാണോ, സീറ്റ് ബെൽറ്റ് ഇട്ടോ എന്ന് നോക്കുക. എഞ്ചിൻ സ്റ്റാർട്ട് ചെയ്യുന്നതിന് മുമ്പ് ഹാൻഡ്ബ്രേക്ക് ഇട്ട് ഗിയർ ന്യൂട്രലിലാക്കുക.",3.0)
                else step(checks,"Before you start","Before you start: doors shut, seat and mirrors set, seat belt on. Handbrake on and P before you start the engine.",
                    "തുടങ്ങുന്നതിന് മുമ്പ്: ഡോറുകൾ അടച്ചിട്ടുണ്ടോ, സീറ്റും മിററുകളും ശരിയാണോ, സീറ്റ് ബെൽറ്റ് ഇട്ടോ എന്ന് നോക്കുക. എഞ്ചിൻ സ്റ്റാർട്ട് ചെയ്യുന്നതിന് മുമ്പ് ഹാൻഡ്ബ്രേക്ക് ഇട്ട് ഗിയർ പി-യിലാക്കുക.",3.0),
                if(manual)step(move,"Moving off","To move off: first gear and the biting point, then the mirrors and a look over your right shoulder. Signal if it would help anyone, then release the handbrake and move off gently.",
                    "വണ്ടി എടുക്കാൻ: ഫസ്റ്റ് ഗിയറിട്ട് ബൈറ്റിംഗ് പോയിന്റിൽ പിടിക്കുക, മിററുകൾ നോക്കി വലത് തോളിന് മുകളിലൂടെ തിരിഞ്ഞുനോക്കുക. ആവശ്യമെങ്കിൽ ഇൻഡിക്കേറ്റർ ഇട്ട്, ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്ത് പതുക്കെ മുന്നോട്ട് എടുക്കുക.",0.0)
                else step(move,"Moving off","To move off: foot on the brake and select D, then the mirrors and a look over your right shoulder. Signal if it would help anyone, then release the handbrake and ease off the brake.",
                    "വണ്ടി എടുക്കാൻ: ബ്രേക്ക് ചവിട്ടിപ്പിടിച്ച് ഗിയർ ഡി-യിലേക്ക് മാറ്റുക, മിററുകൾ നോക്കി വലത് തോളിന് മുകളിലൂടെ തിരിഞ്ഞുനോക്കുക. ആവശ്യമെങ്കിൽ ഇൻഡിക്കേറ്റർ ഇട്ട്, ഹാൻഡ്ബ്രേക്ക് റിലീസ് ചെയ്ത് ബ്രേക്കിൽ നിന്ന് പതുക്കെ കാൽ എടുക്കുക.",0.0),
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

    /** A step shared by both cars, in either language. */
    private class Shared(val part:Part,val title:String,val en:String,val ml:String,val pause:Double) {
        fun on(language:String)=Step(part,title,en,if(language==VoicePhrases.MALAYALAM)ml else en,pause)
    }
    private val LOOK=Shared(Part.MOVE_OFF,"Look","Now look around: the inside mirror, the right mirror, and over your right shoulder.",
        "ഇനി ചുറ്റും നോക്കുക: ഉള്ളിലെ മിററും വലതുവശത്തെ മിററും നോക്കി, വലത് തോളിന് മുകളിലൂടെ തിരിഞ്ഞുനോക്കുക.",3.0)
    private val SIGNAL=Shared(Part.MOVE_OFF,"Signal","If it would help anyone, signal right.",
        "ആർക്കെങ്കിലും ഉപകാരപ്പെടുമെങ്കിൽ, വലത്തേക്ക് ഇൻഡിക്കേറ്റർ ഇടുക.",2.0)
}
