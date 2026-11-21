package ai.learndrive.mobile

import ai.learndrive.mobile.DriveFocus.Outcome
import ai.learndrive.mobile.DriveFocus.Past
import ai.learndrive.mobile.DriveFocus.Topic
import ai.learndrive.mobile.DriveFocus.Why
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** One focus per drive: what the route offers, the choice, the brief, the measuring and the review. */
class DriveFocusTest {
    // ---------------------------------------------------------------- ld_route_features arrays

    private fun features(generation:Long=1,length:Double=3000.0,progress:Double?=0.0,heightsTo:Double?=3000.0,turnsTo:Double?=3000.0,
        slopes:List<List<Double>> = emptyList(),bends:List<List<Double>> = emptyList(),turns:List<Double> = emptyList()):DoubleArray {
        val v=arrayListOf(1.0,generation.toDouble(),length,if(progress!=null)1.0 else 0.0,progress?:-1.0,
            if(heightsTo!=null)1.0 else 0.0,0.0,heightsTo?:0.0,if(turnsTo!=null)1.0 else 0.0,turnsTo?:0.0,
            slopes.size.toDouble(),bends.size.toDouble(),turns.size.toDouble())
        slopes.forEach {v+=it};bends.forEach {v+=it};v+=turns
        return v.toDoubleArray()
    }
    private fun header(generation:Long=1,progress:Double?)=features(generation,progress=progress).copyOf(13)
    private fun slope(start:Double,end:Double,grade:Double)=listOf(start,end,grade,grade*1.2)
    private fun bend(start:Double,angle:Double,sharp:Boolean,comfortable:Double,atTurn:Boolean=false)=listOf(start,angle,if(sharp)1.0 else 0.0,comfortable,if(atTurn)1.0 else 0.0)

    @Test fun theRouteIsReadFromTheCore() {
        val v=features(progress=120.0,slopes=listOf(slope(500.0,800.0,9.0)),bends=listOf(bend(1200.0,-70.0,true,20.0)),turns=listOf(1800.0))
        val r=DriveFocus.Route.parse(v,true)!!
        assertTrue(r.listed&&r.matched);assertEquals(120.0,r.progress,0.0)
        assertEquals(1,r.slopes.size);assertTrue(r.slopes[0].climb)
        assertEquals(1,r.bends.size);assertTrue(r.bends[0].sharp);assertEquals(listOf(1800.0),r.turns)
        // Each tick only the header is asked for: where the car is, no lists.
        val h=DriveFocus.Route.parse(header(progress=130.0),false)!!
        assertFalse(h.listed);assertEquals(130.0,h.progress,0.0);assertTrue(h.slopes.isEmpty())
        // No line, a short array, another layout, or counts that do not match: nothing.
        assertNull(DriveFocus.Route.parse(features(generation=0),true))
        assertNull(DriveFocus.Route.parse(DoubleArray(5),true))
        assertNull(DriveFocus.Route.parse(v.copyOf().also {it[0]=2.0},true))
        assertFalse(DriveFocus.Route.parse(v.copyOf(v.size-1),true)!!.listed)
    }

    @Test fun theOfferCountsWhatIsAheadAndToldApartFromTurns() {
        val route=DriveFocus.Route.parse(features(progress=600.0,heightsTo=2500.0,turnsTo=2600.0,
            slopes=listOf(slope(200.0,500.0,8.0),slope(1000.0,1300.0,-9.0),slope(2000.0,2300.0,7.0)),
            bends=listOf(bend(400.0,40.0,false,30.0),bend(900.0,60.0,true,20.0),bend(1500.0,-45.0,false,32.0,atTurn=true),
                bend(2200.0,50.0,false,28.0),bend(2900.0,70.0,true,18.0))),true)
        val offer=DriveFocus.offer(route,turns=4,routed=true)
        // Behind the car: not counted. At a turn: that turn. Beyond Google's known turns: not yet known.
        assertEquals(1,offer.climbs);assertEquals(1,offer.descents);assertEquals(2,offer.hills)
        assertEquals(2,offer.bends);assertEquals(4,offer.turns)
        // Heights and turns known only for the first part of a 3 km route.
        assertEquals(2,offer.partKm(Topic.HILLS));assertEquals(2,offer.partKm(Topic.BENDS))
        // Without Google's turns a bend cannot be told from a turn; without heights there are no hills.
        val unknown=DriveFocus.offer(DriveFocus.Route.parse(features(heightsTo=null,turnsTo=null,bends=listOf(bend(900.0,60.0,true,20.0))),true),null,true)
        assertNull(unknown.bends);assertNull(unknown.hills);assertNull(unknown.turns)
        assertEquals(DriveFocus.Offer(false),DriveFocus.offer(null,null,false))
    }

    // ---------------------------------------------------------------- choosing

    private val hilly=DriveFocus.Offer(true,climbs=2,descents=1,bends=7,turns=5)
    private fun past(topic:Topic,vararg outcomes:Pair<Outcome,Int>)=Past(topic,mapOf(*outcomes),0)

    @Test fun theFirstFocusIsWhatTheRouteHasMostOf() {
        val c=DriveFocus.choose(hilly,emptyList(),null)
        assertEquals(Topic.BENDS,c.topic);assertEquals(Why.FIRST,c.why)
        // Then a skill not yet practised, the one the route has most of.
        val next=DriveFocus.choose(hilly,listOf(past(Topic.BENDS,Outcome.BEND_BEFORE to 4,Outcome.BEND_IN to 1)),null)
        assertEquals(Topic.TURNS,next.topic);assertEquals(Why.NEXT,next.why)
    }

    @Test fun moreWrongThanRightLastTimeMeansTheSameAgain() {
        val last=past(Topic.BENDS,Outcome.BEND_BEFORE to 2,Outcome.BEND_IN to 3)
        val c=DriveFocus.choose(hilly,listOf(last),null)
        assertEquals(Topic.BENDS,c.topic);assertEquals(Why.AGAIN,c.why);assertEquals(last,c.last)
        assertTrue(DriveFocus.brief(c,hilly,"en").startsWith("Bends again today. Last time, you slowed inside three of five bends."))
        // Not on this route: something else, not the same again.
        val flat=DriveFocus.Offer(true,climbs=0,descents=0,bends=1,turns=6)
        assertEquals(Topic.TURNS,DriveFocus.choose(flat,listOf(last),null).topic)
    }

    @Test fun onceEverySkillHasBeenPractisedTheWeakestComesNextNeverTheSameTwice() {
        val history=listOf(past(Topic.HILLS,Outcome.CLIMB_STEADY to 1,Outcome.DESCENT_BUILT to 1),
            past(Topic.TURNS,Outcome.TURN_EARLY to 5),past(Topic.ROAD_USERS,Outcome.USER_BEFORE to 3,Outcome.USER_AFTER to 1),
            past(Topic.BENDS,Outcome.BEND_BEFORE to 3,Outcome.BEND_FAST to 1))
        // Hills went half right: weakest, and not the last drive's focus.
        assertEquals(Topic.HILLS,DriveFocus.choose(hilly,history,null).topic)
        // The weakest was the last drive's (and not more wrong than right): another; of two that went
        // equally well, the one practised longer ago.
        val c=DriveFocus.choose(hilly,history+past(Topic.HILLS,Outcome.CLIMB_STEADY to 1,Outcome.DESCENT_BUILT to 1),null)
        assertEquals(Topic.ROAD_USERS,c.topic)
    }

    @Test fun theLearnersChoiceWhenTheRouteHasIt() {
        assertEquals(DriveFocus.Choice(Topic.HILLS,Why.CHOSEN),DriveFocus.choose(hilly,emptyList(),Topic.HILLS))
        // One bend is enough when chosen, though the coach would not choose it.
        val oneBend=DriveFocus.Offer(true,climbs=0,descents=0,bends=1,turns=0)
        assertEquals(Topic.BENDS,DriveFocus.choose(oneBend,emptyList(),Topic.BENDS).topic)
        assertEquals(Topic.ROAD_USERS,DriveFocus.choose(oneBend,emptyList(),null).topic)
        // Not on the route: the coach chooses, and says why.
        val c=DriveFocus.choose(oneBend,emptyList(),Topic.HILLS)
        assertEquals(Why.NOT_ON_ROUTE,c.why);assertEquals(Topic.HILLS,c.wanted);assertEquals(Topic.ROAD_USERS,c.topic)
        assertTrue(DriveFocus.brief(c,oneBend,"en").startsWith("This route has no steep climbs or descents, so today's focus is road users."))
        val free=DriveFocus.choose(DriveFocus.Offer(false),emptyList(),Topic.BENDS)
        assertTrue(DriveFocus.brief(free,DriveFocus.Offer(false),"en").startsWith("Without a destination there is no route to read bends from, so today's focus is road users."))
    }

    // ---------------------------------------------------------------- the brief

    @Test fun theBriefNamesTheFocusCountsItAndSaysHowToDriveIt() {
        val hills=DriveFocus.brief(DriveFocus.Choice(Topic.HILLS,Why.FIRST),hilly,"en")
        assertEquals("Today's focus is hills. Your route has two climbs and a descent. Before a climb, change down while the road is still flat, then keep a steady speed up it. From the top of a descent, hold your speed back so that it doesn't build. After the drive, we'll go through each one.",hills)
        val part=DriveFocus.Offer(true,climbs=0,descents=3,hillsKm=10)
        assertEquals("Today's focus is hills. The first ten kilometres of your route have three descents. From the top of a descent, hold your speed back so that it doesn't build. After the drive, we'll go through each one.",
            DriveFocus.brief(DriveFocus.Choice(Topic.HILLS,Why.CHOSEN),part,"en"))
        assertTrue(DriveFocus.brief(DriveFocus.Choice(Topic.BENDS,Why.NEXT),hilly,"en").contains("Your route has seven bends."))
        assertTrue(DriveFocus.brief(DriveFocus.Choice(Topic.TURNS,Why.NEXT),hilly,"en").contains("Your route has five turns."))
        val ml=DriveFocus.brief(DriveFocus.Choice(Topic.HILLS,Why.FIRST),hilly,"ml")
        assertTrue(ml,ml.startsWith("ഇന്നത്തെ ഡ്രൈവിൽ ശ്രദ്ധ കയറ്റങ്ങളിലും ഇറക്കങ്ങളിലുമാണ്. നിങ്ങളുടെ റൂട്ടിൽ രണ്ട് കയറ്റങ്ങളും ഒരു ഇറക്കവും ഉണ്ട്."))
    }

    @Test fun everyBriefIsAboutTwentySecondsAndMalayalamIsMalayalam() {
        val offers=listOf(hilly,DriveFocus.Offer(true,climbs=1,descents=0,hillsKm=4,bends=2,bendsKm=4,turns=1),
            DriveFocus.Offer(true,climbs=17,descents=19,bends=20,turns=18),DriveFocus.Offer(false))
        val last=listOf(past(Topic.HILLS,Outcome.CLIMB_SLOWED to 3,Outcome.DESCENT_BUILT to 2),past(Topic.BENDS,Outcome.BEND_IN to 2),
            past(Topic.TURNS,Outcome.TURN_LATE to 2),past(Topic.ROAD_USERS,Outcome.USER_NONE to 2))
        for(offer in offers)for(topic in Topic.entries)for(why in Why.entries) {
            val choice=DriveFocus.Choice(topic,why,if(why==Why.NOT_ON_ROUTE)Topic.HILLS else null,if(why==Why.AGAIN)last.first {it.topic==topic} else null)
            val en=DriveFocus.brief(choice,offer,"en")
            val words=en.split(' ').size
            // About 2.6 words a second.
            assertTrue("$words words: $en",words in 18..62)
            assertFalse(en,en.contains("  ")||en.contains("null"))
            val ml=DriveFocus.brief(choice,offer,"ml")
            assertTrue(ml,ml.none {it in 'a'..'z'||it in 'A'..'Z'||it.isDigit()})
            assertTrue(ml,ml.length in 60..520)
        }
    }

    // ---------------------------------------------------------------- measuring

    /** Drives [tracker] along the route from [from] to [to] m: [speed] at each place (km/h), one tick
     * every 50 ms, and each second the whole route ([route] with the car at that place, or not placed).
     * [stopAt]: where the car waits [stopFor] seconds; [gap]: where it has no position on the route. */
    private fun drive(tracker:DriveFocus.Tracker,route:(Double?)->DoubleArray,from:Double,to:Double,speed:(Double)->Double,
        stopAt:Double=Double.NaN,stopFor:Double=0.0,gap:ClosedRange<Double>?=null,generation:Long=1):Double {
        var s=from;var t=0.0;var nextRoute=0.0;var stopped=0.0;var waited=false
        while(s<to) {
            val lost=gap!=null&&s in gap
            if(t>=nextRoute){tracker.route(DriveFocus.Route.parse(route(if(lost)null else s),true)!!);nextRoute+=1}
            var v=speed(s)
            if(!stopAt.isNaN()&&s>=stopAt&&!waited){v=0.0;stopped+=.05;if(stopped>=stopFor)waited=true}
            tracker.sample(t,DriveFocus.Route.parse(header(generation,if(lost)null else s),false),true,v)
            s+=v/3.6*.05;t+=.05
            if(v==0.0&&waited)s+=.01
        }
        return t
    }
    private fun ramp(s:Double,a:Double,b:Double,va:Double,vb:Double)=va+(vb-va)*((s-a)/(b-a)).coerceIn(0.0,1.0)

    @Test fun hillsSteadyFallingAwayHeldBuildingAndAHillStart() {
        val slopes=listOf(slope(500.0,800.0,9.0),slope(1200.0,1500.0,-8.0),slope(2000.0,2300.0,10.0),slope(2500.0,2800.0,-7.0))
        val tracker=DriveFocus.Tracker(Topic.HILLS,4)
        drive(tracker,{s->features(progress=s,slopes=slopes)},0.0,2950.0,{s->when {
            s<500->30.0
            s<800->30.0                         // a steady climb
            s<1200->30.0
            s<1500->ramp(s,1200.0,1450.0,25.0,42.0) // the speed builds on the way down
            s<2000->30.0
            s<2300->ramp(s,2000.0,2250.0,30.0,14.0) // and falls away on the next climb
            s<2500->25.0
            else->26.0}},stopAt=2150.0,stopFor=6.0)
        val r=tracker.finish()
        assertEquals(listOf(Outcome.CLIMB_STEADY,Outcome.DESCENT_BUILT,Outcome.CLIMB_STOPPED,Outcome.DESCENT_HELD),r.items.map {it.outcome})
        assertTrue(r.items[1].text,r.items[1].text.startsWith("2. Descent, about 8 %: 25 km/h at the top, 42 km/h at the most. The speed built up by 17 km/h."))
        assertTrue(r.items[2].text,r.items[2].text.contains("Stopped on it, then moved off again: a hill start."))
        assertEquals(3,r.good);assertEquals(4,r.measured)
        val review=DriveFocus.review(r)
        assertTrue(review,review.contains("Your route had 4 climbs and descents ahead at the start. Measured: 4."))
        assertTrue(review,review.contains("Next time: from the top of a descent, hold the car back"))
        assertTrue(review,review.contains("an observation, not a driving grade"))
        // Falling away without a stop.
        val second=DriveFocus.Tracker(Topic.HILLS,1)
        drive(second,{s->features(progress=s,slopes=listOf(slope(500.0,800.0,9.0)))},0.0,900.0,{s->if(s<500)32.0 else ramp(s,500.0,780.0,32.0,15.0)})
        assertEquals(listOf(Outcome.CLIMB_SLOWED),second.finish().items.map {it.outcome})
    }

    @Test fun bendsBeforeInAndFastTurnsAreNotBends() {
        val bends=listOf(bend(600.0,80.0,true,20.0),bend(1200.0,-50.0,false,35.0),
            bend(1800.0,90.0,true,18.0,atTurn=true),bend(2400.0,45.0,false,30.0))
        val tracker=DriveFocus.Tracker(Topic.BENDS,3)
        drive(tracker,{s->features(progress=s,bends=bends)},0.0,2600.0,{s->when {
            s<600->ramp(s,550.0,595.0,40.0,20.0)   // slowed on the straight before it
            s<700->20.0
            s<1200->38.0
            s<1240->ramp(s,1200.0,1235.0,38.0,25.0) // slowing inside the bend
            s<2400->45.0
            else->45.0}})                        // no slowing for a bend comfortable at 30
        val r=tracker.finish()
        assertEquals(listOf(Outcome.BEND_BEFORE,Outcome.BEND_IN,Outcome.BEND_FAST),r.items.map {it.outcome})
        assertTrue(r.items[0].text,r.items[0].text.startsWith("1. Sharp right bend: 40 km/h 50 m before it, 20 km/h at its start"))
        assertTrue(r.items[1].text,r.items[1].text.startsWith("2. Left bend:"))
        assertTrue(r.items[2].text,r.items[2].text.endsWith("Faster than the 30 km/h it is comfortable at."))
        val review=DriveFocus.review(r)
        assertTrue(review,review.contains("At a comfortable speed from the start: 1 of 3 · slowed in the bend: 1 · faster than comfortable: 1"))
    }

    @Test fun withoutAPositionThroughABendItIsNotMeasured() {
        val tracker=DriveFocus.Tracker(Topic.BENDS,2)
        drive(tracker,{s->features(progress=s,bends=listOf(bend(600.0,80.0,true,20.0),bend(1200.0,-50.0,false,35.0)))},0.0,1400.0,{20.0},gap=590.0..640.0)
        val r=tracker.finish()
        assertNull(r.items[0].outcome);assertTrue(r.items[0].text.contains("not measured"))
        assertEquals(Outcome.BEND_BEFORE,r.items[1].outcome)
        assertTrue(DriveFocus.review(r).contains("Driven: 2, measured: 1."))
    }

    @Test fun aRerouteSettlesWhatWasUnderWayAndReadsTheNewRoute() {
        val tracker=DriveFocus.Tracker(Topic.HILLS,2)
        drive(tracker,{s->features(progress=s,slopes=listOf(slope(500.0,800.0,9.0),slope(1200.0,1500.0,-8.0)))},0.0,600.0,{30.0})
        // A new line from where the car is: its own climb ahead; the old descent is gone.
        drive(tracker,{s->features(generation=2,length=1500.0,progress=s,slopes=listOf(slope(300.0,500.0,11.0)))},0.0,700.0,{30.0},generation=2)
        val r=tracker.finish()
        assertEquals(2,r.items.size)
        assertNull(r.items[0].outcome)            // the first climb, left half-way: not measured
        assertEquals(Outcome.CLIMB_STEADY,r.items[1].outcome)
    }

    /** Google's current step every second (distance as Google gives it), the speed every 50 ms. */
    private fun turns(tracker:DriveFocus.Tracker,steps:List<Pair<DriveFocus.Turn?,(Double)->Double>>,lastTo:Double=-20.0) {
        var t=0.0
        steps.forEachIndexed {i,(kind,speed)->
            var d=300.0;var nextUpdate=t
            val end=if(i==steps.size-1)lastTo else -20.0
            while(d>end) {
                if(t>=nextUpdate){tracker.turn(i+1,kind,d.coerceAtLeast(0.0),t);nextUpdate+=1}
                val v=speed(d);tracker.sample(t,null,true,v)
                d-=v/3.6*.05;t+=.05
            }
        }
        tracker.turn(steps.size+1,null,500.0,t)
    }

    @Test fun turnsInGoodTimeLateAndFastAndOnlyThoseDrivenUpTo() {
        val tracker=DriveFocus.Tracker(Topic.TURNS,4)
        turns(tracker,listOf(
            DriveFocus.Turn.LEFT to {d->ramp(d,45.0,25.0,40.0,15.0)},                 // eased off early
            DriveFocus.Turn.RIGHT to {d->if(d>12)35.0 else ramp(d,12.0,4.0,35.0,15.0)}, // braked at the last moment
            null to {_->40.0},                                                         // a step that is not a turn
            DriveFocus.Turn.SHARP_LEFT to {_->25.0},                                   // never slowed enough
            DriveFocus.Turn.ROUNDABOUT to {_->30.0}),lastTo=200.0)                     // the drive ended first
        val r=tracker.finish()
        assertEquals(listOf(Outcome.TURN_EARLY,Outcome.TURN_LATE,Outcome.TURN_FAST),r.items.map {it.outcome})
        assertTrue(r.items[0].text,r.items[0].text.startsWith("1. Left turn: 40 km/h 50 m before it, 15 km/h 15 m before it. Slowed in good time."))
        assertTrue(r.items[2].text,r.items[2].text.endsWith("Still 25 km/h at the turn."))
        assertTrue(DriveFocus.review(r).contains("Next time: "))
    }

    @Test fun roadUsersComeFromYouFirst() {
        val r=DriveFocus.Tracker(Topic.ROAD_USERS,null).finish(intArrayOf(4,2,1,0,0,0,0,7,0,0,0))
        assertEquals(7,r.measured);assertEquals(4,r.good)
        val text=DriveFocus.review(r,"Road-user situations on record: 7.")
        assertTrue(text,text.startsWith("You slowed before being told: 4 of 7\nYou slowed after being told: 2\nNo slowing seen: 1"))
        assertTrue(text,text.endsWith("Road-user situations on record: 7."))
        assertEquals(0,DriveFocus.Tracker(Topic.ROAD_USERS,null).finish(null).measured)
        // Settled once: a second finish is the same result.
        val tracker=DriveFocus.Tracker(Topic.BENDS,0);assertTrue(tracker.finish()===tracker.finish())
    }

    @Test fun whatIsKeptIsTheSkillAndItsOutcomesOnly() {
        val p=past(Topic.TURNS,Outcome.TURN_EARLY to 3,Outcome.TURN_LATE to 1).copy(at=1_700_000_000_000)
        assertEquals("turns|1700000000000|turn_early=3,turn_late=1",p.encode())
        assertEquals(p,Past.decode(p.encode()))
        assertEquals(Past(Topic.HILLS,emptyMap(),5),Past.decode("hills|5|"))
        // Another skill's outcome or an unknown one is dropped; a damaged line is nothing.
        assertEquals(mapOf(Outcome.BEND_IN to 2),Past.decode("bends|5|bend_in=2,turn_late=1,future=4")!!.outcomes)
        assertNull(Past.decode("bends|5"));assertNull(Past.decode("lanes|5|"));assertNull(Past.decode("bends|x|"))
        assertNull(Past.decode("bends|5|bend_in=-1"));assertNull(Past.decode("bends|5|bend_in"))
        assertNotNull(Past.decode(" turns|1| "))
    }
}
