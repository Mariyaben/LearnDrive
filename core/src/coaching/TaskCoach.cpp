#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/TaskCoach.hpp"
#include "learndrive/SceneState.hpp"
#include <cmath>
#include <sstream>
namespace ld::coaching {
namespace {
double lead(const WorldState& w,double now) noexcept {
    if(!w.active||!w.route.valid||!w.route.routeId||!w.route.eventId||w.route.feature<3||w.route.feature>5||
       !cognition::fresh(now,w.route.captureTime,w.route.provider?3.:2.5)||!cognition::fresh(now,w.route.gpsTime,2.5)||
       !cognition::fresh(now,w.captureTime)||!cognition::fresh(now,w.telemetry.time,.15)||!w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)||w.telemetry.speedKmh<=3||w.telemetry.speedKmh>300||
       !std::isfinite(w.route.distanceM)||w.route.distanceM<0)return -1;
    double seconds=w.route.distanceM/(w.telemetry.speedKmh/3.6);
    if(w.route.provider){if(!std::isfinite(w.route.etaSeconds)||w.route.etaSeconds<0)return -1;seconds=std::min(seconds,w.route.etaSeconds);}
    return seconds;
}
bool delivered(const Event& e) noexcept {return e.visualOnly||e.deliveryStage==DeliveryStage::Completed;}
const char* phase(TaskPhase p) {
    switch(p){case TaskPhase::Off:return "OFF";case TaskPhase::Waiting:return "WAITING_FOR_ROUTE";case TaskPhase::Preparing:return "PREPARING";
    case TaskPhase::AwaitingDelivery:return "AWAITING_DELIVERY";case TaskPhase::ObservingResponse:return "OBSERVING_RESPONSE";
    case TaskPhase::Approach:return "APPROACH_PREPARATION";case TaskPhase::ExecutionQuiet:return "EXECUTION_QUIET";
    case TaskPhase::Uncertain:return "EVIDENCE_UNAVAILABLE";case TaskPhase::WaitingWindow:return "WAITING_FOR_APPROACH";default:return "OUTCOME_UNASSESSED";}
}
}
void TaskCoach::enable(bool value) noexcept {if(value==enabled_)return;enabled_=value;state_={};state_.phase=value?TaskPhase::Waiting:TaskPhase::Off;}
void TaskCoach::sourceChanged() noexcept {
    if(state_.event){++state_.unassessedOutcomes;state_.event=0;}
    state_.phase=enabled_?TaskPhase::Waiting:TaskPhase::Off;state_.planningDelivered=state_.approachDelivered=state_.speedReductionObserved=false;
}
bool TaskCoach::evidence(const Candidate& c,const WorldState& w,double now) noexcept {
    const double seconds=lead(w,now);
    if(seconds<0||w.telemetry.braking||w.telemetry.hardBraking||w.telemetry.shock||c.origin!=5||c.kind!=EventKind::Route||c.urgent||c.critical||c.lessonEpoch!=w.route.epoch)return false;
    if(c.lessonStep==1){const Prompt expected=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;
        return c.prompt==expected&&seconds>=22&&seconds<=40;}
    // The 3.8 s recorded approach cue needs room to finish before the 8 s
    // quiet boundary. These are conservative experimental timing windows.
    return c.lessonStep==2&&c.prompt==Prompt::junctionApproach&&seconds>=13&&seconds<=18;
}
void TaskCoach::observe(const WorldState& w,const ShortMemory& memory,const EventLedger& ledger) noexcept {
    if(!enabled_)return;
    if(!w.active){sourceChanged();return;}
    if(!w.route.routeId||!w.route.eventId){sourceChanged();return;}
    const double seconds=lead(w,w.now);state_.leadSeconds=seconds;
    const bool different=state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch;
    if(seconds<0||w.telemetry.braking||w.telemetry.hardBraking||w.telemetry.shock){state_.phase=TaskPhase::Uncertain;return;}
    if(different){
        if(state_.event)++state_.unassessedOutcomes;
        state_.source=w.sourceId;state_.route=w.route.routeId;state_.event=w.route.eventId;state_.epoch=w.route.epoch;
        state_.startedAt=w.now;state_.responseSince=-1;state_.planningDelivered=state_.approachDelivered=state_.speedReductionObserved=false;++state_.opportunities;
    }
    bool submitted=false;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.identity.space==IdentitySpace::Route&&e.identity.id==w.route.eventId&&
        e.identity.scope==w.route.routeId&&e.lessonEpoch==state_.epoch&&e.acceptedAt>=state_.startedAt&&e.lessonStep){
        if(delivered(e)&&e.lessonStep==1&&!state_.planningDelivered){state_.planningDelivered=true;state_.responseSince=w.now;++state_.planningCompletions;}
        if(delivered(e)&&e.lessonStep==2&&!state_.approachDelivered){state_.approachDelivered=true;++state_.approachCompletions;}
        submitted|=e.deliveryStage==DeliveryStage::Submitted||e.deliveryStage==DeliveryStage::Started;
    }
    if(state_.planningDelivered&&memory.speedTrend(w.now,state_.responseSince)==SpeedTrend::Decreasing&&!state_.speedReductionObserved){state_.speedReductionObserved=true;++state_.observedReductions;}
    if(seconds<8){state_.phase=TaskPhase::ExecutionQuiet;return;}
    if(submitted){state_.phase=TaskPhase::AwaitingDelivery;return;}
    if(!state_.planningDelivered)state_.phase=seconds>40?TaskPhase::WaitingWindow:seconds>=22?TaskPhase::Preparing:TaskPhase::ExecutionQuiet;
    else if(seconds>=13&&seconds<=18&&!state_.approachDelivered&&!state_.speedReductionObserved)state_.phase=TaskPhase::Approach;
    else state_.phase=TaskPhase::ObservingResponse;
}
Candidate TaskCoach::proposal(const WorldState& w) const noexcept {
    Candidate c;
    if(state_.phase==TaskPhase::Preparing){c.lessonStep=1;c.prompt=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;}
    else if(state_.phase==TaskPhase::Approach){c.lessonStep=2;c.prompt=Prompt::junctionApproach;}
    else return c;
    c.origin=5;c.kind=EventKind::Route;c.identity={IdentitySpace::Route,w.route.eventId,w.route.routeId};c.supported=true;c.lessonEpoch=w.route.epoch;
    c.captureTime=w.route.captureTime;c.maxAge=w.route.provider?3:2.5;c.expiresAt=c.captureTime+c.maxAge;
    if(!evidence(c,w,w.now))return {};
    return c;
}
bool TaskCoach::valid(const Candidate& c,const WorldState& w,double now,const EventLedger& ledger) const noexcept {
    if(!c.lessonStep){const double seconds=lead(w,now);return int(c.prompt)<56&&(!enabled_||c.urgent||seconds<0||seconds>=8);}
    if(!enabled_||state_.phase==TaskPhase::Uncertain||state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch||!evidence(c,w,now))return false;
    if(c.lessonStep==1)return !state_.planningDelivered;
    if(!state_.planningDelivered||state_.speedReductionObserved||state_.approachDelivered)return false;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.identity==c.identity&&e.lessonStep==1&&e.lessonEpoch==state_.epoch&&e.acceptedAt>=state_.startedAt&&delivered(e))return true;
    return false;
}
std::string TaskCoach::json() const {
    std::ostringstream o;o<<"{\"lesson\":\"junction_preparation\",\"enabled\":"<<(enabled_?"true":"false")<<",\"phase\":\""<<phase(state_.phase)<<"\",\"opportunities\":"<<state_.opportunities
    <<",\"planning_deliveries\":"<<state_.planningCompletions<<",\"approach_deliveries\":"<<state_.approachCompletions<<",\"observed_speed_reductions\":"<<state_.observedReductions
    <<",\"unassessed_outcomes\":"<<state_.unassessedOutcomes<<",\"mirror_check\":\"UNOBSERVED\",\"signal\":\"UNOBSERVED\",\"position\":\"UNOBSERVED\",\"controls\":\"UNOBSERVED\",\"driving_outcome\":\"UNASSESSED\"}";return o.str();
}
}
