#include "learndrive/decision_benchmark.hpp"
#include "learndrive/ContextualPolicyEngine.hpp"
#include <chrono>
namespace ld {
void benchmarkDecisions(double out[5]) {
    cognition::SceneState scene;scene.captureTime=10;scene.telemetry.time=10;scene.telemetry.speedValid=true;
    scene.telemetry.speedKmh=54;scene.count=cognition::maxHazards;
    for(size_t i=0;i<scene.count;++i)scene.hazards[i]={i+1,i%2?cognition::HazardType::bus:cognition::HazardType::pothole,
        cognition::Lane::ego,40,5,.9,true,false,1,0};
    cognition::ReflexEngine reflex;cognition::ContextualPolicyEngine policy;
    std::array<double,10000> timings{};double sum=0;volatile int result=0;
    for(auto& t:timings) {
        const auto begin=std::chrono::steady_clock::now();
        result+=int(reflex.evaluate(scene,10).command)+int(policy.evaluate(scene,10).action);
        t=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();sum+=t;
    }
    std::sort(timings.begin(),timings.end());
    out[0]=timings[5000];out[1]=timings[9500];out[2]=timings[9900];out[3]=timings.back();out[4]=sum/timings.size();
    (void)result;
}
}
