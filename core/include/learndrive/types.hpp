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
    mapLeftTurnNear=52,mapRightTurnNear=53,mapLeftTurn50=54,mapRightTurn50=55 };
constexpr int promptCount=55;
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
    case Prompt::mapLeftTurnNear:case Prompt::mapRightTurnNear:case Prompt::mapLeftTurn50:case Prompt::mapRightTurn50:return 2;
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
    const bool map=int(next)>=int(Prompt::mapLeftBend)&&int(next)<=int(Prompt::mapRightTurn50);
    if(map&&current!=Prompt::none)return false; // Routine map cues never interrupt speech.
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
