#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/SituationPlanner.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
namespace ld::coaching {
namespace {
using F=Fragment;
struct Plan {
    std::array<F,6> context{};size_t contexts{};
    std::array<F,8> actions{};size_t actionCount{};
    bool horn=true; // Indian road practice; off elsewhere.
    void ctx(F f) noexcept {if(f==F::none)return;for(size_t i=0;i<contexts;++i)if(context[i]==f)return;if(contexts<context.size())context[contexts++]=f;}
    void act(F f) noexcept {if(f==F::none||(f==F::soundHorn&&!horn))return;for(size_t i=0;i<actionCount;++i)if(actions[i]==f)return;if(actionCount<actions.size())actions[actionCount++]=f;}
    bool has(F f) const noexcept {for(size_t i=0;i<actionCount;++i)if(actions[i]==f)return true;return false;}
    void drop(F f) noexcept {size_t k=0;for(size_t i=0;i<actionCount;++i)if(actions[i]!=f)actions[k++]=actions[i];actionCount=k;}
    void dropCtx(F f) noexcept {size_t k=0;for(size_t i=0;i<contexts;++i)if(context[i]!=f)context[k++]=context[i];contexts=k;}
    bool hasCtx(F f) const noexcept {for(size_t i=0;i<contexts;++i)if(context[i]==f)return true;return false;}
    bool navigation() const noexcept {for(size_t i=0;i<contexts;++i)if(isNavigationFragment(context[i]))return true;return false;}
};
// The spoken distance for Google's distance: the largest round distance not beyond it (never
// more room than there is), and only when close enough to be honest; otherwise none.
F distanceFragment(double d) noexcept {
    if(!std::isfinite(d))return F::none;
    const std::pair<double,F> bands[]={{200,F::in200m},{150,F::in150m},{100,F::in100m},{50,F::in50m}};
    for(const auto& [b,f]:bands)if(b<=d+8)return d-b<=std::max(15.,.3*b)?f:F::none;
    return F::none;
}
// What Google says is there, in an instructor's words. Replaces the generic "junction/turn ahead".
// step 1-2: "In 100 metres, turn right." (+ "Then turn left straight after."); step 3: at the turn.
void navigation(const WorldState& w,int step,double v,Plan& plan) noexcept {
    if(!w.route.provider||!w.route.valid)return;
    const int m=w.route.maneuver;const double d=w.route.distanceM;
    if(step==3) {
        if(m==1||m==2){plan.ctx(m==1?F::leftTurnHere:F::rightTurnHere);}
        else if(m==7){plan.ctx(F::giveWayRight);}
        else if(m==9){plan.ctx(F::pullOver);plan.drop(F::lookBothWays);plan.drop(F::soundHorn);plan.act(F::checkMirrors);}
        return;
    }
    F what=F::none;
    switch(m) {
    case 1:what=F::turnLeft;break;case 2:what=F::turnRight;break;
    case 3:what=w.route.side<0?F::keepLeftRoute:w.route.side>0?F::keepRightRoute:F::none;break;
    case 5:what=F::mergeCarefully;break;case 6:what=F::uTurn;break;
    case 7:what=w.route.roundaboutExit==1?F::roundaboutFirst:w.route.roundaboutExit==2?F::roundaboutSecond:
        w.route.roundaboutExit==3?F::roundaboutThird:w.route.roundaboutExit==4?F::roundaboutFourth:F::roundaboutEnter;break;
    case 9:what=F::destinationAhead;break;
    default:break;
    }
    if(what==F::none)return; // Keep the generic "Junction ahead."
    const F dist=distanceFragment(d);
    if(dist==F::none&&(m==1||m==2))return; // "Left turn ahead." already says it.
    plan.dropCtx(F::junctionAhead);plan.dropCtx(F::leftTurnAhead);plan.dropCtx(F::rightTurnAhead);
    if(dist!=F::none)plan.ctx(dist); else plan.ctx(F::junctionAhead);
    plan.ctx(what);
    // Two turns close together: say the second with the first.
    if(w.route.nextGapM<=80&&(w.route.nextManeuver==1||w.route.nextManeuver==2))plan.ctx(w.route.nextManeuver==1?F::thenLeft:F::thenRight);
    // The signal goes with the direction (keep left/right; a U-turn is to the right here).
    if(step==1&&m==3)plan.act(w.route.side<0?F::signalLeft:F::signalRight);
    if(step==1&&m==6){plan.act(F::signalRight);plan.drop(F::easeOff);}
    // Preparation said late (the learner is already close): slow down with it.
    if(step==1&&d<=routeApproachDistance(v)&&v>routeApproachSpeed)plan.act(F::brakeGently);
}
// Most urgent first; an instructor says what to do NOW before what to do next.
int order(F f) noexcept {
    switch(f){case F::brakeNow:return 0;case F::brakeGently:return 1;case F::stopAndWait:return 2;case F::readyToStop:return 3;
    case F::clutchDown:return 4;case F::holdBrake:return 5;case F::keepLeft:return 6;case F::easeOff:return 7;case F::downSecond:return 8;
    case F::downThird:return 9;case F::coverBrake:return 10;case F::giveSpace:return 11;case F::safeGap:return 12;case F::letThemPass:return 13;
    case F::checkMirrors:return 14;case F::signalLeft:return 15;case F::signalRight:return 16;case F::lookBothWays:return 17;
    case F::soundHorn:return 18;case F::steadyAccelerator:return 19;case F::laneCentre:return 20;default:return 21;}
}
F actorContext(const ActorRisk& r) noexcept {
    const int c=r.category;const bool person=c==0,two=c==1||c==3,truck=c==7,bus=c==5;
    switch(r.kind) {
    case RiskKind::Oncoming:return truck?F::oncomingTruck:bus?F::oncomingBus:two?F::oncomingTwoWheeler:F::oncomingVehicle;
    case RiskKind::Crossing:return person?F::personCrossing:F::vehicleCrossing;
    case RiskKind::Roadside:return r.left?F::personLeft:F::personRight;
    case RiskKind::InPath:return person?F::personAhead:two?F::twoWheelerAhead:truck?F::truckAhead:F::vehicleAhead;
    default:return F::none;
    }
}
// What each recorded instruction asks the learner to do.
bool actions(Prompt p,double v,Plan& plan) noexcept {
    switch(p) {
    case Prompt::junctionPlanLeft:plan.ctx(F::leftTurnAhead);plan.act(F::checkMirrors);plan.act(F::signalLeft);return true;
    case Prompt::junctionPlanRight:plan.ctx(F::rightTurnAhead);plan.act(F::checkMirrors);plan.act(F::signalRight);return true;
    case Prompt::junctionPlan:plan.ctx(F::junctionAhead);plan.act(F::checkMirrors);plan.act(F::easeOff);return true;
    case Prompt::junctionBrakeGearDown:plan.act(F::easeOff);plan.act(F::brakeGently);plan.act(F::downSecond);return true;
    case Prompt::junctionGearDown:plan.act(F::downSecond);plan.act(F::coverBrake);return true;
    case Prompt::junctionEntry:plan.act(F::clutchDown);plan.act(F::lookBothWays);return true;
    case Prompt::personEaseCover:case Prompt::riderEaseCover:plan.act(F::easeOff);plan.act(F::coverBrake);return true;
    case Prompt::vehicleEaseGap:plan.act(F::easeOff);plan.act(F::safeGap);return true;
    case Prompt::brakeReadyStop:plan.act(F::brakeGently);plan.act(F::readyToStop);return true;
    case Prompt::clutchHoldBrake:plan.act(F::clutchDown);plan.act(F::holdBrake);return true;
    case Prompt::gearDownSecond:plan.act(F::downSecond);return true;
    case Prompt::gearDownThird:plan.act(F::downThird);return true;
    case Prompt::downhillThird:plan.ctx(F::downhill);plan.act(F::downThird);return true;
    case Prompt::downhillSecond:plan.ctx(F::steepDownhill);plan.act(F::downSecond);return true;
    case Prompt::uphillSecond:plan.ctx(F::uphill);plan.act(F::downSecond);plan.act(F::steadyAccelerator);return true;
    case Prompt::uphillThird:plan.ctx(F::uphill);plan.act(F::downThird);plan.act(F::steadyAccelerator);return true;
    case Prompt::vehicleCrossingWait:if(v>=0&&v<=15)plan.act(F::stopAndWait);else{plan.act(F::brakeGently);plan.act(F::readyToStop);}plan.act(F::letThemPass);return true;
    case Prompt::personCrossingStop:plan.act(F::stopAndWait);plan.act(F::letThemPass);return true;
    case Prompt::oncomingKeepLeft:plan.act(F::keepLeft);plan.act(F::easeOff);return true;
    case Prompt::personLeftSpace:case Prompt::personRightSpace:plan.act(F::easeOff);plan.act(F::giveSpace);return true;
    case Prompt::driftLeftCentre:plan.ctx(F::driftingLeft);plan.act(F::laneCentre);return true;
    case Prompt::driftRightKeepLeft:plan.ctx(F::driftingRight);plan.act(F::keepLeft);return true;
    case Prompt::bumpAhead:plan.ctx(F::bumpAheadContext);plan.act(F::brakeGently);plan.act(F::clutchDown);plan.act(F::downSecond);return true;
    case Prompt::bendHorn:plan.ctx(F::bendAhead);plan.act(F::soundHorn);plan.act(F::keepLeft);return true;
    default:return false; // Gear-up, stop/move-off, turn exit, awareness: their own recording is clearer.
    }
}
// The standalone instruction equivalent to a secondary road-user intent (ledger record).
Prompt equivalent(const ActorRisk& r) noexcept {
    switch(r.kind){case RiskKind::Oncoming:return Prompt::oncomingKeepLeft;case RiskKind::Roadside:return r.left?Prompt::personLeftSpace:Prompt::personRightSpace;
    case RiskKind::Crossing:return r.category==0?Prompt::personCrossingStop:Prompt::vehicleCrossingWait;
    default:return r.category==0?Prompt::personEaseCover:(r.category==1||r.category==3)?Prompt::riderEaseCover:Prompt::vehicleEaseGap;}
}
}
bool SituationPlanner::compose(Candidate& primary,const WorldState& w,const RiskCoach& risk,const TaskCoach& task,Utterance& out) noexcept {
    const bool speedKnown=w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&cognition::fresh(w.now,w.telemetry.time,.15);
    const double v=speedKnown?w.telemetry.speedKmh:-1;
    Plan plan;plan.horn=w.hornRules;
    // 1. The primary road user (if any) names the situation it is about.
    const ActorRisk* primaryActor=primary.identity.space==IdentitySpace::Optical?risk.riskForOptical(primary.identity.id):nullptr;
    if(primaryActor)plan.ctx(actorContext(*primaryActor));
    if(!actions(primary.prompt,v,plan))return false;
    const size_t ownContexts=plan.contexts,ownActions=plan.actionCount;
    // Indian road practice the recorded sentence does not say: sound the horn when entering
    // a junction and before passing a person or two-wheeler at the road edge.
    if(primary.prompt==Prompt::junctionEntry)plan.act(F::soundHorn);
    // What Google says is there (after the horn, so pulling over at the destination drops it).
    if(primary.lessonStep)navigation(w,primary.lessonStep,v,plan);
    if(primaryActor&&primaryActor->kind==RiskKind::Roadside&&(primaryActor->category==0||primaryActor->category==1||primaryActor->category==3)&&v>5)
        plan.act(F::soundHorn);
    const bool urgent=primary.urgent;
    int priorityMax=priority(primary.prompt);
    primary.alsoCount=0;
    auto absorb=[&](const Identity& id,Prompt p,EventKind kind,int step=0,uint64_t epoch=0){if(primary.alsoCount<primary.also.size()){
        primary.also[primary.alsoCount]=id;primary.alsoPrompt[primary.alsoCount]=p;primary.alsoKind[primary.alsoCount]=kind;
        primary.alsoStep[primary.alsoCount]=step;primary.alsoEpoch[primary.alsoCount]=epoch;++primary.alsoCount;}
        priorityMax=std::max(priorityMax,priority(p));};
    // 2. The slope changes what stopping needs: engine braking downhill, holding uphill.
    const bool pitch=w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    const int band=pitch?w.gradeBand:0;
    const bool braking=plan.has(F::brakeGently)||plan.has(F::brakeNow)||plan.has(F::readyToStop)||plan.has(F::stopAndWait);
    if(band<=-2) {
        plan.ctx(band<=-3?F::steepDownhill:F::downhill);
        if(braking&&speedKnown&&v>8)plan.act(v<20||band<=-3?F::downSecond:F::downThird);
    } else if(band>=2) {
        plan.ctx(F::uphill);
        if(plan.has(F::stopAndWait))plan.act(F::holdBrake);
    }
    if(!urgent) {
        // 3. Other valid instructions true at this moment (gear/slope advice).
        for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i){const auto& c=w.candidates[i];
            if(c.identity==primary.identity||c.urgent||c.lessonStep||c.origin!=6)continue;
            Plan probe;if(!actions(c.prompt,v,probe))continue;
            for(size_t k=0;k<probe.contexts;++k)plan.ctx(probe.context[k]);
            for(size_t k=0;k<probe.actionCount;++k)plan.act(probe.actions[k]);
            absorb(c.identity,c.prompt,c.kind);}
        // 4. The next most important OTHER road user, if it needs something different.
        const ActorRisk* second=nullptr;
        for(size_t i=0;i<risk.riskCount();++i){const auto& r=risk.riskAt(i);
            if(!r.level||(primaryActor&&r.id==primaryActor->id)||actorContext(r)==F::none)continue;
            if(primaryActor&&actorContext(r)==actorContext(*primaryActor))continue;
            if(!second||r.score>second->score)second=&r;}
        if(second) {
            plan.ctx(actorContext(*second));
            Plan probe;actions(equivalent(*second),v,probe);
            for(size_t k=0;k<probe.actionCount;++k)plan.act(probe.actions[k]);
            if(const uint64_t optical=risk.opticalFor(second->id))absorb({IdentitySpace::Optical,optical,0},equivalent(*second),EventKind::Attention);
        }
        // 5. A junction step due now is said in the same breath, never dropped because a
        // road user took the turn to speak ("Left turn ahead. Person on the left...").
        if(!primary.lessonStep&&primary.origin!=5) {
            const Candidate due=task.proposal(w);
            Plan probe;probe.horn=plan.horn;
            if(due.lessonStep&&actions(due.prompt,v,probe)) {
                if(due.prompt==Prompt::junctionEntry)probe.act(F::soundHorn);
                navigation(w,due.lessonStep,v,probe);
                for(size_t k=0;k<probe.contexts;++k)plan.ctx(probe.context[k]);
                for(size_t k=0;k<probe.actionCount;++k)plan.act(probe.actions[k]);
                absorb(due.identity,due.prompt,due.kind,due.lessonStep,due.lessonEpoch);
            }
        }
        // 6. Junction phase: name it when acting near a known junction.
        const auto& j=task.progress();
        // Only once its preparation is due or given: naming the turn earlier and again at
        // its own instruction is repetition, not information.
        const double named=j.prepareLead>0?j.prepareLead+1:25;
        if(j.event&&j.leadSeconds>=0&&j.leadSeconds<=named&&primary.origin!=5&&!plan.navigation())
            plan.ctx(w.route.feature==4?F::leftTurnAhead:w.route.feature==5?F::rightTurnAhead:F::junctionAhead);
    }
    // An urgent instruction has room for one scene word: the reason (what is there), never the
    // slope ("Downhill. Brake now." gave no reason to brake). The slope still sets the gear.
    if(urgent&&plan.contexts>1){plan.dropCtx(F::downhill);plan.dropCtx(F::steepDownhill);plan.dropCtx(F::uphill);}
    // An urgent moment sounds different from routine advice: "Brake now", not "Brake gently".
    if(urgent&&plan.has(F::brakeGently)){plan.drop(F::brakeGently);plan.act(F::brakeNow);}
    // 6. Make the action set consistent: braking replaces easing off; one gear target.
    if(plan.has(F::brakeNow)||plan.has(F::brakeGently)||plan.has(F::stopAndWait)){plan.drop(F::easeOff);plan.drop(F::steadyAccelerator);}
    if(plan.has(F::stopAndWait))plan.drop(F::readyToStop);
    if(plan.has(F::downSecond))plan.drop(F::downThird);
    if(plan.has(F::stopAndWait)||plan.has(F::holdBrake))plan.drop(F::coverBrake);
    std::sort(plan.actions.begin(),plan.actions.begin()+plan.actionCount,[](F a,F b){return order(a)<order(b);});
    // Set the scene like an instructor: slope, then junction/bend, then road users (most important first).
    const auto scene=[](F f){const int i=int(f);return i<=3?0:(i<=7||f==F::bumpAheadContext||isNavigationFragment(f))?1:2;};
    std::stable_sort(plan.context.begin(),plan.context.begin()+plan.contexts,[&](F a,F b){return scene(a)<scene(b);});
    // Google's distance and maneuver come before anything else that would not fit.
    const bool navigating=plan.navigation();
    if(navigating&&plan.contexts>3){plan.dropCtx(F::downhill);plan.dropCtx(F::steepDownhill);plan.dropCtx(F::uphill);}
    // Composition must add something the primary recording does not already say.
    if(plan.contexts==ownContexts&&plan.actionCount<=ownActions&&!primary.alsoCount&&!primaryActor)return false;
    // 7. Keep it speakable: urgent = what + 2 actions; otherwise <= 3 + 3 (< ~6 s).
    const size_t maxContext=urgent?1:navigating?4:3,maxActions=urgent?2:3;
    out={};
    for(size_t i=0;i<std::min(plan.contexts,maxContext);++i)out.fragments[out.count++]=int(plan.context[i]);
    for(size_t i=0;i<std::min(plan.actionCount,maxActions)&&out.count<out.fragments.size();++i)out.fragments[out.count++]=int(plan.actions[i]);
    out.priority=std::clamp(priorityMax,1,6);
    return out.count>=2;
}
}
