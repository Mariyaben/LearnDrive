package ai.learndrive.mobile

import ai.learndrive.mobile.DriveFocus.Topic
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test
import java.io.File
import kotlin.math.abs

/**
 * The focus measured on real Kerala routes: OpenStreetMap routes from the route simulator, driven by
 * its learner through the native core as the phone feeds it (route line, GPS fixes, Google-like
 * turns), recorded by tools/focus_route_probe.py into build/focus-fixtures (not in git; the test is
 * skipped without them). Each bend and turn must be measured where the simulated learner drove it, at the
 * speeds they drove: positions along the core's route line and the speeds in the review agree.
 *
 * Fixture lines: F ld_route_features (full, when it changes) · S time, the car's metres along the
 * route (-1 unplaced), speed · T time, Google step, maneuver code, metres to it · B a bend's start,
 * the learner's speed there and slowest in its first 35 m · U a turn's step, speed 15 m before it and
 * slowest from there to the step's end.
 */
class DriveFocusRouteReplayTest {
    private class Truth(val at:Double,val first:Double,val low:Double)

    @Test fun realRoutesAreMeasuredWhereTheLearnerDroveThem() {
        val files=File("build/focus-fixtures").listFiles {f->f.name.endsWith(".txt")}?.sortedBy {it.name}.orEmpty()
        Assume.assumeTrue("no route fixtures on this PC",files.isNotEmpty())
        for(file in files) {
            val lines=file.readLines()
            val truth={tag:String->lines.filter {it.startsWith("$tag ")}.map {it.split(' ').let {p->Truth(p[1].toDouble(),p[2].toDouble(),p[3].toDouble())}}}
            val bends=replay(lines,Topic.BENDS);val turns=replay(lines,Topic.TURNS)
            println("${file.nameWithoutExtension}: ${bends.items.size} bends ${bends.tallies}, ${turns.items.size} turns ${turns.tallies}")
            (bends.items+turns.items).forEach {println("    ${it.text}")}
            val bendTruth=truth("B");val turnTruth=truth("U")
            val measuredBends=bends.items.filter {it.outcome!=null}
            assertTrue("${file.name}: bends not measured",measuredBends.size>=.9*bends.items.size)
            for(item in measuredBends) {
                val t=bendTruth.minBy {abs(it.at-item.at)}
                assertTrue("bend at ${item.at} is not a route-line bend",abs(t.at-item.at)<1)
                assertEquals("${file.name}: speed at the start of the bend at ${item.at}",t.first,item.speeds[1],3.0)
                assertEquals("${file.name}: slowest in the bend at ${item.at}",t.low,item.speeds[2],3.0)
            }
            // Every turn the learner drove up to is reviewed; one straight after another may not be measured.
            assertEquals("${file.name}: turns reviewed",turnTruth.size,turns.items.size)
            val measuredTurns=turns.items.filter {it.outcome!=null}
            assertTrue("${file.name}: turns not measured",measuredTurns.size>=.8*turns.items.size)
            for(item in measuredTurns) {
                val t=turnTruth.first {it.at==item.at}
                assertEquals("${file.name}: speed 15 m before turn ${item.at}",t.first,item.speeds[1],3.0)
                assertEquals("${file.name}: slowest at turn ${item.at}",t.low,item.speeds[2],3.0)
            }
        }
    }

    private fun replay(lines:List<String>,topic:Topic):DriveFocus.Result {
        val tracker=DriveFocus.Tracker(topic,null)
        var route:DriveFocus.Route?=null
        for(line in lines) {
            val p=line.split(' ')
            when(p[0]) {
                "F"->DriveFocus.Route.parse(p.drop(1).map {it.toDouble()}.toDoubleArray(),true)?.let {route=it;tracker.route(it)}
                "S"->{
                    val progress=p[2].toDouble()
                    val here=route?.let {DriveFocus.Route(it.generation,it.length,progress>=0,progress,false,0.0,false,0.0,false,emptyList(),emptyList(),emptyList())}
                    tracker.sample(p[1].toDouble(),here,true,p[3].toDouble())
                }
                "T"->tracker.turn(p[2].toInt(),when(p[3].toInt()) {
                    1->DriveFocus.Turn.LEFT;2->DriveFocus.Turn.RIGHT;6->DriveFocus.Turn.U_TURN;7->DriveFocus.Turn.ROUNDABOUT;else->null
                },p[4].toDouble(),p[1].toDouble())
            }
        }
        return tracker.finish()
    }
}
