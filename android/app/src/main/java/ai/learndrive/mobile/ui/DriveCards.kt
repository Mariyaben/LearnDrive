package ai.learndrive.mobile.ui

import ai.learndrive.mobile.navigation.google.GoogleManeuverKind
import kotlin.math.roundToInt

/** The coach card: the situation, the one thing to do now, and the rest of what was said. */
data class CoachCard(
    /** "MANEUVER PREP", "PERSON AHEAD", "OBSERVING". */
    val header:String,
    val tone:CoachTone,
    /** The one action to take now: "SIGNAL RIGHT". */
    val title:String,
    /** Everything else that was said: "Check mirrors • Ease off gas". */
    val subtitle:String,
    /** A CoachIcon kind. */
    val icon:String,
    val chips:List<CardChip> =emptyList(),
    /** A full sentence rather than one action (idle and parked states): shown in normal case. */
    val sentence:Boolean=false
)
enum class CoachTone {ROUTE,ROAD_USER,URGENT,ROAD,CONTROL,CALM,PARKED}
data class CardChip(val text:String,val active:Boolean=false)

/** The turn card, from Google's turn-by-turn feed (memory only) or the map the app reads itself. */
data class TurnCard(
    /** "230 m", "1.2 km", "Now"; null when the distance is not known. */
    val distance:String?,
    /** "Turn right onto Link SH 56". */
    val instruction:String,
    /** A route number read from the road name: "SH 56", "NH 66". */
    val shield:String?,
    val icon:String,
    /** The step after this one: "Continue 4.5 km to Odayanchal". */
    val then:String?=null,
    val thenIcon:String?=null,
    /** Whose data this is ("Google Maps", "© OpenStreetMap"): shown in the card, as their terms require. */
    val attribution:String?=null,
    /** Navigating with turn-by-turn directions (green), or only reading the road ahead (dark). */
    val guiding:Boolean=true
)

/** Builds the cards from what the coach said and what the route provider reports. */
object DriveCards {
    // Instruction fragments (ld::Fragment): the planner composes one sentence per moment from them.
    private val ROAD_USER=8..19
    private val NAVIGATION=(45..66).toSet()+setOf(5,6,7)
    private val SLOPE=setOf(1,2,3,67,68,69,70)
    private val BEND=setOf(4,74,75,76,77)
    private const val BUMP=44
    private val DRIFT=setOf(20,21)
    private val TURN_NOW=setOf(60,61,64,65)

    /** What each action fragment asks, short enough to read at a glance. */
    private val ACTION=mapOf(
        22 to "Brake gently",23 to "Brake now",24 to "Be ready to stop",25 to "Stop and wait",26 to "Ease off gas",
        27 to "Cover the brake",28 to "Keep left",29 to "Give them space",30 to "Keep a safe gap",31 to "Change down to 2nd",
        32 to "Change down to 3rd",33 to "Clutch down",34 to "Hold the brake",35 to "Check mirrors",36 to "Signal left",
        37 to "Signal right",38 to "Look both ways",39 to "Steady gas",40 to "Back to lane centre",41 to "Let them pass",
        42 to "Look again first",43 to "Sound the horn",71 to "Stay in gear",72 to "Brake to control speed",73 to "Change down a gear")

    private fun clean(s:String)=s.trim().trimEnd('.',',',':').trim()
    private fun ageChip(age:Int?)=age?.let {CardChip(if(it<2)"Just now" else "${it}s ago")}

    /**
     * A sentence the coach composed from fragments ([ids], in speaking order; [text] gives a
     * fragment's words). [origin] is who proposed it (5 a route step, 7 a road user, 6 vehicle
     * control, 3 the road ahead).
     */
    fun fromFragments(ids:IntArray,origin:Int,ageSeconds:Int?,text:(Int)->String):CoachCard {
        val contexts=ids.filter {it !in ACTION.keys}
        val actions=ids.filter {it in ACTION.keys}
        val roadUser=contexts.firstOrNull {it in ROAD_USER}
        val urgent=23 in actions
        val routeStep=origin==5||contexts.any {it in NAVIGATION}
        val (header,tone,icon)=when {
            roadUser!=null->Triple(clean(text(roadUser)).uppercase(),if(urgent)CoachTone.URGENT else CoachTone.ROAD_USER,roadUserIcon(roadUser))
            routeStep->Triple(if(contexts.any {it in TURN_NOW}||38 in actions)"TURN NOW" else "MANEUVER PREP",
                if(urgent)CoachTone.URGENT else CoachTone.ROUTE,turnIcon(contexts))
            contexts.any {it in BEND}->contexts.first {it in BEND}.let {Triple(clean(text(it)).uppercase(),CoachTone.ROAD,bendIcon(it))}
            contexts.any {it in SLOPE}->Triple(clean(text(contexts.first {it in SLOPE})).uppercase(),CoachTone.ROAD,"hill")
            BUMP in contexts->Triple("SPEED BUMP AHEAD",CoachTone.ROAD,"bump")
            contexts.any {it in DRIFT}->Triple(clean(text(contexts.first {it in DRIFT})).uppercase(),CoachTone.ROAD,"lane")
            else->Triple("VEHICLE CONTROL",if(urgent)CoachTone.URGENT else CoachTone.CONTROL,"gear")
        }
        // The headline: what to do now. The planner orders actions most urgent first; a route
        // step's defining action is its signal, then its braking, then looking both ways.
        val headline=when {
            urgent->23
            routeStep&&roadUser==null->listOf(36,37,22,38,33).firstOrNull {it in actions}?:actions.firstOrNull()
            else->actions.firstOrNull()
        }
        // The rest: other things around (a slope, a second road user) and the other actions. A
        // route step's own place ("In 50 metres, turn right") is on the turn card already.
        val headerFragment=roadUser?:contexts.firstOrNull {(it in BEND||it in SLOPE||it==BUMP||it in DRIFT)&&!routeStep}
        val around=contexts.filter {it!=headerFragment&&it !in NAVIGATION}.map {clean(text(it))}
        val rest=actions.filter {it!=headline}.map {ACTION.getValue(it)}
        val title=headline?.let {ACTION.getValue(it).uppercase()}?:header
        val chips=when(tone) {
            CoachTone.ROUTE->turnSteps(contexts,actions)
            else->listOfNotNull(CardChip(source(origin,contexts),true),ageChip(ageSeconds))
        }
        return CoachCard(if(headline==null&&roadUser==null)categoryOf(origin) else header,tone,title,(around+rest).joinToString(" • "),icon,chips)
    }

    /** A recorded sentence (not composed): "Change up to third gear", "Straighten the wheel and add gentle accelerator". */
    fun fromText(sentence:String,origin:Int,ageSeconds:Int?):CoachCard {
        val parts=sentence.split(". ").map(::clean).filter(String::isNotBlank)
        if(parts.isEmpty())return idle("",null)
        val clauses=parts.flatMap {p->if(p.length>22&&" and " in p)p.split(" and ").map(::clean) else listOf(p)}
        val lower=sentence.lowercase()
        val signal=when {"signal left" in lower->"Signal left";"signal right" in lower->"Signal right";else->null}
        val route=origin==5||signal!=null||"turn" in lower&&origin!=7
        val tone=when {"brake now" in lower||"emergency" in lower->CoachTone.URGENT;route->CoachTone.ROUTE;origin==7||origin==4->CoachTone.ROAD_USER
            origin==6->CoachTone.CONTROL;else->CoachTone.ROAD}
        // A road-user recording starts with who it is about: "Person ahead. Ease off the accelerator…".
        val who=if((origin==7||origin==4)&&parts.size>1)parts[0] else null
        val actions=if(who!=null)clauses.drop(1) else clauses
        val title=(signal?:actions.firstOrNull()?:clauses.first()).uppercase()
        val subtitle=actions.filter {!it.equals(signal,true)&&it.uppercase()!=title}.joinToString(" • ") {it.replaceFirstChar(Char::uppercase)}
        val header=who?.uppercase()?:if("straighten" in lower)"AFTER THE TURN" else when(tone) {
            CoachTone.ROUTE->if("here" in lower||"look both ways" in lower)"TURN NOW" else "MANEUVER PREP"
            CoachTone.CONTROL->"VEHICLE CONTROL";CoachTone.URGENT->"ACT NOW";else->categoryOf(origin)
        }
        val icon=when {
            route->if("left" in lower)"left" else if("right" in lower)"right" else "straight"
            tone==CoachTone.CONTROL->"gear";"hill" in lower||"uphill" in lower||"downhill" in lower->"hill";"bend" in lower->"bendLeft"
            "bump" in lower->"bump";tone==CoachTone.ROAD_USER||tone==CoachTone.URGENT->"warning";else->"coach"
        }
        val chips=if(tone==CoachTone.ROUTE&&"straighten" !in lower)turnStepsFromText(lower)
            else listOfNotNull(CardChip(if(tone==CoachTone.CONTROL)"From your speed" else categoryOf(origin).lowercase().replaceFirstChar(Char::uppercase),true),ageChip(ageSeconds))
        return CoachCard(header,tone,title,subtitle,icon,chips)
    }

    /** Between instructions: what is coming up, and never an "all clear". */
    fun idle(whatsNext:String,hint:String?):CoachCard=
        CoachCard("OBSERVING",CoachTone.CALM,whatsNext.ifBlank {"Watching the road"},hint.orEmpty(),"eye",emptyList(),sentence=true)
    /** An instruction left unsaid because the learner had already slowed ("you first"). */
    fun held(caption:String):CoachCard=CoachCard("YOU FIRST",CoachTone.CALM,caption,"The coach has seen it. Keep watching.","check",emptyList(),sentence=true)
    fun parked(message:String,hint:String):CoachCard=CoachCard("PARKED",CoachTone.PARKED,message,hint,"coach",emptyList(),sentence=true)
    /** The drive's one focus, while it is briefed at the start (instructions still take the card). */
    fun focus(title:String,detail:String):CoachCard=CoachCard("TODAY'S FOCUS",CoachTone.CALM,title,detail,"coach",emptyList(),sentence=true)

    private fun categoryOf(origin:Int)=when(origin){5->"ROUTE";6->"VEHICLE CONTROL";7,4->"ROAD USER";3->"ROAD AHEAD";else->"COACH"}
    private fun source(origin:Int,contexts:List<Int>)=when {
        contexts.any {it in ROAD_USER}->"Camera";contexts.any {it in SLOPE}->"Elevation";contexts.any {it in BEND}->"Route line"
        BUMP in contexts->"Map";origin==6->"From your speed";contexts.any {it in DRIFT}->"Lane lines";else->"Coach"
    }
    private fun roadUserIcon(id:Int)=when(id){13,14,15,16->"person";11,17->"bike";else->"car"}
    private fun bendIcon(id:Int)=if(id==75||id==77)"bendRight" else "bendLeft"
    private fun turnIcon(contexts:List<Int>)=when {
        contexts.any {it in setOf(6,49,60,62,51)}->"left";contexts.any {it in setOf(7,50,61,63,52)}->"right"
        53 in contexts->"uturn";contexts.any {it in 54..58}->"roundabout";59 in contexts||65 in contexts->"flag";else->"straight"
    }
    /** The three steps of a turn, the one this sentence belongs to lit: what was said, not what was done. */
    private fun turnSteps(contexts:List<Int>,actions:List<Int>):List<CardChip> {
        val step=when {contexts.any {it in TURN_NOW}||38 in actions||33 in actions->2;22 in actions||31 in actions||32 in actions->1;else->0}
        return listOf("Mirrors · signal","Brake · gear","Turn").mapIndexed {i,s->CardChip(s,i==step)}
    }
    private fun turnStepsFromText(lower:String):List<CardChip> {
        val step=when {"here" in lower||"look both ways" in lower||"clutch down if" in lower->2;"brake" in lower||"change down" in lower->1;else->0}
        return listOf("Mirrors · signal","Brake · gear","Turn").mapIndexed {i,s->CardChip(s,i==step)}
    }

    // ---- the turn card
    /** A route number in a road name: "Link SH 56" -> "SH 56"; "Kanhangad - Panathoor Rd" -> null. */
    fun shield(road:String?):String? {
        if(road.isNullOrBlank())return null
        val m=Regex("""\b(NH|SH|MDR|ODR|AH)\s*-?\s*(\d{1,4}[A-Z]?)\b""").find(road)?:return null
        return "${m.groupValues[1]} ${m.groupValues[2]}"
    }
    fun distance(m:Double?):String?=when {m==null||!m.isFinite()||m<0->null;m>=1000->"%.1f km".format(m/1000);m>=20->"${(m/10).roundToInt()*10} m";else->"Now"}
    fun maneuverIcon(kind:GoogleManeuverKind,side:Int=0)=when(kind) {
        GoogleManeuverKind.LEFT_TURN,GoogleManeuverKind.SHARP_LEFT,GoogleManeuverKind.SLIGHT_LEFT,GoogleManeuverKind.KEEP_LEFT,GoogleManeuverKind.FORK_LEFT->"left"
        GoogleManeuverKind.RIGHT_TURN,GoogleManeuverKind.SHARP_RIGHT,GoogleManeuverKind.SLIGHT_RIGHT,GoogleManeuverKind.KEEP_RIGHT,GoogleManeuverKind.FORK_RIGHT->"right"
        GoogleManeuverKind.U_TURN->"uturn";GoogleManeuverKind.ROUNDABOUT->"roundabout";GoogleManeuverKind.ARRIVAL->"flag"
        GoogleManeuverKind.ON_RAMP,GoogleManeuverKind.OFF_RAMP,GoogleManeuverKind.MERGE->if(side<0)"left" else if(side>0)"right" else "straight"
        else->"straight"
    }
    fun maneuverText(kind:GoogleManeuverKind,roundaboutExit:Int?)=when(kind) {
        GoogleManeuverKind.LEFT_TURN->"Turn left";GoogleManeuverKind.RIGHT_TURN->"Turn right"
        GoogleManeuverKind.SLIGHT_LEFT->"Slight left";GoogleManeuverKind.SLIGHT_RIGHT->"Slight right"
        GoogleManeuverKind.SHARP_LEFT->"Sharp left";GoogleManeuverKind.SHARP_RIGHT->"Sharp right"
        GoogleManeuverKind.KEEP_LEFT,GoogleManeuverKind.FORK_LEFT->"Keep left";GoogleManeuverKind.KEEP_RIGHT,GoogleManeuverKind.FORK_RIGHT->"Keep right"
        GoogleManeuverKind.MERGE->"Merge";GoogleManeuverKind.ON_RAMP->"Take the ramp";GoogleManeuverKind.OFF_RAMP->"Take the exit"
        GoogleManeuverKind.U_TURN->"Make a U-turn"
        GoogleManeuverKind.ROUNDABOUT->roundaboutExit?.let {"At the roundabout, take exit $it"}?:"Enter the roundabout"
        GoogleManeuverKind.ARRIVAL->"Arrive at your destination";GoogleManeuverKind.STRAIGHT->"Continue straight"
        else->"Follow the route"
    }
    /**
     * Google's next step, as the card shows it. [instruction] is Google's own wording when it gives
     * one ("Turn right onto Link SH 56"); otherwise the maneuver and road name are put together.
     */
    fun googleTurn(kind:GoogleManeuverKind,distanceMetres:Double?,instruction:String?,road:String?,roundaboutExit:Int?,side:Int,
                   next:GoogleManeuverKind,nextGapMetres:Double?,nextRoad:String?,destination:String?):TurnCard {
        val own=maneuverText(kind,roundaboutExit)+(if(!road.isNullOrBlank()&&kind!=GoogleManeuverKind.ARRIVAL)" onto $road" else "")
        val text=instruction?.takeIf {it.isNotBlank()}?.replace(Regex("\\s+")," ")?:own
        val gap=distance(nextGapMetres)?.takeIf {it!="Now"}
        val then=when {
            gap==null->null
            next==GoogleManeuverKind.ARRIVAL->"Continue $gap"+(destination?.takeIf {it.isNotBlank()}?.let {" to $it"}?:" to your destination")
            next==GoogleManeuverKind.UNKNOWN||next==GoogleManeuverKind.STRAIGHT->"Continue $gap"+(nextRoad?.takeIf {it.isNotBlank()}?.let {" on $it"}?:"")
            else->maneuverText(next,null)+" after $gap"
        }
        val thenIcon=if(then==null)null else if(next==GoogleManeuverKind.ARRIVAL||next==GoogleManeuverKind.UNKNOWN)"straight" else maneuverIcon(next)
        return TurnCard(distance(distanceMetres),text,shield(road),maneuverIcon(kind,side),then,thenIcon,"Google Maps",true)
    }
}
