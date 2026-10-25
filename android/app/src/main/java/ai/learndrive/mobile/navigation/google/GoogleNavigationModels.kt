package ai.learndrive.mobile.navigation.google

import com.google.android.libraries.mapsplatform.turnbyturn.model.Maneuver

/** App-owned values. Google integer constants must never be passed as Valhalla types. */
enum class GoogleNavigationPhase(val nativeValue:Int) {
    UNKNOWN(0), ENROUTE(1), REROUTING(2), STOPPED(3), ARRIVED(4)
}

enum class GoogleManeuverKind(val nativeValue:Int) {
    UNKNOWN(0), LEFT_TURN(1), RIGHT_TURN(2), SLIGHT_LEFT(1), SLIGHT_RIGHT(2),
    SHARP_LEFT(1), SHARP_RIGHT(2), KEEP_LEFT(3), KEEP_RIGHT(3), FORK_LEFT(3), FORK_RIGHT(3),
    ON_RAMP(4), OFF_RAMP(4), MERGE(5), U_TURN(6), ROUNDABOUT(7), STRAIGHT(8), ARRIVAL(9)
}

/** In-memory provider estimates. No maneuver coordinate, gear, safe speed or survey accuracy is implied. */
data class GoogleNavigationUpdate(
    val sessionId:Long,
    val routeEpoch:Long,
    val sequence:Long,
    val receivedAtSeconds:Double,
    val state:GoogleNavigationPhase,
    val stepNumber:Int?=null,
    val maneuver:GoogleManeuverKind=GoogleManeuverKind.UNKNOWN,
    val distanceMeters:Double?=null,
    val timeSeconds:Double?=null,
    val roundaboutExit:Int?=null
)

data class GoogleNavigationState(
    val status:String="Choose Google to enable its search and map",
    val ready:Boolean=false,
    val routing:Boolean=false,
    val routeReady:Boolean=false,
    val guiding:Boolean=false,
    val destinationLabel:String="",
    val error:Boolean=false
)

/** Only explicit ordinary turns are normalized to left/right. Ramps and roundabouts stay distinct. */
internal fun normalizeGoogleManeuver(value:Int):GoogleManeuverKind=when(value) {
    Maneuver.TURN_LEFT->GoogleManeuverKind.LEFT_TURN
    Maneuver.TURN_RIGHT->GoogleManeuverKind.RIGHT_TURN
    Maneuver.TURN_SLIGHT_LEFT->GoogleManeuverKind.SLIGHT_LEFT
    Maneuver.TURN_SLIGHT_RIGHT->GoogleManeuverKind.SLIGHT_RIGHT
    Maneuver.TURN_SHARP_LEFT->GoogleManeuverKind.SHARP_LEFT
    Maneuver.TURN_SHARP_RIGHT->GoogleManeuverKind.SHARP_RIGHT
    Maneuver.TURN_KEEP_LEFT->GoogleManeuverKind.KEEP_LEFT
    Maneuver.TURN_KEEP_RIGHT->GoogleManeuverKind.KEEP_RIGHT
    Maneuver.FORK_LEFT->GoogleManeuverKind.FORK_LEFT
    Maneuver.FORK_RIGHT->GoogleManeuverKind.FORK_RIGHT
    Maneuver.MERGE_LEFT,Maneuver.MERGE_RIGHT,Maneuver.MERGE_UNSPECIFIED->GoogleManeuverKind.MERGE
    Maneuver.ON_RAMP_LEFT,Maneuver.ON_RAMP_RIGHT,Maneuver.ON_RAMP_SLIGHT_LEFT,Maneuver.ON_RAMP_SLIGHT_RIGHT,
    Maneuver.ON_RAMP_SHARP_LEFT,Maneuver.ON_RAMP_SHARP_RIGHT,Maneuver.ON_RAMP_KEEP_LEFT,Maneuver.ON_RAMP_KEEP_RIGHT,
    Maneuver.ON_RAMP_U_TURN_CLOCKWISE,Maneuver.ON_RAMP_U_TURN_COUNTERCLOCKWISE,Maneuver.ON_RAMP_UNSPECIFIED->GoogleManeuverKind.ON_RAMP
    Maneuver.OFF_RAMP_LEFT,Maneuver.OFF_RAMP_RIGHT,Maneuver.OFF_RAMP_SLIGHT_LEFT,Maneuver.OFF_RAMP_SLIGHT_RIGHT,
    Maneuver.OFF_RAMP_SHARP_LEFT,Maneuver.OFF_RAMP_SHARP_RIGHT,Maneuver.OFF_RAMP_KEEP_LEFT,Maneuver.OFF_RAMP_KEEP_RIGHT,
    Maneuver.OFF_RAMP_U_TURN_CLOCKWISE,Maneuver.OFF_RAMP_U_TURN_COUNTERCLOCKWISE,Maneuver.OFF_RAMP_UNSPECIFIED->GoogleManeuverKind.OFF_RAMP
    Maneuver.TURN_U_TURN_CLOCKWISE,Maneuver.TURN_U_TURN_COUNTERCLOCKWISE->GoogleManeuverKind.U_TURN
    Maneuver.ROUNDABOUT_CLOCKWISE,Maneuver.ROUNDABOUT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_EXIT_CLOCKWISE,Maneuver.ROUNDABOUT_EXIT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_LEFT_CLOCKWISE,Maneuver.ROUNDABOUT_LEFT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_RIGHT_CLOCKWISE,Maneuver.ROUNDABOUT_RIGHT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_SHARP_LEFT_CLOCKWISE,Maneuver.ROUNDABOUT_SHARP_LEFT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_SHARP_RIGHT_CLOCKWISE,Maneuver.ROUNDABOUT_SHARP_RIGHT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_SLIGHT_LEFT_CLOCKWISE,Maneuver.ROUNDABOUT_SLIGHT_LEFT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_SLIGHT_RIGHT_CLOCKWISE,Maneuver.ROUNDABOUT_SLIGHT_RIGHT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_STRAIGHT_CLOCKWISE,Maneuver.ROUNDABOUT_STRAIGHT_COUNTERCLOCKWISE,
    Maneuver.ROUNDABOUT_U_TURN_CLOCKWISE,Maneuver.ROUNDABOUT_U_TURN_COUNTERCLOCKWISE->GoogleManeuverKind.ROUNDABOUT
    Maneuver.DESTINATION,Maneuver.DESTINATION_LEFT,Maneuver.DESTINATION_RIGHT->GoogleManeuverKind.ARRIVAL
    Maneuver.STRAIGHT->GoogleManeuverKind.STRAIGHT
    else->GoogleManeuverKind.UNKNOWN
}
