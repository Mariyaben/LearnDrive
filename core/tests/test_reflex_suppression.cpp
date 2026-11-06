#include "learndrive/ReflexEngine.hpp"
#include <iostream>
using namespace ld;
using namespace ld::cognition;
int main() {
    SceneState s; s.captureTime=10; s.telemetry.time=10; s.count=1;
    s.hazards[0]={1,HazardType::bus,Lane::ego,20,2.5,.9,true,false};
    s.telemetry.longitudinalAcceleration=-4.0;
    ReflexEngine reflex;
    auto routine=reflex.evaluate(s,10);
    if (!routine.suppressDistanceAlerts || routine.command!=ReflexCommand::none) return 1;
    s.hazards[0].ttiSeconds=1.0;
    auto critical=reflex.evaluate(s,10);
    if (!critical.suppressDistanceAlerts || critical.command!=ReflexCommand::BRAKE_HARDER ||
        critical.driverAction!=DriverAction::hardBraking) return 2;
    s.telemetry.longitudinalAcceleration=-1.799;
    if(classifyDriverAction(s.telemetry,10)!=DriverAction::coasting) return 3;
    s.telemetry.longitudinalAcceleration=-1.8;
    if(classifyDriverAction(s.telemetry,10)!=DriverAction::moderateBraking) return 4;
    s.telemetry.longitudinalAcceleration=-3.5;
    if(classifyDriverAction(s.telemetry,10)!=DriverAction::moderateBraking) return 5;
    s.telemetry.longitudinalAcceleration=-3.501;
    if(classifyDriverAction(s.telemetry,10)!=DriverAction::hardBraking) return 6;
    s.hazards[0].ttiSeconds=1.2;
    if(reflex.evaluate(s,10).command!=ReflexCommand::none) return 7;
    s.hazards[0].ttiSeconds=1.199;
    if(reflex.evaluate(s,10).command!=ReflexCommand::BRAKE_HARDER) return 8;
    s.telemetry.time=9;
    if(classifyDriverAction(s.telemetry,10)!=DriverAction::coasting) return 9;
    std::cout << "hard-braking suppression and BRAKE_HARDER override passed\n";
}
