package ai.learndrive.mobile

import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * One focus per drive (0.9.9). Before the car moves, the coach names one skill for this drive and
 * what the route holds of it, as an instructor opens a lesson: "Today's focus is hills. Your route
 * has two climbs and a descent." After the drive the review is about that one skill, a line for
 * each hill, bend or turn. The coaching during the drive is unchanged.
 *
 * Everything is measured from the route (Google's line, its heights and its turns) and the car's
 * own speed. The phone cannot see gears, pedals, eyes or the indicator, so each result is what the
 * speed did where the road asked for something: an observation, never a grade. What is kept after
 * the drive is the skill and how many of each outcome there were ([Past]): no place, road, route
 * or Google content.
 */
object DriveFocus {
    enum class Topic(val key:String,val noun:String) {
        HILLS("hills","hills"),BENDS("bends","bends"),TURNS("turns","turns"),ROAD_USERS("road_users","road users");
        companion object {fun of(key:String?)=entries.firstOrNull {it.key==key}}
    }

    /** What a drive's hill, bend, turn or road user came to, as measured. [good]: as the coach asks. */
    enum class Outcome(val key:String,val topic:Topic,val good:Boolean) {
        CLIMB_STEADY("climb_steady",Topic.HILLS,true),CLIMB_STOPPED("climb_stopped",Topic.HILLS,true),
        CLIMB_SLOWED("climb_slowed",Topic.HILLS,false),
        DESCENT_HELD("descent_held",Topic.HILLS,true),DESCENT_BUILT("descent_built",Topic.HILLS,false),
        BEND_BEFORE("bend_before",Topic.BENDS,true),BEND_IN("bend_in",Topic.BENDS,false),BEND_FAST("bend_fast",Topic.BENDS,false),
        TURN_EARLY("turn_early",Topic.TURNS,true),TURN_LATE("turn_late",Topic.TURNS,false),TURN_FAST("turn_fast",Topic.TURNS,false),
        USER_BEFORE("user_before",Topic.ROAD_USERS,true),USER_AFTER("user_after",Topic.ROAD_USERS,false),
        USER_NONE("user_none",Topic.ROAD_USERS,false);
        companion object {fun of(key:String)=entries.firstOrNull {it.key==key}}
    }

    /** Google's turns that ask the learner to slow right down: the most they should still be doing 15 m
     * before one ([approachKmh]: easing off gently, about 1.3 m/s^2, still reaches turning speed from
     * there) and at it ([turnKmh]). */
    enum class Turn(val label:String,val approachKmh:Double,val turnKmh:Double) {
        LEFT("Left turn",25.0,20.0),RIGHT("Right turn",25.0,20.0),SHARP_LEFT("Sharp left turn",18.0,12.0),
        SHARP_RIGHT("Sharp right turn",18.0,12.0),U_TURN("U-turn",15.0,10.0),ROUNDABOUT("Roundabout",25.0,20.0)
    }

    // ------------------------------------------------------------------ the route

    data class Slope(val start:Double,val end:Double,val grade:Double,val steepest:Double) {val climb get()=grade>0}
    data class Bend(val start:Double,val angle:Double,val sharp:Boolean,val comfortableKmh:Double,val atTurn:Boolean)

    /** The route line now (ld_route_features). Positions are metres along it. [listed]: the lists were asked for. */
    class Route(val generation:Long,val length:Double,val matched:Boolean,val progress:Double,
        val heightsKnown:Boolean,val heightsTo:Double,val turnsKnown:Boolean,val turnsTo:Double,
        val listed:Boolean,val slopes:List<Slope>,val bends:List<Bend>,val turns:List<Double>) {
        companion object {
            const val HEADER=13
            /** null when there is no route line, or the values are not ld_route_features layout 1. */
            fun parse(v:DoubleArray?,full:Boolean):Route? {
                if(v==null||v.size<HEADER||v[0]!=1.0||!(v[1]>=1)||!(v[2]>0))return null
                val s=v[10].toInt();val b=v[11].toInt();val t=v[12].toInt()
                val listed=full&&s>=0&&b>=0&&t>=0&&HEADER+4*s+5*b+t==v.size
                var i=HEADER
                val slopes=if(listed)List(s) {Slope(v[i++],v[i++],v[i++],v[i++])} else emptyList()
                val bends=if(listed)List(b) {Bend(v[i++],v[i++],v[i++]!=0.0,v[i++],v[i++]!=0.0)} else emptyList()
                val turns=if(listed)List(t) {v[i++]} else emptyList()
                return Route(v[1].toLong(),v[2],v[3]!=0.0&&v[4]>=0,v[4],v[5]!=0.0,v[7],v[8]!=0.0,v[9],listed,
                    slopes.filter {it.start.isFinite()&&it.end>it.start&&it.grade.isFinite()&&it.grade!=0.0},
                    bends.filter {it.start.isFinite()&&it.angle.isFinite()&&it.comfortableKmh.isFinite()},turns)
            }
        }
    }

    /** What this drive offers each skill: how many are ahead on the route (null: not known). */
    data class Offer(val routed:Boolean,val climbs:Int?=null,val descents:Int?=null,val hillsKm:Int?=null,
        val bends:Int?=null,val bendsKm:Int?=null,val turns:Int?=null) {
        val hills:Int? get()=if(climbs!=null&&descents!=null)climbs+descents else null
        fun count(topic:Topic):Int?=when(topic){Topic.HILLS->hills;Topic.BENDS->bends;Topic.TURNS->turns;Topic.ROAD_USERS->null}
        /** The km of the route a count covers when that is only its first part (heights or turns not known further). */
        fun partKm(topic:Topic):Int?=when(topic){Topic.HILLS->hillsKm;Topic.BENDS->bendsKm;else->null}
    }

    /** [route]: the line with its lists, or null; [turns]: Google's turns still to come (its steps),
     * or null; [routed]: there is a destination route. */
    fun offer(route:Route?,turns:Int?,routed:Boolean):Offer {
        if(route==null||!route.listed)return Offer(routed,turns=turns)
        val from=if(route.matched)route.progress else 0.0
        var climbs:Int?=null;var descents:Int?=null;var hillsKm:Int?=null
        if(route.heightsKnown) {
            val ahead=route.slopes.filter {it.end>from}
            climbs=ahead.count {it.climb};descents=ahead.size-climbs
            if(route.heightsTo<route.length-PART_M)hillsKm=km(route.heightsTo)
        }
        var bends:Int?=null;var bendsKm:Int?=null
        // A bend is told from a turn only where Google's turns are known.
        if(route.turnsKnown) {
            bends=route.bends.count {!it.atTurn&&it.start>from&&it.start<=route.turnsTo}
            if(route.turnsTo<route.length-PART_M)bendsKm=km(route.turnsTo)
        }
        return Offer(true,climbs,descents,hillsKm,bends,bendsKm,turns)
    }
    private const val PART_M=200.0
    private fun km(m:Double)=maxOf(1,(m/1000).toInt())

    // ------------------------------------------------------------------ choosing

    enum class Why {FIRST,NEXT,AGAIN,CHOSEN,NOT_ON_ROUTE}
    /** This drive's focus, and why: the first, the next skill, the same again, the learner's choice, or
     * the learner's choice ([wanted]) is not on this route. [last]: the drive it repeats. [all]: the
     * learner chose "All": every skill is briefed, measured and reviewed ([topic] is then unused). */
    data class Choice(val topic:Topic,val why:Why,val wanted:Topic?=null,val last:Past?=null,val all:Boolean=false) {
        val topics:List<Topic> get()=if(all)Topic.entries else listOf(topic)
        companion object {
            /** "All": the drive covers every skill, as the coach did before drives had one focus. */
            fun everything()=Choice(Topic.ROAD_USERS,Why.CHOSEN,all=true)
        }
    }

    private fun available(topic:Topic,offer:Offer,wanted:Boolean):Boolean {
        if(topic==Topic.ROAD_USERS)return true
        val n=offer.count(topic)?:return false
        // One bend or turn is too few to make a drive of; one hill is not.
        return n>=if(wanted||topic==Topic.HILLS)1 else 2
    }

    /** As an instructor would: the learner's chosen skill if the route has it; else the same skill
     * again when last time more went wrong than right; else a skill not yet practised (the one the
     * route has most of), else the one that went least well, not the same as last time. */
    fun choose(offer:Offer,history:List<Past>,wanted:Topic?):Choice {
        if(wanted!=null&&available(wanted,offer,true))return Choice(wanted,Why.CHOSEN)
        val drives=drives(history)
        // The last drive: its one skill, or every skill measured on an "All" drive (then the one that went worst).
        val last=drives.lastOrNull().orEmpty()
        val again=last.filter {available(it.topic,offer,false)&&it.measured>=2&&2*it.good<it.measured}.minByOrNull {it.good.toDouble()/it.measured}
        val pick=if(again!=null)Choice(again.topic,Why.AGAIN,last=again)
        else {
            val open=Topic.entries.filter {available(it,offer,false)}
            val fresh=open.filter {t->history.none {it.topic==t}}
            val lastTopics=last.map {it.topic}.toSet()
            val topic=if(fresh.isNotEmpty())fresh.maxBy {offer.count(it)?:0}
            else open.filter {it !in lastTopics}.ifEmpty {open}.minWith(compareBy<Topic>({ratio(history,it)},{-ago(drives,it)}))
            Choice(topic,if(history.isEmpty())Why.FIRST else Why.NEXT)
        }
        return if(wanted!=null)pick.copy(why=Why.NOT_ON_ROUTE,wanted=wanted,last=null) else pick
    }
    private fun ratio(history:List<Past>,topic:Topic)=history.lastOrNull {it.topic==topic}?.let {if(it.measured>0)it.good.toDouble()/it.measured else .5}?:.5
    /** How many drives ago [topic] was last practised (never: more than any). */
    private fun ago(drives:List<List<Past>>,topic:Topic)=drives.size-1-drives.indexOfLast {d->d.any {it.topic==topic}}

    /** The history as drives, oldest first: an "All" drive's skills together, every other entry a drive of its own. */
    fun drives(history:List<Past>):List<List<Past>> {
        val out=ArrayList<MutableList<Past>>()
        for(p in history) {
            val previous=out.lastOrNull()?.lastOrNull()
            if(p.all&&previous!=null&&previous.all&&previous.at==p.at)out.last()+=p else out+=mutableListOf(p)
        }
        return out
    }

    /** One focused drive as kept for the learner: the skill and how many of each outcome, nothing else.
     * [all]: one of the skills of an "All" drive (kept as "hills*"; the drive's other skills share [at]). */
    data class Past(val topic:Topic,val outcomes:Map<Outcome,Int>,val at:Long,val all:Boolean=false) {
        val measured:Int get()=outcomes.values.sum()
        val good:Int get()=outcomes.entries.sumOf {if(it.key.good)it.value else 0}
        fun encode():String=topic.key+(if(all)"*" else "")+"|"+at+"|"+outcomes.entries.joinToString(",") {"${it.key.key}=${it.value}"}
        companion object {
            fun decode(line:String):Past? {
                val parts=line.trim().split('|');if(parts.size!=3)return null
                val all=parts[0].endsWith("*")
                val topic=Topic.of(parts[0].removeSuffix("*"))?:return null;val at=parts[1].toLongOrNull()?:return null
                val outcomes=LinkedHashMap<Outcome,Int>()
                if(parts[2].isNotEmpty())for(item in parts[2].split(',')) {
                    val pair=item.split('=');if(pair.size!=2)return null
                    val n=pair[1].toIntOrNull()?.takeIf {it in 0..10000}?:return null
                    Outcome.of(pair[0])?.takeIf {it.topic==topic}?.let {outcomes[it]=n}
                }
                return Past(topic,outcomes,at,all)
            }
        }
    }

    // ------------------------------------------------------------------ the brief

    /** About 20 seconds, said parked before the drive: the focus, what the route holds of it, how to
     * drive it, and that the review will go through it. In the coach's voice language. */
    fun brief(choice:Choice,offer:Offer,language:String):String=when {
        language==ai.learndrive.mobile.audio.VoicePhrases.MALAYALAM->if(choice.all)Malayalam.everything(offer) else Malayalam.brief(choice,offer)
        choice.all->English.everything(offer)
        else->English.brief(choice,offer)
    }

    /** How to drive [topic], in the brief's own words (hills: climbs and descents), for the Handbook. */
    fun advice(topic:Topic):String=English.technique(topic,Offer(true,climbs=1,descents=1))

    /** The brief on the coach card while it is said: a title and what the route holds. */
    fun card(choice:Choice,offer:Offer):Pair<String,String> {
        if(choice.all) {
            fun part(km:Int?)=km?.let {" (first $it km)"}.orEmpty()
            val route=listOfNotNull(offer.climbs?.takeIf {it>0}?.let {plural(it,"climb")+part(offer.hillsKm)},
                offer.descents?.takeIf {it>0}?.let {plural(it,"descent")+part(offer.hillsKm)},
                offer.bends?.takeIf {it>0}?.let {plural(it,"bend")+part(offer.bendsKm)},offer.turns?.takeIf {it>0}?.let {plural(it,"turn")})
            return "Today's focus: everything" to
                if(route.isEmpty())(if(offer.routed)"Hills, bends, turns and road users" else "Road users: no destination, so no route to read") else (route+"road users").joinToString(" · ")
        }
        val n=offer.count(choice.topic)
        val where=offer.partKm(choice.topic)?.let {" in the first $it km of your route"}?:" on your route"
        val detail=when(choice.topic) {
            Topic.HILLS->listOfNotNull(offer.climbs?.takeIf {it>0}?.let {plural(it,"climb")},offer.descents?.takeIf {it>0}?.let {plural(it,"descent")})
                .joinToString(" and ").takeIf {it.isNotEmpty()}?.let {"$it$where"}
            Topic.BENDS->n?.takeIf {it>0}?.let {plural(it,"bend")+where}
            Topic.TURNS->n?.takeIf {it>0}?.let {plural(it,"turn")+" on your route"}
            Topic.ROAD_USERS->"People, two-wheelers and vehicles: slow before being told"
        }
        return "Today's focus: ${choice.topic.noun}" to (detail?:"Reviewed after the drive")
    }
    private fun plural(n:Int,noun:String)="$n $noun"+if(n==1)"" else "s"

    /** The outcome that went wrong most often in [past], its count and how many of its kind there were. */
    private fun worst(past:Past):Triple<Outcome,Int,Int>? {
        val bad=past.outcomes.filter {!it.key.good&&it.value>0}.maxByOrNull {it.value}?:return null
        val kind=past.outcomes.filterKeys {sameKind(it,bad.key)}.values.sum()
        return Triple(bad.key,bad.value,kind)
    }
    private fun sameKind(a:Outcome,b:Outcome)=when {
        a.key.startsWith("climb")->b.key.startsWith("climb")
        a.key.startsWith("descent")->b.key.startsWith("descent")
        else->a.topic==b.topic
    }

    private object English {
        private val WORDS=listOf("no","one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve",
            "thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen","twenty")
        private fun n(x:Int)=WORDS.getOrNull(x)?:x.toString()
        private fun count(x:Int,noun:String)=if(x==1)"a $noun" else "${n(x)} ${noun}s"

        fun brief(c:Choice,o:Offer):String {
            val out=ArrayList<String>()
            out+=opening(c,o)
            counted(c.topic,o)?.let {out+=it}
            out+=technique(c.topic,o)
            out+=if(c.topic==Topic.ROAD_USERS)"After the drive, you'll see how often you slowed first." else "After the drive, we'll go through each one."
            return out.joinToString(" ")
        }
        /** "All": every skill, what the route holds (only when it is known for all of the route), and one line on driving them. */
        fun everything(o:Offer):String {
            val out=ArrayList<String>()
            out+="Today's focus is everything: hills, bends, turns and road users."
            if(o.hillsKm==null&&o.bendsKm==null) {
                val parts=listOfNotNull(o.climbs?.takeIf {it>0}?.let {count(it,"climb")},o.descents?.takeIf {it>0}?.let {count(it,"descent")},
                    o.bends?.takeIf {it>0}?.let {count(it,"bend")},o.turns?.takeIf {it>0}?.let {count(it,"turn")})
                if(parts.isNotEmpty())out+="Your route has "+(if(parts.size<2)parts[0] else parts.dropLast(1).joinToString(", ")+" and "+parts.last())+"."
            }
            if(!o.routed)out+="Without a destination, the review can only measure road users, but I'll coach you on everything I see."
            out+="Slow down early for every bend and turn, keep a steady speed on hills, and watch far ahead for people and vehicles."
            out+="After the drive, we'll go through each one."
            return out.joinToString(" ")
        }
        private fun opening(c:Choice,o:Offer):String=when(c.why) {
            Why.AGAIN->c.last?.let {worst(it)}?.let {(bad,k,of)->"${c.topic.noun.replaceFirstChar {it.titlecase()}} again today. Last time, ${reason(bad,k,of)}."}
                ?:"Today's focus is ${c.topic.noun} again."
            Why.NOT_ON_ROUTE->{
                val wanted=c.wanted?:c.topic
                val lack=when {
                    !o.routed->"Without a destination there is no route to read ${wanted.noun} from"
                    o.count(wanted)==null->"The ${wanted.noun} on this route could not be read"
                    else->"This route has "+when(wanted){Topic.HILLS->"no steep climbs or descents";Topic.BENDS->"hardly any bends";else->"hardly any turns"}
                }
                "$lack, so today's focus is ${c.topic.noun}."
            }
            else->"Today's focus is ${c.topic.noun}."
        }
        private fun reason(bad:Outcome,k:Int,of:Int):String {
            val part="${n(k)} of ${n(of)}"
            return when(bad) {
                Outcome.CLIMB_SLOWED->"your speed dropped on $part climbs"
                Outcome.DESCENT_BUILT->"your speed built up on $part descents"
                Outcome.BEND_IN->"you slowed inside $part bends"
                Outcome.BEND_FAST->"$part bends were faster than comfortable"
                Outcome.TURN_LATE->"you slowed late for $part turns"
                Outcome.TURN_FAST->"you were still fast at $part turns"
                Outcome.USER_AFTER->"you slowed only after being told for $part road users"
                else->"no slowing was seen for $part road users"
            }
        }
        private fun counted(t:Topic,o:Offer):String? {
            val where=o.partKm(t)?.let {"The first ${n(it)} kilometres of your route have"}?:"Your route has"
            return when(t) {
                Topic.HILLS->{
                    val c=o.climbs?:0;val d=o.descents?:0
                    if(c+d==0)null else "$where "+listOfNotNull(c.takeIf {it>0}?.let {count(it,"climb")},d.takeIf {it>0}?.let {count(it,"descent")}).joinToString(" and ")+"."
                }
                Topic.BENDS->o.bends?.takeIf {it>0}?.let {"$where ${count(it,"bend")}."}
                Topic.TURNS->o.turns?.takeIf {it>0}?.let {"Your route has ${count(it,"turn")}."}
                Topic.ROAD_USERS->null
            }
        }
        fun technique(t:Topic,o:Offer):String=when(t) {
            Topic.HILLS->{
                val climb="Before a climb, change down while the road is still flat, then keep a steady speed up it."
                val descent="From the top of a descent, hold your speed back so that it doesn't build."
                when {(o.climbs?:0)>0&&(o.descents?:0)==0->climb;(o.descents?:0)>0&&(o.climbs?:0)==0->descent;else->"$climb $descent"}
            }
            Topic.BENDS->"Slow down before each bend, while the road is still straight, and look through it to where it leads. Then keep a steady speed round it."
            Topic.TURNS->"Slow down early for each turn, not at the last moment, and look into the road you're turning into for people crossing."
            Topic.ROAD_USERS->"Watch far ahead for people, two-wheelers and vehicles, and ease off as soon as one may need room, before I have to say it."
        }
    }

    /** Malayalam as the coach's phrases say it ([ai.learndrive.mobile.audio.VoicePhrases]): numbers as words. */
    private object Malayalam {
        private val WORDS=listOf("","ഒരു","രണ്ട്","മൂന്ന്","നാല്","അഞ്ച്","ആറ്","ഏഴ്","എട്ട്","ഒമ്പത്","പത്ത്","പതിനൊന്ന്",
            "പന്ത്രണ്ട്","പതിമൂന്ന്","പതിനാല്","പതിനഞ്ച്","പതിനാറ്","പതിനേഴ്","പതിനെട്ട്","പത്തൊമ്പത്","ഇരുപത്")
        private fun n(x:Int)=WORDS.getOrNull(x)?.takeIf {x>0}?:x.toString()
        private val FOCUS=mapOf(Topic.HILLS to "കയറ്റങ്ങളിലും ഇറക്കങ്ങളിലുമാണ്",Topic.BENDS to "വളവുകളിലാണ്",
            Topic.TURNS to "തിരിവുകളിലാണ്",Topic.ROAD_USERS to "റോഡിലെ മറ്റുള്ളവരിലാണ്: ആളുകൾ, ടൂവീലറുകൾ, വാഹനങ്ങൾ")

        fun brief(c:Choice,o:Offer):String {
            val out=ArrayList<String>()
            out+=opening(c,o)
            counted(c.topic,o)?.let {out+=it}
            out+=technique(c.topic,o)
            out+=if(c.topic==Topic.ROAD_USERS)"ഡ്രൈവ് കഴിഞ്ഞ്, എത്ര തവണ നിങ്ങൾ ആദ്യം വേഗം കുറച്ചു എന്ന് നോക്കാം." else "ഡ്രൈവ് കഴിഞ്ഞ് ഓരോന്നും നമുക്ക് ഒരുമിച്ച് നോക്കാം."
            return out.joinToString(" ")
        }
        /** A noun as counted: one, one with "and" (-ഉം), many, many with "and". */
        private class Noun(val one:String,val oneAnd:String,val many:String,val manyAnd:String)
        private val CLIMB=Noun("കയറ്റം","കയറ്റവും","കയറ്റങ്ങൾ","കയറ്റങ്ങളും")
        private val DESCENT=Noun("ഇറക്കം","ഇറക്കവും","ഇറക്കങ്ങൾ","ഇറക്കങ്ങളും")
        private val BEND=Noun("വളവ്","വളവും","വളവുകൾ","വളവുകളും")
        private val TURN=Noun("തിരിവ്","തിരിവും","തിരിവുകൾ","തിരിവുകളും")
        /** "All", as [English.everything] says it. */
        fun everything(o:Offer):String {
            val out=ArrayList<String>()
            out+="ഇന്നത്തെ ഡ്രൈവിൽ ശ്രദ്ധ എല്ലാത്തിലുമാണ്: കയറ്റിറക്കങ്ങൾ, വളവുകൾ, തിരിവുകൾ, റോഡിലെ മറ്റുള്ളവർ."
            if(o.hillsKm==null&&o.bendsKm==null) {
                val items=listOfNotNull(o.climbs?.takeIf {it>0}?.let {it to CLIMB},o.descents?.takeIf {it>0}?.let {it to DESCENT},
                    o.bends?.takeIf {it>0}?.let {it to BEND},o.turns?.takeIf {it>0}?.let {it to TURN})
                // Several are joined by -ഉം on each, as the hills sentence does; one stands alone.
                val and=items.size>1
                if(items.isNotEmpty())out+="നിങ്ങളുടെ റൂട്ടിൽ "+items.joinToString(" ") {(k,w)->
                    if(k==1)"ഒരു "+(if(and)w.oneAnd else w.one) else "${n(k)} "+(if(and)w.manyAnd else w.many)}+" ഉണ്ട്."
            }
            if(!o.routed)out+="ലക്ഷ്യസ്ഥാനം നൽകാത്തതിനാൽ, ഡ്രൈവിന് ശേഷം റോഡിലെ മറ്റുള്ളവരുടെ കാര്യം മാത്രമേ അളക്കാനാകൂ. എങ്കിലും ഞാൻ കാണുന്നതെല്ലാം പറഞ്ഞുതരാം."
            out+="ഓരോ വളവിനും തിരിവിനും നേരത്തെ തന്നെ വേഗം കുറയ്ക്കുക, കയറ്റങ്ങളിൽ ഒരേ വേഗം പിടിക്കുക, ദൂരേക്ക് നോക്കി ആളുകളെയും വാഹനങ്ങളെയും ശ്രദ്ധിക്കുക."
            out+="ഡ്രൈവ് കഴിഞ്ഞ് ഓരോന്നും നമുക്ക് ഒരുമിച്ച് നോക്കാം."
            return out.joinToString(" ")
        }
        private fun focus(t:Topic)="ഇന്നത്തെ ഡ്രൈവിൽ ശ്രദ്ധ ${FOCUS.getValue(t)}."
        private fun opening(c:Choice,o:Offer):String=when(c.why) {
            Why.AGAIN->"ഇന്നും ശ്രദ്ധ ${FOCUS.getValue(c.topic)}."+(c.last?.let {worst(it)}?.let {" "+reason(it.first)}?:"")
            Why.NOT_ON_ROUTE->{
                val wanted=c.wanted?:c.topic
                val what=when(wanted){Topic.HILLS->"കുത്തനെയുള്ള കയറ്റിറക്കങ്ങൾ";Topic.BENDS->"പറയത്തക്ക വളവുകൾ";else->"തിരിവുകൾ"}
                val lack=when {
                    !o.routed->"ലക്ഷ്യസ്ഥാനം നൽകാത്തതിനാൽ റൂട്ട് അറിയില്ല."
                    o.count(wanted)==null->"ഈ റൂട്ടിലെ $what അറിയാൻ കഴിഞ്ഞില്ല."
                    else->"ഈ റൂട്ടിൽ $what ഇല്ല."
                }
                "$lack അതുകൊണ്ട് ${focus(c.topic)}"
            }
            else->focus(c.topic)
        }
        private fun reason(bad:Outcome)=when(bad) {
            Outcome.CLIMB_SLOWED->"കഴിഞ്ഞ തവണ കയറ്റങ്ങളിൽ വേഗം വല്ലാതെ കുറഞ്ഞുപോയി."
            Outcome.DESCENT_BUILT->"കഴിഞ്ഞ തവണ ഇറക്കങ്ങളിൽ വേഗം കൂടിപ്പോയി."
            Outcome.BEND_IN->"കഴിഞ്ഞ തവണ പല വളവുകളിലും വളവിനുള്ളിൽ വെച്ചാണ് വേഗം കുറച്ചത്."
            Outcome.BEND_FAST->"കഴിഞ്ഞ തവണ പല വളവുകളിലും വേഗം കൂടുതലായിരുന്നു."
            Outcome.TURN_LATE->"കഴിഞ്ഞ തവണ പല തിരിവുകളിലും വേഗം കുറച്ചത് വൈകിയാണ്."
            Outcome.TURN_FAST->"കഴിഞ്ഞ തവണ പല തിരിവുകളിലും വേഗം കൂടുതലായിരുന്നു."
            else->"കഴിഞ്ഞ തവണ പലപ്പോഴും ഞാൻ പറഞ്ഞതിനു ശേഷമാണ് വേഗം കുറച്ചത്."
        }
        private fun counted(t:Topic,o:Offer):String? {
            val where=o.partKm(t)?.let {"റൂട്ടിന്റെ ആദ്യത്തെ ${if(it==1)"ഒരു" else n(it)} കിലോമീറ്ററിൽ"}?:"നിങ്ങളുടെ റൂട്ടിൽ"
            return when(t) {
                Topic.HILLS->{
                    val c=o.climbs?:0;val d=o.descents?:0
                    when {
                        c+d==0->null
                        c>0&&d>0->"$where ${if(c==1)"ഒരു കയറ്റവും" else "${n(c)} കയറ്റങ്ങളും"} ${if(d==1)"ഒരു ഇറക്കവും" else "${n(d)} ഇറക്കങ്ങളും"} ഉണ്ട്."
                        c>0->"$where ${if(c==1)"ഒരു കയറ്റം" else "${n(c)} കയറ്റങ്ങൾ"} ഉണ്ട്."
                        else->"$where ${if(d==1)"ഒരു ഇറക്കം" else "${n(d)} ഇറക്കങ്ങൾ"} ഉണ്ട്."
                    }
                }
                Topic.BENDS->o.bends?.takeIf {it>0}?.let {"$where ${if(it==1)"ഒരു വളവ്" else "${n(it)} വളവുകൾ"} ഉണ്ട്."}
                Topic.TURNS->o.turns?.takeIf {it>0}?.let {"നിങ്ങളുടെ റൂട്ടിൽ ${if(it==1)"ഒരു തിരിവ്" else "${n(it)} തിരിവുകൾ"} ഉണ്ട്."}
                Topic.ROAD_USERS->null
            }
        }
        private fun technique(t:Topic,o:Offer):String=when(t) {
            Topic.HILLS->{
                val climb="കയറ്റത്തിന് മുമ്പ്, റോഡ് നിരപ്പായിരിക്കുമ്പോൾ തന്നെ ഗിയർ കുറയ്ക്കുക, എന്നിട്ട് ഒരേ വേഗത്തിൽ കയറുക."
                val descent="ഇറക്കത്തിന്റെ തുടക്കം മുതലേ വേഗം കൂടാതെ പിടിച്ചുനിർത്തുക."
                when {(o.climbs?:0)>0&&(o.descents?:0)==0->climb;(o.descents?:0)>0&&(o.climbs?:0)==0->descent;else->"$climb $descent"}
            }
            Topic.BENDS->"ഓരോ വളവിനും മുമ്പ്, റോഡ് നേരെയായിരിക്കുമ്പോൾ തന്നെ വേഗം കുറയ്ക്കുക, വളവിലൂടെ മുന്നോട്ട് നോക്കുക. എന്നിട്ട് വളവിൽ ഒരേ വേഗം പിടിക്കുക."
            Topic.TURNS->"ഓരോ തിരിവിനും നേരത്തെ തന്നെ വേഗം കുറയ്ക്കുക, അവസാന നിമിഷമല്ല. തിരിയുന്ന റോഡിൽ ആരെങ്കിലും മുറിച്ചുകടക്കുന്നുണ്ടോ എന്ന് നോക്കുക."
            Topic.ROAD_USERS->"ദൂരേക്ക് നോക്കി ഓടിക്കുക. ആർക്കെങ്കിലും ഇടം വേണ്ടിവരുമെന്ന് കണ്ടാലുടൻ, ഞാൻ പറയുന്നതിന് മുമ്പേ, ആക്സിലറേറ്റർ കുറയ്ക്കുക."
        }
    }

    // ------------------------------------------------------------------ measuring

    /** One hill, bend or turn after the drive: what was measured (outcome null: not measured). [at]:
     * where it starts along the route line (m), or Google's step number for a turn; [speeds] (km/h):
     * a hill's at its start, slowest and fastest; a bend's 50 m before, at its start and slowest in it;
     * a turn's 50 m and 15 m before and slowest at it. Not shown: for checks. */
    class Item(val outcome:Outcome?,val text:String,val at:Double=Double.NaN,val speeds:List<Double> = emptyList())

    class Result(val topic:Topic,val offered:Int?,val items:List<Item>,counts:Map<Outcome,Int>?=null) {
        val tallies:Map<Outcome,Int> = counts?:items.mapNotNull {it.outcome}.groupingBy {it}.eachCount()
        val measured:Int get()=tallies.values.sum()
        val good:Int get()=tallies.entries.sumOf {if(it.key.good)it.value else 0}
        val unmeasured:Int get()=items.count {it.outcome==null}
        fun past(at:Long)=Past(topic,tallies,at)
    }

    // Bends: the speed 50 m before one, at its start and through its first 35 m.
    private const val APPROACH_M=50.0
    private const val BEND_ZONE_M=35.0
    // How far in the first position on a hill or bend may be for its entry speed to count, and how
    // much of it must have a measured speed and position.
    private const val ENTRY_SLOPE_M=30.0
    private const val ENTRY_BEND_M=10.0
    private const val COVERED=.6
    // A stop: 2 km/h or less for 2 s.
    private const val STOP_KMH=2.0
    private const val STOP_S=2.0
    // What counts as a clear change of speed (km/h): GPS speed and positions on the line are rough.
    private const val CLIMB_LOSS=10.0
    private const val DESCENT_GAIN=10.0
    private const val BEND_DROP=7.0
    private const val BEND_OVER=7.0
    private const val TURN_SLACK=2.0

    /**
     * One drive's measurements for its focus. Give it the route now and then ([route]: its climbs,
     * descents and bends, read again after a reroute), where the car is along it and its speed every
     * tick ([sample]), and Google's current step whenever it changes ([turn]). [finish] settles it.
     */
    class Tracker(val topic:Topic,val offered:Int?) {
        private enum class Kind {CLIMB,DESCENT,BEND}
        private class Watch(val kind:Kind,var start:Double,var end:Double,var grade:Double,
            val right:Boolean=false,val sharp:Boolean=false,val comfortable:Double=0.0) {
            /** A bend at one of Google's turns: that turn, not a bend. */
            var excluded=false
            var approach=Double.NaN;var entry=Double.NaN;var firstIn=Double.NaN
            var low=Double.POSITIVE_INFINITY;var high=Double.NEGATIVE_INFINITY
            var covered=0.0;var stopSince=Double.NaN;var stopped=false
            val zoneEnd get()=if(kind==Kind.BEND)start+BEND_ZONE_M else end
            val lookFrom get()=if(kind==Kind.BEND)start-APPROACH_M else start
        }
        private class TurnWatch(val kind:Turn,val step:Int) {
            var at50=Double.NaN;var at15=Double.NaN;var low=Double.POSITIVE_INFINITY;var reached=false
        }
        private var generation=0L
        private val watches=ArrayList<Watch>()
        // Hills and bends of this route line already reviewed: read again each second (a hill's ends
        // refined as more heights arrive), they are never watched twice.
        private val settled=ArrayList<Watch>()
        private fun same(w:Watch,kind:Kind,start:Double,end:Double)=w.kind==kind&&
            if(kind==Kind.BEND)abs(w.start-start)<1 else minOf(w.end,end)-maxOf(w.start,start)>=.5*minOf(w.end-w.start,end-start)
        private val items=ArrayList<Item>()
        private var lastP=Double.NaN
        private var step:Int?=null
        private var turnWatch:TurnWatch?=null
        private var stepDistance=Double.NaN
        private var stepAt=Double.NaN
        private var finished:Result?=null

        /** The route with its lists. A new line (a reroute) settles what was under way and reads what is ahead again. */
        fun route(route:Route) {
            if(!route.listed||(topic!=Topic.HILLS&&topic!=Topic.BENDS)||finished!=null)return
            if(route.generation!=generation) {
                settleStarted();watches.clear();settled.clear();generation=route.generation;lastP=Double.NaN
            }
            // Where the car is: only what is still ahead of it is watched (unknown: nothing changes).
            val here=if(route.matched)route.progress else lastP
            fun ahead(at:Double)=!here.isNaN()&&at>here
            if(topic==Topic.HILLS&&route.heightsKnown)for(s in route.slopes) {
                val kind=if(s.climb)Kind.CLIMB else Kind.DESCENT
                if(settled.any {same(it,kind,s.start,s.end)})continue
                // The same slope, refined as more of the route's heights arrive.
                val known=watches.firstOrNull {same(it,kind,s.start,s.end)}
                if(known!=null){if(known.firstIn.isNaN()&&ahead(s.start)){known.start=s.start;known.end=s.end;known.grade=s.grade}}
                else if(ahead(s.start))watches+=Watch(kind,s.start,s.end,s.grade)
            }
            if(topic==Topic.BENDS&&route.turnsKnown)for(b in route.bends) {
                if(b.start>route.turnsTo||settled.any {same(it,Kind.BEND,b.start,b.start)})continue
                val known=watches.firstOrNull {same(it,Kind.BEND,b.start,b.start)}
                // Whether it is a turn is settled once the car is near it.
                if(known!=null){if(ahead(known.lookFrom))known.excluded=b.atTurn}
                else if(ahead(b.start-APPROACH_M))watches+=Watch(Kind.BEND,b.start,b.start+BEND_ZONE_M,0.0,b.angle>0,b.sharp,b.comfortableKmh).also {it.excluded=b.atTurn}
            }
            watches.sortBy {it.start}
        }

        /** Google's current step: its number, the turn it is (null: not one this focus measures), its
         * distance (m) when received at [receivedAt]. A new step settles the turn before it. */
        fun turn(step:Int?,kind:Turn?,distance:Double?,receivedAt:Double) {
            if(topic!=Topic.TURNS||finished!=null||step==null)return
            if(step!=this.step){settleTurn();this.step=step;turnWatch=kind?.let {TurnWatch(it,step)}}
            stepDistance=distance?.takeIf {it.isFinite()&&it>=0}?:Double.NaN;stepAt=receivedAt
        }

        /** Every tick: [here] the route's header now (where the car is), and the measured speed (km/h). */
        fun sample(time:Double,here:Route?,speedValid:Boolean,speedKmh:Double) {
            if(finished!=null||!time.isFinite())return
            val v=speedKmh.takeIf {speedValid&&it.isFinite()&&it>=0&&it<250}
            if(topic==Topic.TURNS){if(v!=null)sampleTurn(time,v);return}
            if(topic!=Topic.HILLS&&topic!=Topic.BENDS)return
            val p=here?.takeIf {it.generation==generation&&it.matched}?.progress
            if(p==null||v==null){lastP=Double.NaN;return}
            val previous=lastP;lastP=p
            for(w in watches) {
                if(w.excluded)continue
                if(w.kind==Kind.BEND&&w.approach.isNaN()&&p>=w.start-APPROACH_M&&p<w.start)w.approach=v
                if(p<w.start||p>w.zoneEnd)continue
                if(w.firstIn.isNaN()){w.firstIn=p;w.entry=v}
                w.low=minOf(w.low,v);w.high=maxOf(w.high,v)
                if(!previous.isNaN()&&p>previous&&p-previous<40)w.covered+=p-maxOf(previous,w.start)
                if(v<=STOP_KMH){if(w.stopSince.isNaN())w.stopSince=time else if(time-w.stopSince>=STOP_S)w.stopped=true}
                else w.stopSince=Double.NaN
            }
            // Passed, in route order.
            val passed=watches.filter {p>it.zoneEnd+5}
            passed.forEach(::settle);watches.removeAll(passed)
        }

        private fun sampleTurn(time:Double,v:Double) {
            val w=turnWatch?:return
            if(stepDistance.isNaN()||!stepAt.isFinite())return
            // Google's distance arrives about once a second: carried on with the measured speed.
            val d=stepDistance-v/3.6*(time-stepAt).coerceIn(0.0,3.0)
            if(d>60)return
            if(w.at50.isNaN()&&d<=APPROACH_M)w.at50=v
            if(w.at15.isNaN()&&d<=15&&d>=5)w.at15=v
            if(d<=15){w.low=minOf(w.low,v);w.reached=true}
        }

        /** Settles the drive: what is under way, and (road users) this drive's "You first" outcomes
         * (before, after, none: ld_learner_summary). */
        fun finish(roadUsers:IntArray?=null):Result {
            finished?.let {return it}
            settleStarted();watches.clear();settleTurn()
            val result=if(topic==Topic.ROAD_USERS) {
                val s=roadUsers?.takeIf {it.size>=3}
                val counts=if(s==null)emptyMap() else mapOf(Outcome.USER_BEFORE to s[0].coerceAtLeast(0),
                    Outcome.USER_AFTER to s[1].coerceAtLeast(0),Outcome.USER_NONE to s[2].coerceAtLeast(0)).filterValues {it>0}
                Result(topic,offered,emptyList(),counts)
            } else Result(topic,offered,items.toList())
            finished=result;return result
        }

        /** Hills and bends the car was already on: settled as they stand (most are then not measured). */
        private fun settleStarted() {watches.filter {!it.firstIn.isNaN()}.forEach(::settle)}

        private fun settle(w:Watch) {
            settled+=w
            if(w.excluded)return
            val number=items.size+1
            val zone=w.zoneEnd-w.start
            val slack=if(w.kind==Kind.BEND)ENTRY_BEND_M else ENTRY_SLOPE_M
            val name=when(w.kind) {
                Kind.CLIMB->"Climb, about ${pct(w.grade)} %"
                Kind.DESCENT->"Descent, about ${pct(w.grade)} %"
                Kind.BEND->if(w.sharp)"Sharp ${if(w.right)"right" else "left"} bend" else "${if(w.right)"Right" else "Left"} bend"
            }
            if(w.firstIn.isNaN()||w.firstIn-w.start>slack||w.covered<COVERED*zone) {
                items+=Item(null,"$number. $name: not measured (no speed or position for part of it).",w.start);return
            }
            val entry=w.entry.roundToInt();val low=w.low.roundToInt();val high=w.high.roundToInt()
            items+=when(w.kind) {
                Kind.CLIMB->{
                    val o=when {w.stopped->Outcome.CLIMB_STOPPED;w.entry-w.low>CLIMB_LOSS->Outcome.CLIMB_SLOWED;else->Outcome.CLIMB_STEADY}
                    Item(o,"$number. $name: $entry km/h at its start, $low km/h at the slowest. "+when(o) {
                        Outcome.CLIMB_STOPPED->"Stopped on it, then moved off again: a hill start."
                        Outcome.CLIMB_SLOWED->"The speed fell away by ${entry-low} km/h on the way up."
                        else->"A steady speed up it."},w.start,listOf(w.entry,w.low,w.high))
                }
                Kind.DESCENT->{
                    val o=if(w.high-w.entry>DESCENT_GAIN)Outcome.DESCENT_BUILT else Outcome.DESCENT_HELD
                    Item(o,"$number. $name: $entry km/h at the top, $high km/h at the most. "+
                        if(o==Outcome.DESCENT_BUILT)"The speed built up by ${high-entry} km/h." else "The speed was held.",
                        w.start,listOf(w.entry,w.low,w.high))
                }
                Kind.BEND->{
                    val o=when {w.entry-w.low>=BEND_DROP->Outcome.BEND_IN;w.entry>w.comfortable+BEND_OVER->Outcome.BEND_FAST;else->Outcome.BEND_BEFORE}
                    val before=if(w.approach.isNaN())"" else "${w.approach.roundToInt()} km/h 50 m before it, "
                    Item(o,"$number. $name: $before$entry km/h at its start, $low km/h at the slowest in it. "+when(o) {
                        Outcome.BEND_IN->"Slowed in the bend."
                        Outcome.BEND_FAST->"Faster than the ${w.comfortable.roundToInt()} km/h it is comfortable at."
                        else->"At a comfortable speed from its start."},w.start,listOf(w.approach,w.entry,w.low))
                }
            }
        }

        private fun settleTurn() {
            val w=turnWatch?:return;turnWatch=null
            // A turn not driven up to (rerouted away, or the drive ended first) is not reviewed.
            if(!w.reached)return
            val number=items.size+1
            if(w.at15.isNaN()){items+=Item(null,"$number. ${w.kind.label}: not measured (no speed or distance close to it).",w.step.toDouble());return}
            val o=when {
                w.at15<=w.kind.approachKmh+TURN_SLACK->Outcome.TURN_EARLY
                w.low<=w.kind.turnKmh+TURN_SLACK->Outcome.TURN_LATE
                else->Outcome.TURN_FAST
            }
            val before=if(w.at50.isNaN())"" else "${w.at50.roundToInt()} km/h 50 m before it, "
            items+=Item(o,"$number. ${w.kind.label}: $before${w.at15.roundToInt()} km/h 15 m before it. "+when(o) {
                Outcome.TURN_EARLY->"Slowed in good time."
                Outcome.TURN_LATE->"Slowed late, close to the turn."
                else->"Still ${w.low.roundToInt()} km/h at the turn."},w.step.toDouble(),listOf(w.at50,w.at15,w.low))
        }
        private fun pct(grade:Double)=abs(grade).roundToInt()
    }

    /** One drive's focus measured: a tracker for its skill, or one for every skill on an "All" drive. */
    class Session(val choice:Choice,offer:Offer) {
        private val trackers=choice.topics.map {Tracker(it,offer.count(it))}
        fun route(route:Route)=trackers.forEach {it.route(route)}
        fun turn(step:Int?,kind:Turn?,distance:Double?,receivedAt:Double)=trackers.forEach {it.turn(step,kind,distance,receivedAt)}
        fun sample(time:Double,here:Route?,speedValid:Boolean,speedKmh:Double)=trackers.forEach {it.sample(time,here,speedValid,speedKmh)}
        fun finish(roadUsers:IntArray?=null)=Drive(choice.all,trackers.map {it.finish(roadUsers)})
    }

    /** What a drive's focus came to: one result, or every skill's on an "All" drive. */
    class Drive(val all:Boolean,val results:List<Result>) {
        val title:String get()=if(all)"everything" else results.first().topic.noun
        val measured:Int get()=results.sumOf {it.measured}
        val topics:List<Topic> get()=results.map {it.topic}
        /** As kept for the learner: the skill's result; on an "All" drive each skill that had something measured. */
        fun past(at:Long):List<Past> =if(all)results.filter {it.measured>0}.map {Past(it.topic,it.tallies,at,true)} else results.map {it.past(at)}
    }

    // ------------------------------------------------------------------ the review

    /** The review of a drive's focus: one skill as [review] below, or every skill on an "All" drive,
     * each under its name, then one thing for next time. [extra]: lines added at the end. */
    fun review(d:Drive,extra:String?=null):String {
        if(!d.all)return review(d.results.first(),extra)
        val out=StringBuilder()
        for(r in d.results)out.append(r.topic.noun.uppercase()).append('\n').append(section(r)).append("\n\n")
        val tallies=d.results.flatMap {it.tallies.entries}.groupBy({it.key},{it.value}).mapValues {it.value.sum()}
        out.append(tip(tallies,d.measured,Topic.ROAD_USERS,all=true))
        out.append("\n\n").append(CAVEAT_ROUTE)
        extra?.takeIf {it.isNotBlank()}?.let {out.append("\n\n").append(it)}
        return out.toString()
    }

    private const val CAVEAT_ROUTE="Measured from your speed and the route only, and the route's positions and heights are approximate. The phone cannot see your gears, pedals or where you looked, or why you slowed: an observation, not a driving grade."
    private const val CAVEAT_ROAD_USERS="Measured from your speed only. The app cannot see where you looked, why you slowed or what your feet did: an observation, not a driving grade."

    /** The session review for the drive's focus (English, on screen). [extra]: lines added at the end. */
    fun review(r:Result,extra:String?=null):String {
        val out=StringBuilder(section(r))
        out.append("\n\n").append(tip(r.tallies,r.measured,r.topic))
        out.append("\n\n").append(if(r.topic==Topic.ROAD_USERS)CAVEAT_ROAD_USERS else CAVEAT_ROUTE)
        extra?.takeIf {it.isNotBlank()}?.let {out.append("\n\n").append(it)}
        return out.toString()
    }

    /** What was driven and measured of one skill, item by item. */
    private fun section(r:Result):String {
        val out=StringBuilder()
        val noun=r.topic.noun
        when(r.topic) {
            Topic.ROAD_USERS->{
                val before=r.tallies[Outcome.USER_BEFORE]?:0;val after=r.tallies[Outcome.USER_AFTER]?:0;val none=r.tallies[Outcome.USER_NONE]?:0
                if(r.measured==0)out.append("No road-user situations were measured on this drive.")
                else out.append("You slowed before being told: $before of ${r.measured}\nYou slowed after being told: $after\nNo slowing seen: $none")
            }
            else->{
                val n=r.items.size;val measured=r.measured
                val one=when(r.topic){Topic.HILLS->"climb or descent";Topic.BENDS->"bend";else->"turn"}
                val many=if(r.topic==Topic.HILLS)"climbs and descents" else noun
                if(r.offered!=null&&r.offered>0)out.append("Your route had ${r.offered} ${if(r.offered==1)one else many} ahead at the start. ")
                out.append(when {
                    n==0->"None was measured on this drive: not driven, or without a measured speed and position on the route."
                    measured==n->"Measured: $measured."
                    else->"Driven: $n, measured: $measured."
                })
                if(n>0)out.append("\n\n").append(r.items.joinToString("\n") {it.text})
                if(measured>0)out.append("\n\n").append(summary(r))
            }
        }
        return out.toString()
    }

    private fun summary(r:Result):String {
        fun c(o:Outcome)=r.tallies[o]?:0
        return when(r.topic) {
            Topic.HILLS->listOfNotNull(
                (c(Outcome.CLIMB_STEADY)+c(Outcome.CLIMB_STOPPED)+c(Outcome.CLIMB_SLOWED)).takeIf {it>0}?.let {
                    "Climbs: a steady speed ${c(Outcome.CLIMB_STEADY)} of $it"+
                        (if(c(Outcome.CLIMB_STOPPED)>0)", stopped and moved off ${c(Outcome.CLIMB_STOPPED)}" else "")+
                        (if(c(Outcome.CLIMB_SLOWED)>0)", the speed fell away ${c(Outcome.CLIMB_SLOWED)}" else "")},
                (c(Outcome.DESCENT_HELD)+c(Outcome.DESCENT_BUILT)).takeIf {it>0}?.let {"Descents: the speed held ${c(Outcome.DESCENT_HELD)} of $it"}).joinToString(" · ")
            Topic.BENDS->"At a comfortable speed from the start: ${c(Outcome.BEND_BEFORE)} of ${r.measured} · slowed in the bend: ${c(Outcome.BEND_IN)} · faster than comfortable: ${c(Outcome.BEND_FAST)}"
            Topic.TURNS->"Slowed in good time: ${c(Outcome.TURN_EARLY)} of ${r.measured} · late: ${c(Outcome.TURN_LATE)} · still fast: ${c(Outcome.TURN_FAST)}"
            Topic.ROAD_USERS->""
        }
    }

    /** One thing for next time: about what went wrong most often ([tallies] of [topic], or of every
     * skill on an "All" drive). */
    private fun tip(tallies:Map<Outcome,Int>,measured:Int,topic:Topic,all:Boolean=false):String {
        if(measured==0)return when {
            all->"Next time: set a destination, so hills, bends and turns can be measured as well as road users."
            topic==Topic.ROAD_USERS->"Next time: watch far ahead, and ease off as soon as someone may need room."
            else->"Next time: drive the route with the destination set, so each one can be measured."
        }
        val bad=tallies.filter {!it.key.good&&it.value>0}.maxByOrNull {it.value}?.key
            ?:return "Every one measured went as the coach asks. Next time: the same, on busier roads or with less help."
        return "Next time: "+when(bad) {
            Outcome.CLIMB_SLOWED->"change down before a climb, while the road is still flat, so the car keeps its speed going up."
            Outcome.DESCENT_BUILT->"from the top of a descent, hold the car back with a lower gear and gentle braking, so the speed does not build."
            Outcome.BEND_IN->"finish slowing while the road is still straight, so the bend itself needs no braking. Slowing in a bend for traffic is right: the phone cannot tell why you slowed."
            Outcome.BEND_FAST->"slow a little more before the bend, to a speed you could stop at within what you can see."
            Outcome.TURN_LATE->"start easing off about 50 m before the turn, so you reach turning speed with time to look."
            Outcome.TURN_FAST->"slow to about 15 to 20 km/h before you turn, and look into the road you are turning into."
            else->"look far ahead and ease off as soon as someone may need room, before the coach has to say it."
        }
    }
}
