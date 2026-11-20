package ai.learndrive.mobile

import ai.learndrive.mobile.navigation.google.GoogleManeuverKind
import ai.learndrive.mobile.ui.CoachTone
import ai.learndrive.mobile.ui.DriveCards
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The drive screen's cards: what the coach said, split into the situation, the one thing to do, and the rest. */
class DriveCardsTest {
    // The words of the fragments these tests use (ld::fragmentText).
    private val words=mapOf(1 to "Downhill.",7 to "Right turn ahead.",8 to "Oncoming truck.",16 to "Person ahead.",18 to "Vehicle ahead.",
        22 to "Brake gently.",23 to "Brake now.",24 to "Be ready to stop.",26 to "Ease off the accelerator.",28 to "Keep left.",
        31 to "Change down to second.",33 to "Clutch down.",35 to "Check your mirrors.",37 to "Signal right.",38 to "Look both ways.",
        43 to "Sound the horn.",48 to "In 50 metres,",50 to "turn right.",60 to "Left turn here.",76 to "Sharp left bend ahead.")
    private fun card(origin:Int,vararg ids:Int)=DriveCards.fromFragments(ids,origin,3) {words.getValue(it)}

    @Test fun aTurnPreparationLeadsWithTheSignal() {
        val c=card(5,7,26,35,37)
        assertEquals("MANEUVER PREP",c.header);assertEquals(CoachTone.ROUTE,c.tone);assertEquals("SIGNAL RIGHT",c.title)
        assertEquals("Ease off gas • Check mirrors",c.subtitle);assertEquals("right",c.icon)
        assertEquals(listOf(true,false,false),c.chips.map {it.active})   // the first of the turn's three steps
    }

    @Test fun theTurnItselfLeadsWithLookingBothWays() {
        val c=card(5,60,33,38,43)
        assertEquals("TURN NOW",c.header);assertEquals("LOOK BOTH WAYS",c.title);assertEquals("Clutch down • Sound the horn",c.subtitle)
        assertEquals(listOf(false,false,true),c.chips.map {it.active});assertEquals("left",c.icon)
        val approach=card(5,48,50,26,22,31)
        assertEquals("BRAKE GENTLY",approach.title);assertEquals(listOf(false,true,false),approach.chips.map {it.active})
    }

    @Test fun aRoadUserIsTheHeaderAndTheMostUrgentActionTheTitle() {
        val c=card(7,16,22,24)
        assertEquals("PERSON AHEAD",c.header);assertEquals(CoachTone.ROAD_USER,c.tone);assertEquals("BRAKE GENTLY",c.title)
        assertEquals("Be ready to stop",c.subtitle);assertEquals("person",c.icon);assertEquals("Camera",c.chips.first().text)
        val urgent=card(7,18,23,24)
        assertEquals(CoachTone.URGENT,urgent.tone);assertEquals("BRAKE NOW",urgent.title);assertEquals("VEHICLE AHEAD",urgent.header)
        // A slope and a road user in one sentence: the road user heads it, the slope is in the rest.
        val both=card(7,1,8,28,26)
        assertEquals("ONCOMING TRUCK",both.header);assertEquals("KEEP LEFT",both.title);assertEquals("Downhill • Ease off gas",both.subtitle)
    }

    @Test fun aBendFromTheRouteNamesItsDirectionAndWhatTheSpeedNeeds() {
        val c=card(6,76,22,31,43)
        assertEquals("SHARP LEFT BEND AHEAD",c.header);assertEquals(CoachTone.ROAD,c.tone);assertEquals("BRAKE GENTLY",c.title)
        assertEquals("Change down to 2nd • Sound the horn",c.subtitle);assertEquals("bendLeft",c.icon);assertEquals("Route line",c.chips.first().text)
    }

    @Test fun aRecordedSentenceIsSplitIntoItsAction() {
        val gear=DriveCards.fromText("Change up to third gear",6,1)
        assertEquals("VEHICLE CONTROL",gear.header);assertEquals("CHANGE UP TO THIRD GEAR",gear.title);assertEquals("",gear.subtitle)
        val exit=DriveCards.fromText("Straighten the wheel and add gentle accelerator",6,1)
        assertEquals("AFTER THE TURN",exit.header);assertEquals("STRAIGHTEN THE WHEEL",exit.title);assertEquals("Add gentle accelerator",exit.subtitle)
        val prep=DriveCards.fromText("Right turn ahead. Check your mirrors and signal right",5,1)
        assertEquals("MANEUVER PREP",prep.header);assertEquals("SIGNAL RIGHT",prep.title);assertTrue(prep.chips.first().active)
    }

    @Test fun googlesNextStepWithItsRouteNumberAndTheStepAfter() {
        val t=DriveCards.googleTurn(GoogleManeuverKind.RIGHT_TURN,232.0,"Turn right onto Link SH 56","Link SH 56",null,0,
            GoogleManeuverKind.ARRIVAL,4500.0,null,"Odayanchal")
        assertEquals("230 m",t.distance);assertEquals("Turn right onto Link SH 56",t.instruction);assertEquals("SH 56",t.shield)
        assertEquals("right",t.icon);assertEquals("Continue 4.5 km to Odayanchal",t.then);assertEquals("straight",t.thenIcon)
        assertEquals("Google Maps",t.attribution)   // Google's terms: its content carries its name
        val next=DriveCards.googleTurn(GoogleManeuverKind.LEFT_TURN,80.0,null,"MG Road",null,0,GoogleManeuverKind.RIGHT_TURN,300.0,null,null)
        assertEquals("Turn left onto MG Road",next.instruction);assertEquals("Turn right after 300 m",next.then);assertNull(next.shield)
    }

    @Test fun routeNumbersAndDistances() {
        assertEquals("NH 66",DriveCards.shield("NH66"));assertEquals("SH 56",DriveCards.shield("Kasaragod SH-56 Rd"))
        assertNull(DriveCards.shield("Kanhangad - Panathoor Rd"));assertNull(DriveCards.shield(null))
        assertEquals("Now",DriveCards.distance(12.0));assertEquals("1.2 km",DriveCards.distance(1234.0));assertNull(DriveCards.distance(Double.NaN))
    }
}
