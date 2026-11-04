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
    case TaskPhase::Uncertain:return "EVIDENCE_UNAVAILABLE";case TaskPhase::Entry:return "JUNCTION_ENTRY";case TaskPhase::WaitingWindow:return "WAITING_FOR_APPROACH";default:return "OUTCOME_UNASSESSED";}
}
}
void TaskCoach::enable(bool value) noexcept {if(value==enabled_)return;enabled_=value;state_={};state_.phase=value?TaskPhase::Waiting:TaskPhase::Off;}
bool TaskCoach::reserves(double block) const noexcept {
    if(!scheduling_||!enabled_||!std::isfinite(block)||block<=0||state_.leadSeconds<0||!state_.event)return false;
    const double lead=state_.leadSeconds,after=lead-block;
    // Planning not yet offered and its window still ahead.
    if(!state_.planningDelivered&&state_.phase==TaskPhase::WaitingWindow&&lead>planningWindowOpen)
        return after<planningWindowClose+minimumUsefulWindow;
    // Optional approach reminder still pending and its window still ahead.
    if(state_.planningDelivered&&!state_.approachDelivered&&(!state_.speedReductionObserved||scheduling_)&&lead>approachWindowOpen)
        return after<approachWindowClose+minimumUsefulWindow;
    return false;
}
void TaskCoach::sourceChanged() noexcept {
    if(state_.event){++state_.unassessedOutcomes;state_.event=0;}
    state_.phase=enabled_?TaskPhase::Waiting:TaskPhase::Off;state_.planningDelivered=state_.approachDelivered=state_.speedReductionObserved=state_.entryDelivered=false;
}
bool TaskCoach::evidence(const Candidate& c,const WorldState& w,double now,bool started) noexcept {
    double seconds=lead(w,now);
    // Started speech: slowing to a stop at this junction is the expected response,
    // not lost evidence. Same fresh route/GPS/camera, measured speed at or below 3 km/h.
    if(started&&seconds<0&&w.active&&w.route.valid&&w.route.feature>=3&&w.route.feature<=5&&
       cognition::fresh(now,w.route.captureTime,w.route.provider?3.:2.5)&&cognition::fresh(now,w.route.gpsTime,2.5)&&
       cognition::fresh(now,w.captureTime)&&cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&
       std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>=0&&w.telemetry.speedKmh<=3&&!w.telemetry.shock&&
       c.origin==5&&c.kind==EventKind::Route&&c.lessonEpoch==w.route.epoch&&(c.lessonStep==2||c.lessonStep==3))return true;
    if(seconds<0||w.telemetry.braking||w.telemetry.hardBraking||w.telemetry.shock||c.origin!=5||c.kind!=EventKind::Route||c.urgent||c.critical||c.lessonEpoch!=w.route.epoch)return false;
    if(c.lessonStep==1){const Prompt expected=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;
        // Started speech has no upper bound: slowing raises distance/speed lead.
        return c.prompt==expected&&seconds>=(started?executionQuietLead:planningWindowClose)&&(started||seconds<=planningWindowOpen);}
    // The 3.8 s recorded approach cue needs room to finish before the 8 s
    // quiet boundary. These are conservative experimental timing windows.
    if(c.lessonStep==3)return c.prompt==Prompt::junctionEntry&&seconds>=(started?0:1.5)&&seconds<executionQuietLead+(started?2:0)&&
        (started||w.telemetry.speedKmh<=20);
    const bool approachPrompt=c.prompt==Prompt::junctionApproach||c.prompt==Prompt::junctionGearDown||
        (c.prompt==Prompt::junctionBrakeGearDown&&(started||w.telemetry.speedKmh>20));
    return c.lessonStep==2&&approachPrompt&&seconds>=(started?executionQuietLead:approachWindowClose)&&(started||seconds<=approachWindowOpen);
}
void TaskCoach::observe(const WorldState& w,const ShortMemory& memory,const EventLedger& ledger) noexcept {
    if(!enabled_)return;
    if(!w.active){state_.quietUntil=-1;state_.quietEvent=0;sourceChanged();return;}
    // Route loss keeps any bounded execution hold (see TaskProgress::quietUntil).
    if(!w.route.routeId||!w.route.eventId){state_.leadSeconds=-1;sourceChanged();return;}
    // Fresh navigation has moved on to a different event: the held junction ended.
    if(state_.quietEvent&&w.route.valid&&w.route.eventId!=state_.quietEvent&&cognition::fresh(w.now,w.route.captureTime,w.route.provider?3.:2.5)){
        state_.quietUntil=-1;state_.quietEvent=0;}
    const double seconds=lead(w,w.now);state_.leadSeconds=seconds;
    const bool different=state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch;
    if(scheduling_&&seconds>=0) {
        if(seconds<executionQuietLead){if(state_.quietUntil<=w.now||state_.quietEvent!=w.route.eventId)++state_.quietHolds;
            state_.quietUntil=w.now+executionQuietHold;state_.quietEvent=w.route.eventId;}
        else {state_.quietUntil=-1;state_.quietEvent=0;} // Measured lead is clear of the execution window again.
    }
    if(seconds<0||w.telemetry.braking||w.telemetry.hardBraking||w.telemetry.shock){state_.phase=TaskPhase::Uncertain;return;}
    if(different){
        if(state_.event)++state_.unassessedOutcomes;
        state_.source=w.sourceId;state_.route=w.route.routeId;state_.event=w.route.eventId;state_.epoch=w.route.epoch;
        state_.startedAt=w.now;state_.responseSince=-1;state_.planningDelivered=state_.approachDelivered=state_.speedReductionObserved=state_.entryDelivered=false;++state_.opportunities;
    }
    bool submitted=false,entrySubmitted=false;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.identity.space==IdentitySpace::Route&&e.identity.id==w.route.eventId&&
        e.identity.scope==w.route.routeId&&e.lessonEpoch==state_.epoch&&e.acceptedAt>=state_.startedAt&&e.lessonStep){
        if(delivered(e)&&e.lessonStep==1&&!state_.planningDelivered){state_.planningDelivered=true;state_.responseSince=w.now;++state_.planningCompletions;}
        if(delivered(e)&&e.lessonStep==2&&!state_.approachDelivered){state_.approachDelivered=true;++state_.approachCompletions;}
        if(delivered(e)&&e.lessonStep==3&&!state_.entryDelivered){state_.entryDelivered=true;++state_.entryCompletions;}
        submitted|=e.deliveryStage==DeliveryStage::Submitted||e.deliveryStage==DeliveryStage::Started;
        entrySubmitted|=e.lessonStep==3&&(e.deliveryStage==DeliveryStage::Submitted||e.deliveryStage==DeliveryStage::Started);
    }
    if(state_.planningDelivered&&memory.speedTrend(w.now,state_.responseSince)==SpeedTrend::Decreasing&&!state_.speedReductionObserved){state_.speedReductionObserved=true;++state_.observedReductions;}
    if(seconds<8&&scheduling_&&entrySubmitted){state_.phase=TaskPhase::AwaitingDelivery;return;} // The entry instruction itself is playing.
    if(seconds<8){
        // One short control instruction at the junction itself (instructor-style), then quiet.
        state_.phase=scheduling_&&!submitted&&state_.planningDelivered&&!state_.entryDelivered&&seconds>=1.5&&w.telemetry.speedKmh<=20?
            TaskPhase::Entry:TaskPhase::ExecutionQuiet;return;}
    if(submitted){state_.phase=TaskPhase::AwaitingDelivery;return;}
    if(!state_.planningDelivered)state_.phase=seconds>40?TaskPhase::WaitingWindow:seconds>=22?TaskPhase::Preparing:TaskPhase::ExecutionQuiet;
    else if(seconds>=13&&seconds<=18&&!state_.approachDelivered&&(!state_.speedReductionObserved||scheduling_))state_.phase=TaskPhase::Approach;
    else state_.phase=TaskPhase::ObservingResponse;
}
Candidate TaskCoach::proposal(const WorldState& w) const noexcept {
    Candidate c;
    if(state_.phase==TaskPhase::Preparing){c.lessonStep=1;c.prompt=w.route.feature==4?Prompt::junctionPlanLeft:w.route.feature==5?Prompt::junctionPlanRight:Prompt::junctionPlan;}
    else if(state_.phase==TaskPhase::Approach){c.lessonStep=2;
        // Adapt rather than repeat: already slowing (or slow) only needs the gear and brake cover.
        c.prompt=!scheduling_?Prompt::junctionApproach:state_.speedReductionObserved||w.telemetry.speedKmh<=20?Prompt::junctionGearDown:Prompt::junctionBrakeGearDown;}
    else if(state_.phase==TaskPhase::Entry){c.lessonStep=3;c.prompt=Prompt::junctionEntry;}
    else return c;
    c.origin=5;c.kind=EventKind::Route;c.identity={IdentitySpace::Route,w.route.eventId,w.route.routeId};c.supported=true;c.lessonEpoch=w.route.epoch;
    c.captureTime=w.route.captureTime;c.maxAge=w.route.provider?3:2.5;c.expiresAt=c.captureTime+c.maxAge;
    if(!evidence(c,w,w.now))return {};
    return c;
}
bool TaskCoach::valid(const Candidate& c,const WorldState& w,double now,const EventLedger& ledger) const noexcept {
    if(!c.lessonStep){const double seconds=lead(w,now);
        // A developing road-user conflict is not routine: it may speak during junction execution.
        const bool conflict=c.origin==7&&priority(c.prompt)>=4;
        if(!c.urgent&&!conflict&&quietHeld(now))return false; // Routine silence persists through evidence loss.
        if(conflict)return true;
        return !isLessonPrompt(c.prompt)&&(!enabled_||c.urgent||seconds<0||seconds>=executionQuietLead);}
    if(!enabled_||state_.phase==TaskPhase::Uncertain||state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch||!evidence(c,w,now))return false;
    if(c.lessonStep==1)return !state_.planningDelivered;
    if(c.lessonStep==3)return scheduling_&&state_.planningDelivered&&!state_.entryDelivered;
    if(!state_.planningDelivered||state_.approachDelivered)return false;
    if(state_.speedReductionObserved&&!(scheduling_&&c.prompt==Prompt::junctionGearDown))return false;
    for(const auto& e:ledger.entries())if(e.used&&e.sourceId==w.sourceId&&e.identity==c.identity&&e.lessonStep==1&&e.lessonEpoch==state_.epoch&&e.acceptedAt>=state_.startedAt&&delivered(e))return true;
    return false;
}
bool TaskCoach::continuing(const Candidate& c,const WorldState& w,double now,const EventLedger& ledger) const noexcept {
    if(!c.lessonStep||!scheduling_)return valid(c,w,now,ledger);
    if(!enabled_||state_.source!=w.sourceId||state_.route!=w.route.routeId||state_.event!=w.route.eventId||state_.epoch!=w.route.epoch||!evidence(c,w,now,true))return false;
    if(c.lessonStep==1)return !state_.planningDelivered;
    if(c.lessonStep==3)return state_.planningDelivered&&!state_.entryDelivered;
    return state_.planningDelivered&&!state_.approachDelivered;
}
std::string TaskCoach::json() const {
    std::ostringstream o;o<<"{\"lesson\":\"junction_preparation\",\"enabled\":"<<(enabled_?"true":"false")<<",\"phase\":\""<<phase(state_.phase)<<"\",\"opportunities\":"<<state_.opportunities
    <<",\"planning_deliveries\":"<<state_.planningCompletions<<",\"approach_deliveries\":"<<state_.approachCompletions<<",\"observed_speed_reductions\":"<<state_.observedReductions
    <<",\"unassessed_outcomes\":"<<state_.unassessedOutcomes<<",\"execution_quiet_holds\":"<<state_.quietHolds<<",\"entry_deliveries\":"<<state_.entryCompletions<<",\"mirror_check\":\"UNOBSERVED\",\"signal\":\"UNOBSERVED\",\"position\":\"UNOBSERVED\",\"controls\":\"UNOBSERVED\",\"driving_outcome\":\"UNASSESSED\"}";return o.str();
}
}
