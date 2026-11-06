#include "learndrive/ContextualPolicyEngine.hpp"
#include <iostream>
using namespace ld;
using namespace ld::cognition;
struct Audio:SpeechDispatcher {
    int count{},priority{};bool flush{},stopped{};const char* last="";
    bool trySpeak(const SpeechCommand& c) noexcept override {++count;priority=c.priority;flush=c.flush;last=c.text;return true;}
    void stopDistanceAlerts() noexcept override {stopped=true;}
};
int main() {
    SceneState s; s.captureTime=10; s.telemetry.time=10; s.telemetry.speedValid=true; s.telemetry.speedKmh=50;
    s.telemetry.pitchValid=true; s.telemetry.pitchDegrees=8.5;
    s.count=3;
    s.hazards[0]={1,HazardType::pothole,Lane::ego,12,2.4,.9,true,false,.5,0};
    s.hazards[1]={2,HazardType::bus,Lane::ego,25,3.8,.95,true,true,.7,-4};
    s.hazards[2]={3,HazardType::bend,Lane::ego,infinity,infinity,.9,true,false};
    const auto d=ContextualPolicyEngine{}.evaluate(s,10);
    if (d.action!=Action::ACTION_HOLD_DISTANCE_BUS || !d.suppressOtherAlerts || d.threatScore<=0) return 1;
    Audio audio;PriorityArbiter arbiter(audio);
    if(!arbiter.contextual({d.action,d.priority,d.suppressOtherAlerts,10,1},s,1,10))return 2;
    if(arbiter.minor(Action::slowSmoothly,10.1))return 3;
    arbiter.reflex({ReflexCommand::BRAKE_HARDER,true,2,0},10.2);
    if(audio.count!=2||audio.priority!=0||!audio.flush||std::string_view(audio.last)!="Brake harder")return 4;
    arbiter.reflex({ReflexCommand::BRAKE_HARDER,true,2,0},10.21);
    if(audio.count!=2||audio.stopped)return 5; // ongoing emergency must not be cut every tick
    s.count=2;s.hazards[0].type=HazardType::pedestrian;s.hazards[0].ttiSeconds=4;s.hazards[0].lateralDistanceM=2;
    s.hazards[1].type=HazardType::vehicle;s.hazards[1].ttiSeconds=3;s.hazards[1].lateralDistanceM=.5;
    if(ContextualPolicyEngine{}.evaluate(s,10).action!=Action::keepSafeGap)return 6;
    s.hazards[0].lateralDistanceM=.25;
    if(ContextualPolicyEngine{}.evaluate(s,10).action!=Action::watchPedestrians)return 7;
    s.hazards[0].type=s.hazards[1].type=HazardType::tailgater;
    if(ContextualPolicyEngine{}.evaluate(s,10).action!=Action::none)return 8;
    SceneState urgent;urgent.captureTime=urgent.telemetry.time=20;urgent.count=1;
    urgent.hazards[0]={1,HazardType::vehicle,Lane::ego,8,1,.95,true,false};
    EmbeddedDualBrain brain;
    auto first=brain.evaluate(urgent,20);
    if(first.reflexCommand!=ReflexCommand::EMERGENCY_STOP)return 9;
    brain.accepted(Action::none,true,20);
    urgent.captureTime=urgent.telemetry.time=20.01;urgent.hazards[0].ttiSeconds=1.5;
    if(brain.evaluate(urgent,20.01).brakeNow)return 10;
    urgent.captureTime=urgent.telemetry.time=20.02;urgent.hazards[0].ttiSeconds=1;
    if(brain.evaluate(urgent,20.02).brakeNow)return 11;
    urgent.captureTime=urgent.telemetry.time=20.03;urgent.telemetry.longitudinalAcceleration=-4;
    if(brain.evaluate(urgent,20.03).reflexCommand!=ReflexCommand::BRAKE_HARDER)return 12;
    std::cout << "bus threat suppressed pothole and incline guidance\n";
}
