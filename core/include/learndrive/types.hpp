#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ld {
constexpr double gravity = 9.80665;
constexpr double pi = 3.14159265358979323846;
constexpr double infinity = std::numeric_limits<double>::infinity();
// Supports slower inference while keeping capture timestamps and a hard age cap.
constexpr double maxVisionAgeSeconds = .35;
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(double s) const { return {x*s,y*s,z*s}; }
    double dot(Vec3 b) const { return x*b.x+y*b.y+z*b.z; }
    Vec3 cross(Vec3 b) const { return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x}; }
    double norm() const { return std::sqrt(dot(*this)); }
    Vec3 unit() const { double n=norm(); return n>1e-9 ? *this*(1/n) : Vec3{}; }
    bool finite() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
};
struct ImuSample {
    double time{}; // Seconds on one monotonic clock; never wall-clock timestamps.
    Vec3 acceleration; // Proper acceleration including +g, device axes, m/s^2.
    Vec3 gyro;         // Device axes, rad/s.
    Vec3 linear;       // Gravity removed, device axes, m/s^2.
    bool hasLinear{};
};
struct Telemetry {
    double time{}, speedKmh{}, pitchDegrees{}, longitudinalAcceleration{}, calibrationProgress{};
    bool calibrated{}, speedValid{}, pitchValid{}, braking{}, hardBraking{}, shock{};
};
enum class Prompt : int { none=0, hill=1, ease=2, brake=3, emergency=4, camera=5, close=6, roadUser=7, traffic=8,
    uphill=9, downhill=10, bendLeft=11, bendRight=12, downhillGear=13, shiftUp=14, shiftDown=15,
    closeQuiet=16, closeGearQuiet=17, roadUserQuiet=18, nearbyCollision=19,
    bendLeftQuiet=20, bendRightQuiet=21, bendLeftGear=22, bendRightGear=23, closeGear=24, imageClose=25, imageCloseGear=26,
    gentleUp=27, gentleDown=28, steepUp=29, steepDownGear=30, steepDown=31, verySteepUp=32, verySteepDown=33,
    slopeUnavailable=34, maintainSpeed=35, holdLaneDistance=36, keepSafeGap=37, slowSmoothly=38,
    watchPedestrians=39, prepareBend=40, observeSign=41, brakeHarder=42, emergencyStop=43,
    pedestrianSpace=44, riderSpace=45, vehicleSpace=46,
    mapLeftBend=47,mapRightBend=48,mapJunction=49,mapLeftTurn=50,mapRightTurn=51,
    mapLeftTurnNear=52,mapRightTurnNear=53,mapLeftTurn50=54,mapRightTurn50=55,
    junctionPlanLeft=56,junctionPlanRight=57,junctionPlan=58,junctionApproach=59,
    // Instructor-style control instructions. They tell the learner what to do
    // with the controls for the measured situation; they never claim to know the
    // current gear/pedal state and never grant permission to proceed.
    junctionBrakeGearDown=60,junctionGearDown=61,junctionEntry=62,turnExit=63,
    personEaseCover=64,riderEaseCover=65,vehicleEaseGap=66,brakeReadyStop=67,clutchHoldBrake=68,
    gearUpSecond=69,gearUpThird=70,gearUpFourth=71,gearUpFifth=72,gearDownSecond=73,gearDownThird=74,
    stopClutch=75,moveOffPrep=76,downhillThird=77,downhillSecond=78,uphillSecond=79,uphillThird=80,
    // Risk-reasoned road-user instructions (origin 7): what to do about the most
    // dangerous road user, from its tracked motion. Never an all-clear or "go".
    vehicleCrossingWait=81,personCrossingStop=82,oncomingKeepLeft=83,personLeftSpace=84,personRightSpace=85,crossedLookAgain=86,
    // Lane keeping on marked roads (lane lines seen consistently), origin 6.
    driftLeftCentre=87,driftRightKeepLeft=88,
    // Road knowledge ahead from the map (origin 6): speed bump, sharp (blind) bend.
    bumpAhead=89,bendHorn=90,
    // Google found a new route (a missed turn, or a better way): calm, no blame, no promise.
    routeChanged=91,
    // Hills from the elevation along the route (Google's Elevation API), named before the slope. The
    // spoken sentence is composed for the speed (which gear, whether to slow down first).
    mapDownhill=92,mapSteepDownhill=93,mapUphill=94,mapSteepUphill=95 };
constexpr int promptCount=95;
inline bool isRiskPrompt(Prompt p){const int i=int(p);return i>=81&&i<=86;}
// Instructor phrase fragments. The situation planner composes one utterance per
// moment from these (context clauses, then actions), e.g. "Uphill. Oncoming truck.
// Person on the left. Keep left. Ease off the accelerator. Give them space."
enum class Fragment : int { none=0,
    downhill=1,steepDownhill,uphill,bendAhead,junctionAhead,leftTurnAhead,rightTurnAhead,
    oncomingTruck,oncomingBus,oncomingVehicle,oncomingTwoWheeler,vehicleCrossing,personCrossing,
    personLeft,personRight,personAhead,twoWheelerAhead,vehicleAhead,truckAhead,driftingLeft,driftingRight,
    brakeGently=22,brakeNow,readyToStop,stopAndWait,easeOff,coverBrake,keepLeft,giveSpace,safeGap,
    downSecond,downThird,clutchDown,holdBrake,checkMirrors,signalLeft,signalRight,lookBothWays,
    steadyAccelerator,laneCentre,letThemPass,lookAgainMoveOff,
    // Indian road practice: sound the horn at blind bends, junctions and when passing.
    soundHorn=43,bumpAheadContext=44,
    // Navigation (Google route): the distance and the maneuver, as an instructor says them.
    in200m=45,in150m,in100m,in50m,turnLeft,turnRight,keepLeftRoute,keepRightRoute,uTurn,
    roundaboutFirst,roundaboutSecond,roundaboutThird,roundaboutFourth,roundaboutEnter,destinationAhead,
    leftTurnHere,rightTurnHere,thenLeft,thenRight,giveWayRight,pullOver,mergeCarefully=66,
    // Hills ahead (map elevation along the route), and how to drive them.
    downhillAhead=67,steepDownhillAhead,uphillAhead,steepUphillAhead,noNeutral,controlSpeedBrake,downOneGear=73 };
constexpr int fragmentCount=73;
inline bool isSlopeAheadFragment(Fragment f){return int(f)>=67&&int(f)<=70;}
inline bool isNavigationFragment(Fragment f){return int(f)>=45&&int(f)<=66;}
inline bool isContextFragment(Fragment f){return (int(f)>=1&&int(f)<=21)||f==Fragment::bumpAheadContext||isNavigationFragment(f);}
inline const char* fragmentText(Fragment f) {
    switch(f) {
    case Fragment::downhill:return "Downhill.";case Fragment::steepDownhill:return "Steep downhill.";case Fragment::uphill:return "Uphill.";
    case Fragment::bendAhead:return "Bend ahead.";case Fragment::junctionAhead:return "Junction ahead.";
    case Fragment::leftTurnAhead:return "Left turn ahead.";case Fragment::rightTurnAhead:return "Right turn ahead.";
    case Fragment::oncomingTruck:return "Oncoming truck.";case Fragment::oncomingBus:return "Oncoming bus.";
    case Fragment::oncomingVehicle:return "Oncoming vehicle.";case Fragment::oncomingTwoWheeler:return "Oncoming two-wheeler.";
    case Fragment::vehicleCrossing:return "Vehicle crossing.";case Fragment::personCrossing:return "Person crossing.";
    case Fragment::personLeft:return "Person on the left.";case Fragment::personRight:return "Person on the right.";
    case Fragment::personAhead:return "Person ahead.";case Fragment::twoWheelerAhead:return "Two-wheeler ahead.";
    case Fragment::vehicleAhead:return "Vehicle ahead.";case Fragment::truckAhead:return "Truck ahead.";
    case Fragment::driftingLeft:return "You are drifting left.";case Fragment::driftingRight:return "You are drifting right.";
    case Fragment::brakeGently:return "Brake gently.";case Fragment::brakeNow:return "Brake now.";case Fragment::readyToStop:return "Be ready to stop.";
    case Fragment::stopAndWait:return "Stop and wait.";case Fragment::easeOff:return "Ease off the accelerator.";
    case Fragment::coverBrake:return "Cover the brake.";case Fragment::keepLeft:return "Keep left.";case Fragment::giveSpace:return "Give them space.";
    case Fragment::safeGap:return "Keep a safe gap.";case Fragment::downSecond:return "Change down to second.";
    case Fragment::downThird:return "Change down to third.";case Fragment::clutchDown:return "Clutch down.";
    case Fragment::holdBrake:return "Hold the brake.";case Fragment::checkMirrors:return "Check your mirrors.";
    case Fragment::signalLeft:return "Signal left.";case Fragment::signalRight:return "Signal right.";
    case Fragment::lookBothWays:return "Look both ways.";case Fragment::steadyAccelerator:return "Steady accelerator.";
    case Fragment::laneCentre:return "Move back to the centre of your lane.";case Fragment::letThemPass:return "Let them pass.";
    case Fragment::lookAgainMoveOff:return "Look again before you move off.";
    case Fragment::soundHorn:return "Sound the horn.";case Fragment::bumpAheadContext:return "Speed bump ahead.";
    case Fragment::in200m:return "In 200 metres,";case Fragment::in150m:return "In 150 metres,";
    case Fragment::in100m:return "In 100 metres,";case Fragment::in50m:return "In 50 metres,";
    case Fragment::turnLeft:return "turn left.";case Fragment::turnRight:return "turn right.";
    case Fragment::keepLeftRoute:return "keep left.";case Fragment::keepRightRoute:return "keep right.";
    case Fragment::uTurn:return "make a U-turn.";
    case Fragment::roundaboutFirst:return "at the roundabout, take the first exit.";
    case Fragment::roundaboutSecond:return "at the roundabout, take the second exit.";
    case Fragment::roundaboutThird:return "at the roundabout, take the third exit.";
    case Fragment::roundaboutFourth:return "at the roundabout, take the fourth exit.";
    case Fragment::roundaboutEnter:return "enter the roundabout.";
    case Fragment::destinationAhead:return "your destination is ahead.";
    case Fragment::leftTurnHere:return "Left turn here.";case Fragment::rightTurnHere:return "Right turn here.";
    case Fragment::thenLeft:return "Then turn left straight after.";case Fragment::thenRight:return "Then turn right straight after.";
    case Fragment::giveWayRight:return "Give way to traffic from the right.";case Fragment::pullOver:return "Pull over safely.";
    case Fragment::mergeCarefully:return "merge carefully.";
    case Fragment::downhillAhead:return "Downhill ahead.";case Fragment::steepDownhillAhead:return "Steep downhill ahead.";
    case Fragment::uphillAhead:return "Uphill ahead.";case Fragment::steepUphillAhead:return "Steep uphill ahead.";
    case Fragment::noNeutral:return "Do not coast in neutral.";case Fragment::controlSpeedBrake:return "Control your speed with the brake.";
    case Fragment::downOneGear:return "Change down a gear.";
    default:return "";
    }
}
// Composed utterance ids: 1000 + priority*100 + slot. Priority is recoverable
// without engine state, so interruption rules stay static and testable.
constexpr int compositeBase=1000;
inline bool isComposite(int id){return id>=compositeBase&&id<compositeBase+700;}
inline int compositePriority(int id){return isComposite(id)?(id-compositeBase)/100:0;}
// Junction lesson steps (origin 5 task): planning, approach, entry.
inline bool isLessonPrompt(Prompt p){const int i=int(p);return (i>=56&&i<=62);}
// Speed-conditioned actions for a camera road user (origin 4).
inline bool isActorActionPrompt(Prompt p){const int i=int(p);return i>=64&&i<=68;}
// Prompts the risk coach (origin 7) may use.
inline bool riskMayUse(Prompt p){return isRiskPrompt(p)||isActorActionPrompt(p)||p==Prompt::pedestrianSpace||p==Prompt::riderSpace||p==Prompt::vehicleSpace;}
// Vehicle-control coaching from measured speed/slope/route (origin 6).
inline bool isControlPrompt(Prompt p){const int i=int(p);return i==63||(i>=69&&i<=80)||(i>=87&&i<=95);}
inline bool isMapHillPrompt(Prompt p){const int i=int(p);return i>=92&&i<=95;}
// Proposed coaching bands, not certified road classifications or safe speeds.
inline int slopeBand(double degrees) {
    if(!std::isfinite(degrees)||std::abs(degrees)>30)return 0;
    const double angle=std::abs(degrees);
    const int band=angle>=12?4:angle>=8?3:angle>=5?2:angle>=2?1:0;
    return degrees<0?-band:band;
}
inline int slopePromptBand(Prompt p) {
    switch(p) {
    case Prompt::gentleUp:return 1;
    case Prompt::hill:case Prompt::uphill:return 2;
    case Prompt::steepUp:return 3;
    case Prompt::verySteepUp:return 4;
    case Prompt::gentleDown:return -1;
    case Prompt::downhill:case Prompt::downhillGear:return -2;
    case Prompt::steepDown:case Prompt::steepDownGear:return -3;
    case Prompt::verySteepDown:return -4;
    default:return 0;
    }
}
// A wording threshold, not a claim that this speed is safe for every hazard.
constexpr double lowAdviceSpeedKmh=10;
inline int priority(Prompt p) {
    switch(p) {
    case Prompt::camera:case Prompt::roadUser:case Prompt::roadUserQuiet:case Prompt::traffic:
    case Prompt::slopeUnavailable:case Prompt::maintainSpeed:case Prompt::observeSign:
    case Prompt::pedestrianSpace:case Prompt::riderSpace:case Prompt::vehicleSpace:return 1;
    case Prompt::hill:case Prompt::uphill:case Prompt::downhill:case Prompt::bendLeft:case Prompt::bendRight:
    case Prompt::downhillGear:case Prompt::shiftUp:case Prompt::shiftDown:case Prompt::closeQuiet:case Prompt::closeGearQuiet:
    case Prompt::bendLeftQuiet:case Prompt::bendRightQuiet:case Prompt::bendLeftGear:case Prompt::bendRightGear:
    case Prompt::imageClose:case Prompt::imageCloseGear:case Prompt::gentleUp:case Prompt::gentleDown:
    case Prompt::steepUp:case Prompt::steepDownGear:case Prompt::steepDown:case Prompt::verySteepUp:case Prompt::verySteepDown:
    case Prompt::prepareBend:case Prompt::mapLeftBend:case Prompt::mapRightBend:case Prompt::mapJunction:case Prompt::mapLeftTurn:case Prompt::mapRightTurn:
    case Prompt::mapLeftTurnNear:case Prompt::mapRightTurnNear:case Prompt::mapLeftTurn50:case Prompt::mapRightTurn50:
    case Prompt::junctionPlanLeft:case Prompt::junctionPlanRight:case Prompt::junctionPlan:case Prompt::junctionApproach:
    case Prompt::junctionBrakeGearDown:case Prompt::junctionGearDown:case Prompt::junctionEntry:case Prompt::turnExit:
    case Prompt::personEaseCover:case Prompt::riderEaseCover:case Prompt::vehicleEaseGap:
    case Prompt::gearUpSecond:case Prompt::gearUpThird:case Prompt::gearUpFourth:case Prompt::gearUpFifth:
    case Prompt::gearDownSecond:case Prompt::gearDownThird:case Prompt::stopClutch:case Prompt::moveOffPrep:
    case Prompt::downhillThird:case Prompt::downhillSecond:case Prompt::uphillSecond:case Prompt::uphillThird:return 2;
    case Prompt::mapDownhill:case Prompt::mapSteepDownhill:case Prompt::mapUphill:case Prompt::mapSteepUphill:return 2;
    case Prompt::brakeReadyStop:case Prompt::clutchHoldBrake:case Prompt::oncomingKeepLeft:return 4;
    case Prompt::vehicleCrossingWait:case Prompt::personCrossingStop:return 5;
    case Prompt::personLeftSpace:case Prompt::personRightSpace:case Prompt::driftLeftCentre:case Prompt::driftRightKeepLeft:return 3;
    case Prompt::bumpAhead:return 3;case Prompt::bendHorn:return 2;case Prompt::routeChanged:return 2;
    case Prompt::crossedLookAgain:return 2;
    case Prompt::ease:case Prompt::close:case Prompt::closeGear:case Prompt::holdLaneDistance:case Prompt::keepSafeGap:
    case Prompt::slowSmoothly:case Prompt::watchPedestrians:return 3;
    case Prompt::brake:return 4;
    case Prompt::emergency:case Prompt::nearbyCollision:return 5;
    case Prompt::brakeHarder:case Prompt::emergencyStop:return 6;
    default:return 0;
    }
}
inline const char* promptText(Prompt p) {
    switch(p) {
    case Prompt::junctionPlanLeft:return "Left turn ahead. Check your mirrors and signal left";
    case Prompt::junctionPlanRight:return "Right turn ahead. Check your mirrors and signal right";
    case Prompt::junctionPlan:return "Junction ahead. Check your mirrors and ease off the accelerator";
    case Prompt::junctionBrakeGearDown:return "Ease off the accelerator and brake gently. Change down to second as you slow";
    case Prompt::junctionGearDown:return "Change down to second gear and cover the brake";
    case Prompt::junctionEntry:return "Clutch down if you need to stop. Look both ways before you turn";
    case Prompt::turnExit:return "Straighten the wheel and add gentle accelerator";
    case Prompt::personEaseCover:return "Person ahead. Ease off the accelerator and cover the brake";
    case Prompt::riderEaseCover:return "Two-wheeler ahead. Ease off the accelerator and cover the brake";
    case Prompt::vehicleEaseGap:return "Vehicle ahead. Ease off and keep a safe gap";
    case Prompt::brakeReadyStop:return "Brake gently and be ready to stop";
    case Prompt::clutchHoldBrake:return "Clutch down and hold the brake";
    case Prompt::gearUpSecond:return "Change up to second gear";
    case Prompt::gearUpThird:return "Change up to third gear";
    case Prompt::gearUpFourth:return "Change up to fourth gear";
    case Prompt::gearUpFifth:return "Change up to fifth gear";
    case Prompt::gearDownSecond:return "Change down to second gear";
    case Prompt::gearDownThird:return "Change down to third gear";
    case Prompt::stopClutch:return "Clutch down as you stop";
    case Prompt::moveOffPrep:return "When it is clear: first gear, check mirrors and signal, then release the clutch slowly";
    case Prompt::downhillThird:return "Downhill. Change down to third and control your speed with the brake";
    case Prompt::downhillSecond:return "Steep downhill. Change down to second and control your speed with the brake";
    case Prompt::uphillSecond:return "Uphill. Change down to second and keep a steady accelerator";
    case Prompt::uphillThird:return "Uphill. Change down to third and keep a steady accelerator";
    case Prompt::vehicleCrossingWait:return "Vehicle crossing ahead. Brake and wait";
    case Prompt::personCrossingStop:return "Person crossing ahead. Stop and let them cross";
    case Prompt::oncomingKeepLeft:return "Oncoming vehicle. Keep left and ease off";
    case Prompt::personLeftSpace:return "Person on the left. Ease off and give them space";
    case Prompt::personRightSpace:return "Person on the right. Ease off and give them space";
    case Prompt::crossedLookAgain:return "It has passed. Look both ways again before you move off";
    case Prompt::driftLeftCentre:return "You are drifting left. Move back to the centre of your lane";
    case Prompt::driftRightKeepLeft:return "You are drifting right. Keep left";
    case Prompt::bumpAhead:return "Speed bump ahead. Slow down, clutch down and change down";
    case Prompt::bendHorn:return "Sharp bend ahead. Sound the horn and keep left";
    case Prompt::routeChanged:return "The route has changed. Keep going and follow the new route";
    case Prompt::mapDownhill:return "Downhill ahead. Stay in gear and control your speed with the brake";
    case Prompt::mapSteepDownhill:return "Steep downhill ahead. Change down to second and control your speed with the brake";
    case Prompt::mapUphill:return "Uphill ahead. Keep a steady accelerator";
    case Prompt::mapSteepUphill:return "Steep uphill ahead. Change down before the climb and keep a steady accelerator";
    case Prompt::junctionApproach:return "Set a suitable approach speed. Prepare to give way";
    case Prompt::hill: return "Uphill. Lower gear if needed";
    case Prompt::ease: return "Ease off accelerator";
    case Prompt::brake: return "Brake gently";
    case Prompt::emergency: return "Collision risk. Brake now";
    case Prompt::camera: return "Camera is on. Watch the road and keep a safe gap";
    case Prompt::close: return "Obstacle in your path. Slow down and be ready to stop";
    case Prompt::roadUser: return "Road user ahead. Slow down and give them space";
    case Prompt::traffic: return "Traffic ahead. Keep a safe gap";
    case Prompt::pedestrianSpace: return "Person ahead. Give space";
    case Prompt::riderSpace: return "Two-wheeler ahead. Give space";
    case Prompt::vehicleSpace: return "Vehicle ahead. Keep a gap";
    case Prompt::mapLeftBend: return "Left bend ahead. Prepare early";
    case Prompt::mapRightBend: return "Right bend ahead. Prepare early";
    case Prompt::mapJunction: return "Junction ahead. Check for traffic";
    case Prompt::mapLeftTurn: return "Left turn ahead. Check for traffic";
    case Prompt::mapRightTurn: return "Right turn ahead. Check for traffic";
    case Prompt::mapLeftTurnNear: return "Left turn in about ten metres";
    case Prompt::mapRightTurnNear: return "Right turn in about ten metres";
    case Prompt::mapLeftTurn50: return "Left turn in about fifty metres";
    case Prompt::mapRightTurn50: return "Right turn in about fifty metres";
    case Prompt::uphill: return "Uphill. Steady power";
    case Prompt::downhill: return "Downhill. Control speed";
    case Prompt::bendLeft: return "Left bend ahead. Ease off early and stay in your lane";
    case Prompt::bendRight: return "Right bend ahead. Ease off early and stay in your lane";
    case Prompt::downhillGear: return "Downhill. Use engine braking";
    case Prompt::shiftUp: return "Engine revs are high. Shift up one gear when safe";
    case Prompt::shiftDown: return "Engine revs are low under load. Shift down one gear when safe";
    case Prompt::closeQuiet: return "Obstacle ahead. Keep space and be ready to stop";
    case Prompt::closeGearQuiet: return "Obstacle ahead. Keep space. Select a lower gear only if the engine struggles";
    case Prompt::roadUserQuiet: return "Road user ahead. Give them space";
    case Prompt::nearbyCollision: return "Collision risk nearby. Stay alert";
    case Prompt::bendLeftQuiet: return "Left bend ahead. Stay in your lane and look through the bend";
    case Prompt::bendRightQuiet: return "Right bend ahead. Stay in your lane and look through the bend";
    case Prompt::bendLeftGear: return "Left bend ahead. Set your speed and gear before the bend. Stay in your lane";
    case Prompt::bendRightGear: return "Right bend ahead. Set your speed and gear before the bend. Stay in your lane";
    case Prompt::closeGear: return "Obstacle ahead. Slow down smoothly. Change down only if the engine struggles as you slow";
    case Prompt::imageClose: return "Obstacle ahead. Keep a safe gap and be ready to stop";
    case Prompt::imageCloseGear: return "Obstacle ahead. Keep a safe gap. Change down only if the engine struggles";
    case Prompt::gentleUp: return "Gentle uphill. Steady power";
    case Prompt::gentleDown: return "Gentle downhill. Watch speed";
    case Prompt::steepUp: return "Steep uphill. Avoid engine strain";
    case Prompt::steepDownGear: return "Steep downhill. Suitable low gear";
    case Prompt::steepDown: return "Steep downhill. Control speed";
    case Prompt::verySteepUp: return "Very steep uphill. Controlled power";
    case Prompt::verySteepDown: return "Very steep downhill. Brake as needed";
    case Prompt::slopeUnavailable: return "Slope reading unavailable";
    case Prompt::maintainSpeed: return "Maintain speed";
    case Prompt::holdLaneDistance: return "Hold lane and distance";
    case Prompt::keepSafeGap: return "Keep a safe gap";
    case Prompt::slowSmoothly: return "Slow down smoothly";
    case Prompt::watchPedestrians: return "Watch for pedestrians";
    case Prompt::prepareBend: return "Prepare for the bend";
    case Prompt::observeSign: return "Observe the road sign";
    case Prompt::brakeHarder: return "Brake harder";
    case Prompt::emergencyStop: return "Emergency stop";
    default: return "";
    }
}
inline bool mayInterrupt(Prompt next,Prompt current) {
    // Routine map, lesson and vehicle-control cues never interrupt speech.
    const bool map=(int(next)>=int(Prompt::mapLeftBend)&&int(next)<=int(Prompt::turnExit))||(isControlPrompt(next))||
        next==Prompt::personEaseCover||next==Prompt::riderEaseCover||next==Prompt::vehicleEaseGap||next==Prompt::crossedLookAgain;
    if(map&&current!=Prompt::none)return false;
    if(priority(next)>priority(current))return true;
    const bool image=current==Prompt::imageClose||current==Prompt::imageCloseGear||current==Prompt::closeQuiet||current==Prompt::closeGearQuiet;
    const bool guidance=slopePromptBand(next)!=0||next==Prompt::shiftUp||next==Prompt::shiftDown||
        next==Prompt::bendLeft||next==Prompt::bendRight||next==Prompt::bendLeftQuiet||next==Prompt::bendRightQuiet||next==Prompt::bendLeftGear||next==Prompt::bendRightGear;
    return image&&guidance&&priority(next)>=priority(current);
}
struct Hazard { double time{}, tti=infinity, confidence{}; bool inPath{}; };
struct Decision {
    Prompt prompt=Prompt::none; double bufferSeconds{}; std::string reason;
    int actorId=0,actorCategory=-1,evidenceKind=0; // 1 sustained attention, 2 optical approach, 3 critical approach.
    float actorX=0,actorY=0,actorW=0,actorH=0;
    // A source-local inward entry, independently seen beside an already spoken
    // actor. This is encounter provenance, never lane or depth certainty.
    int entryReferenceId=0;
    bool newEncounter=false;
};
// The analysed camera image's optics, normalized to that image (after the preview crop and
// rotation): focal lengths in image widths / heights per unit tangent, principal point as
// fractions. The defaults are the 4:3 phone frame the coach's geometry was first tuned on
// (~70 degree horizontal view, optical centre mid-image). A preview that fills a wide panel
// crops the frame, so the same angle covers more of the image and the level horizon is no
// longer mid-image (0.8.1: the bottom 72 % of a 4:3 frame).
struct CameraModel {
    double fx=.72,fy=.96,cx=.5,cy=.5; bool measured{};
    bool valid() const noexcept {return std::isfinite(fx)&&std::isfinite(fy)&&std::isfinite(cx)&&std::isfinite(cy)&&
        fx>.2&&fx<5&&fy>.2&&fy<5&&cx>-.5&&cx<1.5&&cy>-.5&&cy<1.5;}
};
struct Box {
    float x{},y{},w{},h{},score{}; int category{},id{}; double tti=infinity, expansion{},confidence{};
    // Timestamp of independently matched, trajectory-supported neural lead.
    // Optical expansion or an image attention zone alone cannot establish it.
    double leadTime=-infinity;
};
struct LaneMap {
    int width{},height{};
    std::vector<float> road, lane;
    bool inRoad(float x,float y) const {
        if(width<=0||height<=0||road.empty()) return false;
        int ix=std::clamp(int(x*width),0,width-1), iy=std::clamp(int(y*height),0,height-1);
        return road.at(iy*width+ix)>.6f;
    }
};
} // namespace ld
