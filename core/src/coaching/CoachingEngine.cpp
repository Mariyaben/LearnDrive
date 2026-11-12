#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace ld::coaching {
namespace {
bool fresh(double now,double at,double age=maxVisionAgeSeconds) noexcept {return cognition::fresh(now,at,age);}
bool sameEvent(const Event& e,uint64_t source,const Candidate& c) noexcept {
    return e.used&&e.sourceId==source&&e.identity==c.identity&&e.kind==c.kind&&e.lessonStep==c.lessonStep&&(!c.lessonStep||e.lessonEpoch==c.lessonEpoch);
}
int severity(const Candidate& c) noexcept {return c.critical?3:c.urgent?2:1;}
bool terminal(EventState s) noexcept {return s==EventState::Resolved||s==EventState::Expired;}
// A ticket is addressed by its own prompt or by its composed utterance id.
bool samePrompt(const Candidate& c,int prompt) noexcept {return prompt==int(c.prompt)||(c.utterance&&prompt==c.utterance);}
// The i-th instruction a composed utterance also delivered, as its own ledger event.
Candidate absorbed(const Candidate& p,size_t i) noexcept {
    Candidate also;also.identity=p.also[i];also.prompt=p.alsoPrompt[i];also.kind=p.alsoKind[i];also.captureTime=p.captureTime;also.supported=true;
    also.lessonStep=p.alsoStep[i];also.lessonEpoch=p.alsoEpoch[i];if(also.lessonStep)also.origin=5;return also;
}
}
void ShortMemory::clear() noexcept {samples_={};cursor_=count_=0;last_=-infinity;source_=0;vehicle_.beginSource(0);}
bool ShortMemory::observe(const WorldState& w) noexcept {
    if(source_!=w.sourceId){clear();source_=w.sourceId;vehicle_.beginSource(source_);}
    vehicle_.observe(w.sourceId,w.telemetry.time,w.telemetry.speedKmh,w.telemetry.speedValid,w.now);
    if(!std::isfinite(w.now)||w.now<0||w.now<last_||w.now-last_<.099)return false;
    MemorySample s;s.time=w.now;s.frameSequence=w.frameSequence;
    s.speedValid=fresh(w.now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh)&&w.telemetry.speedKmh>=0;
    s.speedKmh=s.speedValid?w.telemetry.speedKmh:0;
    s.pitchValid=fresh(w.now,w.telemetry.time,.15)&&w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    s.pitchDegrees=s.pitchValid?w.telemetry.pitchDegrees:0;
    s.routeValid=w.route.valid;s.routeEvent=w.route.valid?w.route.eventId:0;
    s.telemetryTime=w.telemetry.time;s.accelerationValid=fresh(w.now,w.telemetry.time,.15)&&std::isfinite(w.telemetry.longitudinalAcceleration);
    s.accelerationMps2=s.accelerationValid?w.telemetry.longitudinalAcceleration:0;
    s.roadCaptureTime=w.roadCaptureTime;s.roadValid=w.roadValid&&fresh(w.now,w.roadCaptureTime);s.roadBend=s.roadValid?w.roadBend:0;
    s.routeCaptureTime=w.route.captureTime;s.routeDistanceM=w.route.valid?w.route.distanceM:infinity;s.routeFeature=w.route.valid?w.route.feature:0;
    // The bounded per-sample store keeps coached identities first (attention /
    // spatial actors and any candidate's actor). Raw optical boxes come later,
    // so a crowded scene cannot evict the actor that speech is about.
    const auto priorityActor=[&](const ActorObservation& a) noexcept {
        if(a.identity.space==IdentitySpace::Attention||a.identity.space==IdentitySpace::Spatial)return true;
        for(size_t k=0;k<std::min(w.candidateCount,w.candidates.size());++k)if(w.candidates[k].identity==a.identity)return true;
        return false;
    };
    size_t stored=0;
    for(int pass=0;pass<2;++pass)for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i) {
        const auto& a=w.actors[i];if(!a.relevant||!fresh(w.now,a.captureTime)||priorityActor(a)!=(pass==0))continue;
        if(stored<s.observations.size())s.observations[stored++]=a;
        ++s.actors;
        if(a.opticalClosing&&std::isfinite(a.tti)&&a.tti>0)s.closestTti=std::min(s.closestTti,a.tti);
    }
    samples_[cursor_]=s;cursor_=(cursor_+1)%samples_.size();count_=std::min(count_+1,samples_.size());last_=w.now;return true;
}
size_t ShortMemory::size(double now) const noexcept {
    size_t n=0;for(size_t i=0;i<count_;++i)if(fresh(now,samples_[i].time,10.000001))++n;return n;
}
SpeedTrend ShortMemory::speedTrend(double now,double since) const noexcept {
    return static_cast<SpeedTrend>(vehicle_.trend(now,since));
}
Event& EventLedger::observe(uint64_t source,const Candidate& c,double now) noexcept {
    for(auto& e:events_)if(sameEvent(e,source,c)) {
        if(e.state==EventState::Resolved&&(c.kind==EventKind::Slope||c.kind==EventKind::Bend||c.kind==EventKind::Shift)) {
            e={next_++,source,c.identity,c.kind,EventState::Active,now,now,-infinity,0,true};e.lastPrompt=c.prompt;e.lastCapture=c.captureTime;e.lastConfidence=c.confidence;return e;
        }
        e.lastSeen=now;e.lastPrompt=c.prompt;e.lastCapture=c.captureTime;e.lastConfidence=c.confidence;
        if(terminal(e.state))e.state=e.acceptedAt>=0?EventState::Accepted:EventState::Active;return e;
    }
    Event* slot=nullptr;
    for(auto& e:events_)if(!e.used){slot=&e;break;}
    if(!slot)for(auto& e:events_)if(terminal(e.state)&&(!slot||e.lastSeen<slot->lastSeen))slot=&e;
    if(!slot)slot=&*std::min_element(events_.begin(),events_.end(),[](const Event& a,const Event& b){return a.lastSeen<b.lastSeen;});
    *slot={next_++,source,c.identity,c.kind,EventState::Active,now,now,-infinity,0,true};slot->lessonStep=c.lessonStep;slot->lessonEpoch=c.lessonEpoch;slot->lastPrompt=c.prompt;slot->lastCapture=c.captureTime;slot->lastConfidence=c.confidence;return *slot;
}
void EventLedger::reconcile(uint64_t source,const WorldState& w) noexcept {
    for(auto& e:events_)if(e.used&&!terminal(e.state)) {
        if(e.sourceId!=source){e.state=EventState::Resolved;continue;}
        bool supported=false;
        for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i)if(sameEvent(e,source,w.candidates[i])&&w.candidates[i].supported){supported=true;break;}
        if(!supported)for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)
            if(w.actors[i].identity==e.identity&&w.actors[i].relevant&&fresh(w.now,w.actors[i].captureTime)){supported=true;break;}
        if(e.identity.space==IdentitySpace::Route&&(!w.route.valid||w.route.eventId!=e.identity.id||w.route.routeId!=e.identity.scope)) {
            e.state=EventState::Resolved;continue;
        }
        if(supported)e.lastSeen=w.now;
        else if(w.now-e.lastSeen>10)e.state=EventState::Expired;
        else if(w.now-e.lastSeen>=2&&((e.kind==EventKind::Slope&&fresh(w.now,w.telemetry.time,.15)&&w.telemetry.pitchValid&&
                    uint64_t(w.gradeBand+5)!=e.identity.id)||
                (e.kind==EventKind::Bend&&w.roadValid&&fresh(w.now,w.roadCaptureTime)&&uint64_t(w.roadBend+2)!=e.identity.id)||
                (e.kind==EventKind::Shift&&w.engineValid&&fresh(w.now,w.engineCaptureTime,1))||
                ((e.kind==EventKind::Attention||e.kind==EventKind::Approach||e.kind==EventKind::Critical)&&w.frameSequence&&fresh(w.now,w.captureTime))))e.state=EventState::Resolved;
    }
}
void EventLedger::sourceChanged() noexcept {for(auto& e:events_)if(e.used&&!terminal(e.state))e.state=EventState::Resolved;}
size_t EventLedger::activeCount() const noexcept {size_t n=0;for(const auto&e:events_)if(e.used&&!terminal(e.state))++n;return n;}
size_t EventLedger::resolvedCount() const noexcept {size_t n=0;for(const auto&e:events_)if(e.used&&terminal(e.state))++n;return n;}
PolicyChoice BalancedPolicy::choose(const PolicyContext& c) const noexcept {
    size_t chosen=0;int best=-1;
    for(size_t i=0;i<std::min(c.world.candidateCount,c.world.candidates.size());++i) {
        const auto& candidate=c.world.candidates[i];if(!candidate.supported||candidate.prompt==Prompt::none)continue;
        const int rank=(candidate.urgent?100:0)+(candidate.lessonStep?3:0)+ld::priority(candidate.prompt);
        if(rank>best){best=rank;chosen=i;}
    }
    if(best<0)return {};
    const auto& selected=c.world.candidates[chosen];
    if(selected.urgent)return {Mode::WARN,chosen};
    const auto& route=c.world.route;
    const bool turn=route.feature==4||route.feature==5;
    const bool imminent=route.valid&&turn&&fresh(c.world.now,route.captureTime,route.provider?3.:2.5)&&fresh(c.world.now,route.gpsTime,2.5)&&
        ((std::isfinite(route.distanceM)&&route.distanceM>=0&&route.distanceM<=20)||(std::isfinite(route.etaSeconds)&&route.etaSeconds>=0&&route.etaSeconds<=4));
    if(imminent&&selected.origin!=5&&!(selected.origin==7&&priority(selected.prompt)>=3))return {Mode::DEFER,chosen,"routine cue deferred during an imminent turn; experimental maneuver window"};
    const auto p=selected.prompt;
    const bool speedOnly=p==Prompt::ease||p==Prompt::brake||p==Prompt::slowSmoothly||p==Prompt::gentleDown||p==Prompt::downhill||p==Prompt::steepDown;
    if(speedOnly&&c.memory.speedTrend(c.world.now)==SpeedTrend::Decreasing)
        return {Mode::SILENCE,chosen,"measured speed decreasing; redundant routine speed cue quiet"};
    return {Mode::COACH,chosen};
}
Prompt SafetyValidator::neutral(Prompt p,bool measured) noexcept {
    if(p==Prompt::camera||p==Prompt::slopeUnavailable)return Prompt::none;
    if(measured)return p;
    switch(p) {
    case Prompt::hill:return Prompt::uphill;
    case Prompt::downhillGear:return Prompt::downhill;
    case Prompt::steepDownGear:return Prompt::steepDown;
    case Prompt::bendLeftGear:return Prompt::bendLeftQuiet;
    case Prompt::bendRightGear:return Prompt::bendRightQuiet;
    case Prompt::closeGear:return Prompt::close;
    case Prompt::closeGearQuiet:return Prompt::closeQuiet;
    case Prompt::imageCloseGear:return Prompt::imageClose;
    case Prompt::shiftUp:case Prompt::shiftDown:case Prompt::maintainSpeed:return Prompt::none;
    default:return p;
    }
}
Block SafetyValidator::validate(const Candidate& c,const WorldState& w,double now,bool started) const noexcept {
    if(!w.active)return Block::Inactive;
    if(!std::isfinite(now)||now<w.now||!w.sourceId||!fresh(now,w.telemetry.time,.15))return Block::Stale;
    if(!c.supported||c.prompt==Prompt::none||int(c.prompt)<0||int(c.prompt)>promptCount||!c.identity.id||c.origin<1||c.origin>7)return Block::Unsupported;
    if((isRiskPrompt(c.prompt)&&c.origin!=7)||(c.origin==7&&!riskMayUse(c.prompt)))return Block::Unsupported;
    if(c.origin==7)return RiskCoach::evidence(c,w,now)&&fresh(now,c.captureTime,c.maxAge)&&now<c.expiresAt?
        (w.telemetry.shock?Block::DriverResponse:Block::None):Block::Unsupported;
    if(isLessonPrompt(c.prompt)&&(c.origin!=5||!c.lessonStep))return Block::Unsupported;
    if(isActorActionPrompt(c.prompt)&&c.origin!=4&&c.origin!=7)return Block::Unsupported;
    if(isControlPrompt(c.prompt)!=(c.origin==6))return Block::Unsupported;
    if(c.origin==6)return ControlCoach::evidence(c,w,now)&&fresh(now,c.captureTime,c.maxAge)&&now<c.expiresAt?
        (w.telemetry.shock||w.telemetry.hardBraking?Block::DriverResponse:Block::None):Block::Unsupported;
    if(!std::isfinite(c.maxAge)||c.maxAge<=0||c.maxAge>(c.origin==5?(w.route.provider?3.:2.5):maxVisionAgeSeconds)||
       !std::isfinite(c.expiresAt)||c.expiresAt<=c.captureTime||!std::isfinite(c.confidence)||c.confidence<0||c.confidence>1||std::isnan(c.tti))return Block::Unsupported;
    const auto space=c.identity.space;
    const bool actorSpace=space==IdentitySpace::Attention||space==IdentitySpace::Optical||space==IdentitySpace::Spatial;
    const bool actorEvent=c.kind==EventKind::Attention||c.kind==EventKind::Approach||c.kind==EventKind::Critical;
    if((c.origin==4&&(space!=IdentitySpace::Attention||!actorEvent))||
       ((c.origin==1||c.origin==2)&&(!(space==IdentitySpace::Optical||space==IdentitySpace::Spatial||(space==IdentitySpace::Tactical&&w.simulation))||!actorEvent))||
       (c.origin==3&&!((space==IdentitySpace::Slope&&c.kind==EventKind::Slope)||(space==IdentitySpace::Bend&&c.kind==EventKind::Bend)||(space==IdentitySpace::Engine&&c.kind==EventKind::Shift)))||
       (c.origin==5&&(space!=IdentitySpace::Route||c.kind!=EventKind::Route)))return Block::Unsupported;
    if(c.urgent&&!actorEvent)return Block::Unsupported;
    if(c.origin==2&&c.prompt!=Prompt::ease&&c.prompt!=Prompt::brake&&c.prompt!=Prompt::emergency)return Block::Unsupported;
    if(c.origin==1&&c.prompt!=Prompt::holdLaneDistance&&c.prompt!=Prompt::keepSafeGap&&c.prompt!=Prompt::slowSmoothly&&
       c.prompt!=Prompt::watchPedestrians&&c.prompt!=Prompt::prepareBend&&c.prompt!=Prompt::observeSign&&
       c.prompt!=Prompt::emergency&&c.prompt!=Prompt::brakeHarder&&c.prompt!=Prompt::emergencyStop)return Block::Unsupported;
    if(c.kind==EventKind::Slope&&ld::slopePromptBand(c.prompt)==0)return Block::Unsupported;
    if(c.kind==EventKind::Shift&&c.prompt!=Prompt::shiftUp&&c.prompt!=Prompt::shiftDown)return Block::Unsupported;
    if(!fresh(now,c.captureTime,c.maxAge)||now>=c.expiresAt)return Block::Stale;
    const bool criticalPrompt=c.prompt==Prompt::emergency||c.prompt==Prompt::nearbyCollision||c.prompt==Prompt::brakeHarder||c.prompt==Prompt::emergencyStop;
    if((c.critical||criticalPrompt)&&!w.allowCritical)return Block::CriticalDisabled;
    if(criticalPrompt&&(!c.critical||!c.urgent))return Block::Unsupported;
    if(c.critical!=(c.kind==EventKind::Critical))return Block::Unsupported;
    if(w.telemetry.shock||(w.telemetry.hardBraking&&!c.critical)||(w.telemetry.braking&&!c.critical&&c.origin!=5))return Block::DriverResponse;
    if(c.origin==5) {
        if(!w.route.valid||c.identity.space!=IdentitySpace::Route||c.identity.id!=w.route.eventId||c.identity.scope!=w.route.routeId||
            !fresh(now,w.route.captureTime,w.route.provider?3.:2.5)||!fresh(now,w.route.gpsTime,2.5))return Block::Unsupported;
        const bool left=c.prompt==Prompt::mapLeftTurn||c.prompt==Prompt::mapLeftTurnNear||c.prompt==Prompt::mapLeftTurn50;
        const bool right=c.prompt==Prompt::mapRightTurn||c.prompt==Prompt::mapRightTurnNear||c.prompt==Prompt::mapRightTurn50;
        if(isLessonPrompt(c.prompt)){if(!TaskCoach::evidence(c,w,now,started))return Block::Unsupported;}
        else if(c.lessonStep||int(c.prompt)<47||int(c.prompt)>55||(w.route.provider&&int(c.prompt)>=52))return Block::Unsupported;
        if((left&&w.route.feature!=4)||(right&&w.route.feature!=5)||
           (c.prompt==Prompt::mapLeftBend&&w.route.feature!=1)||(c.prompt==Prompt::mapRightBend&&w.route.feature!=2)||
           (c.prompt==Prompt::mapJunction&&w.route.feature!=3))return Block::Unsupported;
    }
    if(c.kind==EventKind::Slope&&(!w.telemetry.calibrated||!w.telemetry.pitchValid||!std::isfinite(w.telemetry.pitchDegrees)||
       w.gradeBand!=ld::slopePromptBand(c.prompt)||c.identity.id!=uint64_t(w.gradeBand+5)))return Block::Unsupported;
    if(c.kind==EventKind::Bend&&(!w.roadValid||!fresh(now,w.roadCaptureTime)||w.roadBend==0||uint64_t(w.roadBend+2)!=c.identity.id))return Block::Unsupported;
    if(c.kind==EventKind::Bend) {
        const bool left=c.prompt==Prompt::bendLeft||c.prompt==Prompt::bendLeftQuiet||c.prompt==Prompt::bendLeftGear;
        const bool right=c.prompt==Prompt::bendRight||c.prompt==Prompt::bendRightQuiet||c.prompt==Prompt::bendRightGear;
        if((!left&&!right)||(left&&w.roadBend>=0)||(right&&w.roadBend<=0))return Block::Unsupported;
    }
    const bool mechanical=neutral(c.prompt,false)!=c.prompt;
    if(mechanical||c.kind==EventKind::Shift) {
        if(!c.mechanicalMeasured||!w.engineValid||!fresh(now,w.engineCaptureTime,1)||!w.engineUnderLoad||
           w.engineGear<1||w.engineMaxGear>10||w.engineGear>w.engineMaxGear||
           !std::isfinite(w.engineRpm)||w.engineRpm<500||w.engineRpm>12000||
           !std::isfinite(w.engineLowerRpm)||!std::isfinite(w.engineUpperRpm)||w.engineLowerRpm<500||
           w.engineUpperRpm<=w.engineLowerRpm+500||w.engineUpperRpm>12000)return Block::Unsupported;
        if(c.kind==EventKind::Shift&&c.identity.scope!=uint64_t(w.engineGear))return Block::Unsupported;
        if(c.prompt==Prompt::shiftUp&&(w.engineRpm<=w.engineUpperRpm||w.engineGear>=w.engineMaxGear||
            w.gradeBand<0||(w.roadValid&&fresh(now,w.roadCaptureTime)&&w.roadBend!=0)))return Block::Unsupported;
        if(c.prompt==Prompt::shiftDown&&(w.engineRpm>=w.engineLowerRpm||w.engineGear<=1))return Block::Unsupported;
    }
    if(neutral(c.prompt,c.mechanicalMeasured)!=c.prompt)return Block::Unsupported;
    if(actorSpace) {
        const ActorObservation* selected=nullptr;
        for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)if(w.actors[i].identity==c.identity){selected=&w.actors[i];break;}
        if(!selected||!selected->relevant||!fresh(now,selected->captureTime))return Block::Unsupported;
        if(!std::isfinite(selected->confidence)||selected->confidence<.4||selected->confidence>1||
           !std::isfinite(selected->x)||!std::isfinite(selected->y)||!std::isfinite(selected->w)||!std::isfinite(selected->h)||
           selected->x<0||selected->y<0||selected->w<=0||selected->h<=0||selected->x+selected->w>1.001||selected->y+selected->h>1.001)return Block::Unsupported;
        if(c.origin==4&&c.prompt!=Prompt::pedestrianSpace&&c.prompt!=Prompt::riderSpace&&c.prompt!=Prompt::vehicleSpace&&
           c.prompt!=Prompt::roadUserQuiet&&c.prompt!=Prompt::keepSafeGap&&c.prompt!=Prompt::emergency&&c.prompt!=Prompt::nearbyCollision&&
           !isActorActionPrompt(c.prompt))return Block::Unsupported;
        if(c.prompt==Prompt::personEaseCover&&selected->category!=0)return Block::Unsupported;
        if(c.prompt==Prompt::riderEaseCover&&selected->category!=1&&selected->category!=3)return Block::Unsupported;
        if(isActorActionPrompt(c.prompt)) {
            // Control actions need measured motion: no instruction from unknown speed.
            if(!w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)||w.telemetry.speedKmh<=3)return Block::Unsupported;
            if(c.prompt==Prompt::brakeReadyStop&&(!selected->opticalClosing||(!selected->inPath&&!selected->leadSupported)||w.telemetry.speedKmh<lowAdviceSpeedKmh))return Block::Unsupported;
            if(c.prompt==Prompt::clutchHoldBrake&&w.telemetry.speedKmh>=lowAdviceSpeedKmh+5)return Block::Unsupported;
        }
        if((c.prompt==Prompt::pedestrianSpace||c.prompt==Prompt::watchPedestrians)&&selected->category!=0)return Block::Unsupported;
        if(c.prompt==Prompt::riderSpace&&selected->category!=1&&selected->category!=3)return Block::Unsupported;
        if(c.actorCategory>=0&&c.actorCategory!=selected->category)return Block::Unsupported;
        if(c.captureTime>selected->captureTime+1e-6)return Block::Stale;
        const bool speedAction=c.prompt==Prompt::ease||c.prompt==Prompt::brake||c.prompt==Prompt::slowSmoothly;
        if(speedAction&&(!selected->opticalClosing||(!selected->inPath&&!selected->leadSupported)||!std::isfinite(c.tti)||
            !w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)))return Block::Unsupported;
        if((c.prompt==Prompt::keepSafeGap||c.prompt==Prompt::vehicleSpace)&&!selected->leadSupported)return Block::Unsupported;
        if(c.urgent&&(!selected->opticalClosing||!std::isfinite(selected->tti)||selected->tti<=0||selected->confidence<.65))return Block::Unsupported;
        const bool attentionWarning=c.origin==4&&c.evidenceKind>=2&&
            (c.prompt==Prompt::pedestrianSpace||c.prompt==Prompt::riderSpace||c.prompt==Prompt::roadUserQuiet||c.prompt==Prompt::vehicleSpace||c.prompt==Prompt::nearbyCollision);
        if(c.urgent&&!selected->inPath&&!selected->leadSupported&&!attentionWarning)return Block::Unsupported;
        if(c.urgent&&selected->tti-(now-selected->captureTime)>(c.critical?2.:5.))return Block::Unsupported;
    }
    if(c.urgent&&(!std::isfinite(c.tti)||c.tti<=0||c.tti-(now-c.captureTime)>5||!std::isfinite(c.confidence)||c.confidence<.65||c.confidence>1))return Block::Unsupported;
    return Block::None;
}
bool CoachingEngine::configure(int p,bool voice,uint64_t revision) noexcept {
    if(p<0||p>2||!revision||revision<revision_)return false;
    if(configured_&&revision==revision_)return p==int(profile_)&&voice==voice_;
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,lastNow_);
    profile_=static_cast<Profile>(p);voice_=voice;revision_=revision;configured_=true;
    pending_={};accepted_={};deferred_={};decision_={};return true;
}
void CoachingEngine::beginSource(uint64_t source) noexcept {
    if(source==source_)return;
    invalidateEvidence();source_=source;ledger_.sourceChanged();
    synchronizer_.beginSource(source);tracks_.beginSource(source);
    interpreter_.reset(source);lastWorld_={};
    // Pacing is deliberately retained through camera/session/settings changes.
}
void CoachingEngine::invalidateEvidence() noexcept {
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,lastNow_);
    // (The turn lesson first takes in what was just said, so the same Google turn is not restarted.)
    task_.settle(ledger_,lastNow_);
    pending_={};accepted_={};deferred_={};decision_={};memory_.clear();interpreter_.reset(source_);task_.sourceChanged();drive_.sourceChanged();control_.reset();risk_.reset();
}
double CoachingEngine::interval() const noexcept {return profile_==Profile::Minimal?20:profile_==Profile::Guided?8:12;}
Decision CoachingEngine::decide(const WorldState& input,uint64_t sequence) noexcept {
    auto w=input;
    decision_={};pending_={};policyReason_=nullptr;
    if(!configured_||!std::isfinite(w.now)||w.now<0||w.now<lastNow_||w.sourceId!=source_) {decision_.blocked=Block::Stale;return decision_;}
    lastNow_=w.now;
    const auto telemetry=synchronizer_.accept(ObservationChannel::Telemetry,source_,0,w.telemetry.time,w.now,.15);
    if(telemetry==ObservationResult::OutOfOrder||telemetry==ObservationResult::Invalid)w.telemetry.time=-infinity;
    const auto camera=synchronizer_.accept(ObservationChannel::Camera,source_,w.frameSequence,w.captureTime,w.now);
    if(camera==ObservationResult::OutOfOrder||camera==ObservationResult::Invalid) {
        // Independent telemetry/navigation can proceed after a visual-clock
        // rejection. A bad frame cannot support an actor or bend candidate.
        if(std::isfinite(w.captureTime)){w.actorCount=0;w.roadValid=false;}w.captureTime=-infinity;
    }
    if(w.route.valid)synchronizer_.accept(ObservationChannel::Navigation,source_,0,w.route.captureTime,w.now,w.route.provider?3:2.5);
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i){auto&a=w.actors[i];
        const auto result=tracks_.observe({{int(a.identity.space),source_,a.identity.id},a.captureTime,a.confidence,a.category,a.x,a.y,a.w,a.h,a.relevant},w.now);
        if(result==ObservationResult::Invalid||result==ObservationResult::OutOfOrder||result==ObservationResult::Stale)a.relevant=false;
    }
    tracks_.expire(w.now);memory_.observe(w);ledger_.reconcile(source_,w);
    task_.observe(w,memory_,ledger_);
    if(task_.enabled()) {
        size_t kept=0;for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i)
            if(w.candidates[i].origin!=5||w.route.feature<3||w.route.feature>5)w.candidates[kept++]=w.candidates[i];
        w.candidateCount=kept;const auto lesson=task_.proposal(w);
        if(lesson.prompt!=Prompt::none){
            if(kept<w.candidates.size())w.candidates[w.candidateCount++]=lesson;
            else for(size_t i=kept;i>0;--i)if(!w.candidates[i-1].urgent){w.candidates[i-1]=lesson;break;}
        }
    }
    if(scheduling_&&drive_.enabled()) {
        control_.observe(w,memory_,task_,int(profile_));
        std::array<Candidate,4> extra{};const size_t n=control_.proposals(w,extra.data(),extra.size());
        bool slopeInstruction=false;
        for(size_t i=0;i<n;++i)slopeInstruction|=extra[i].prompt==Prompt::downhillSecond||extra[i].prompt==Prompt::downhillThird||
            extra[i].prompt==Prompt::uphillSecond||extra[i].prompt==Prompt::uphillThird||isMapHillPrompt(extra[i].prompt);
        // With the route's elevation, hills come from the map: the phone's tilt cues step aside.
        if(slopeInstruction||w.mapGradeKnown){size_t kept=0;for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i)
            if(w.candidates[i].kind!=EventKind::Slope)w.candidates[kept++]=w.candidates[i];w.candidateCount=kept;}
        for(size_t i=0;i<n&&w.candidateCount<w.candidates.size();++i)w.candidates[w.candidateCount++]=extra[i];
        // Road users: the risk coach replaces object-presence announcements. Keep
        // only independently validated urgent warnings from the older producers.
        risk_.observe(w,memory_,tracks_,ledger_,source_,int(profile_),&learner_);
        {size_t kept=0;for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i){const auto& c=w.candidates[i];
            // Camera road users (origin 4) are reasoned about only by the risk coach.
            if(c.origin==4||((c.origin==1||c.origin==2)&&!c.urgent))continue;w.candidates[kept++]=c;}w.candidateCount=kept;}
        Candidate riskCue;if(risk_.proposals(w,&riskCue,1)) {
            // The risk coach has decided about the most dangerous road user; older
            // camera warnings for the same scene would only duplicate it.
            if(riskCue.urgent||riskCue.evidenceKind/2>=2){size_t kept=0;for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i)
                if(w.candidates[i].origin!=4)w.candidates[kept++]=w.candidates[i];w.candidateCount=kept;}
            if(w.candidateCount<w.candidates.size())w.candidates[w.candidateCount++]=riskCue;
            else w.candidates[w.candidateCount-1]=riskCue;
        }
        // Road-user announcements become instructions for the measured speed and
        // evidence. Unknown speed keeps the original wording (no invented action).
        for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i){auto& c=w.candidates[i];
            if(c.origin!=4||c.urgent||(c.prompt!=Prompt::pedestrianSpace&&c.prompt!=Prompt::riderSpace&&c.prompt!=Prompt::vehicleSpace&&c.prompt!=Prompt::roadUserQuiet))continue;
            if(!w.telemetry.speedValid||!std::isfinite(w.telemetry.speedKmh)||w.telemetry.speedKmh<=3)continue;
            const ActorObservation* a=nullptr;for(size_t k=0;k<std::min(w.actorCount,w.actors.size());++k)if(w.actors[k].identity==c.identity){a=&w.actors[k];break;}
            if(!a)continue;const double v=w.telemetry.speedKmh;
            if(a->opticalClosing&&(a->inPath||a->leadSupported)&&v>=lowAdviceSpeedKmh)c.prompt=Prompt::brakeReadyStop;
            else if(v<lowAdviceSpeedKmh+5&&a->relevant&&a->category==0)c.prompt=Prompt::clutchHoldBrake;
            else if(a->category==0)c.prompt=Prompt::personEaseCover;
            else if(a->category==1||a->category==3)c.prompt=Prompt::riderEaseCover;
            else c.prompt=Prompt::vehicleEaseGap;
        }
    }
    interpreter_.observe(w,memory_,ledger_,revision_);lastWorld_=w;
    drive_.observe(w,task_,accepted_);
    if(deferred_.valid&&validator_.validate(deferred_.candidate,w,w.now)!=Block::None){deferred_={};++stats_.expired;}
    WorldState safe=w;safe.candidateCount=0;Block rejected=Block::None;bool duplicate=false;
    for(size_t i=0;i<std::min(w.candidateCount,w.candidates.size());++i) {
        auto candidate=w.candidates[i];candidate.prompt=SafetyValidator::neutral(candidate.prompt,candidate.mechanicalMeasured);
        auto block=validator_.validate(candidate,w,w.now);
        if(block==Block::None&&!task_.valid(candidate,w,w.now,ledger_))block=Block::Unsupported;
        if(block!=Block::None){rejected=block;continue;}
        auto& event=ledger_.observe(source_,candidate,w.now);
        event.providerContent=event.providerContent||(candidate.origin==5&&w.route.provider);
        const bool worsened=(candidate.urgent&&std::isfinite(candidate.tti)&&std::isfinite(event.acceptedTti)&&
            candidate.tti<=event.acceptedTti-std::max(.75,event.acceptedTti*.25))||
            // One escalation per encounter: "ease off" may become "brake, be ready to stop".
            (scheduling_&&drive_.enabled()&&priority(candidate.prompt)>priority(event.acceptedPrompt)&&event.acceptances<2);
        if(event.acceptedAt>=0&&!event.retryable&&severity(candidate)<=event.acceptedSeverity&&
           (!worsened||w.now-event.acceptedAt<2)) {duplicate=true;continue;}
        safe.candidates[safe.candidateCount++]=candidate;
    }
    const PolicyContext context{safe,memory_,&interpreter_.scene(),&ledger_};
    const auto choice=drive_.enabled()?drive_.choose(context):policy_->choose(context);
    if(int(choice.mode)<0||int(choice.mode)>3){decision_.blocked=Block::Unsupported;return decision_;}
    policyReason_=choice.reason;
    if(choice.mode==Mode::SILENCE||choice.candidate>=safe.candidateCount) {
        decision_.blocked=duplicate?Block::Duplicate:rejected;
        if(duplicate)++stats_.duplicates;else if(rejected!=Block::None)++stats_.selectionRejected;
        return decision_;
    }
    const auto& c=safe.candidates[choice.candidate];auto& event=ledger_.observe(source_,c,w.now);
    Ticket ticket{c,sequence,source_,revision_,event.number,w.now,-infinity,true,false,false,false};
    decision_.mode=choice.mode;decision_.ticket=ticket;
    if((choice.mode==Mode::WARN&&!c.urgent)||(c.urgent&&choice.mode!=Mode::WARN)){decision_.mode=Mode::SILENCE;decision_.blocked=Block::Unsupported;return decision_;}
    const bool ordinary=choice.mode!=Mode::WARN;
    const bool scheduled=scheduling_&&drive_.enabled();
    if(scheduled&&choice.mode==Mode::COACH&&!c.lessonStep&&!c.urgent&&!DriveCoach::developingConflict(c,safe)&&!(c.origin==7&&(priority(c.prompt)>=3||c.evidenceKind%2))&&task_.reserves(interval())) {
        // Speaking this routine cue now would consume the speech interval that a
        // known, higher-value task step needs shortly. It stays a candidate.
        decision_={};decision_.blocked=Block::Cooldown;policyReason_="RESERVED_FOR_UPCOMING_TASK";++stats_.reservedForTask;return decision_;
    }
    // An episode's own follow-up step is spaced from its previous step, not by
    // the unrelated-routine interval, so the profile cannot silently remove it.
    // Lesson pacing follows the task schedule (act-by steps are seconds apart).
    const bool followUp=scheduling_&&c.lessonStep>1&&lastOrdinaryLesson_==c.identity&&lastOrdinaryLessonEpoch_==c.lessonEpoch;
    double spacing=followUp?std::min(interval(),actByStepSpacing):interval();
    // The post-turn instruction belongs to the junction just taught.
    if(scheduled&&c.prompt==Prompt::turnExit&&lastOrdinaryLesson_.space==IdentitySpace::Route)spacing=std::min(interval(),4.);
    // The same encounter getting worse ("ease off" -> "brake, be ready to stop") is not paced as new chatter.
    if(scheduled)for(const auto& e:ledger_.entries())if(e.used&&e.sourceId==source_&&e.identity==c.identity&&e.acceptedAt>=0&&
        priority(c.prompt)>priority(e.acceptedPrompt)){spacing=std::min(spacing,2.);break;}
    // A risk instruction about a developing conflict is not paced like routine chatter.
    if(scheduled&&c.origin==7)spacing=std::min(spacing,priority(c.prompt)>=4?3.:priority(c.prompt)>=3?6.:interval());
    // ...and braking for a person or two-wheeler in the path waits only for the key words of
    // what was just said about someone else (Nov 8, 2:42).
    if(scheduled&&c.origin==7&&c.pathFirst)spacing=std::min(spacing,1.5);
    // Consecutive vehicle-control instructions (e.g. second then third gear) are one sequence.
    if(scheduled&&c.origin==6&&lastOrdinaryOrigin_==6)spacing=std::min(interval(),4.);
    if(scheduling_&&c.lessonStep&&!followUp) {
        // Deadline-aware pacing: if waiting out the full routine interval would
        // leave this step less than a useful part of its window, the minimum
        // episode spacing applies instead. Unrelated routine cues keep the interval.
        const double lead=task_.progress().leadSeconds,close=c.lessonStep==1?actByLateLimit:c.lessonStep==2?actByApproachClose:actByEntryClose;
        if(lead>=0&&lead-std::max(0.,interval()-(w.now-lastOrdinary_))<close+minimumUsefulWindow)spacing=std::min(interval(),actByStepSpacing);
    }
    if(choice.mode==Mode::DEFER||(ordinary&&w.now-lastOrdinary_<spacing)||(!ordinary&&w.now-lastWarning_<.25)) {
        decision_.mode=Mode::DEFER;decision_.blocked=Block::Cooldown;event.state=EventState::Deferred;
        if(!deferred_.valid||deferred_.eventNumber!=ticket.eventNumber)++stats_.deferred;
        deferred_=ticket;return decision_;
    }
    // A single fresh slot is chosen anew every tick, never a FIFO of old cues.
    if(scheduled) {
        // One picture of the moment -> one instruction (see SituationPlanner).
        Utterance u;
        if(SituationPlanner::compose(ticket.candidate,safe,risk_,task_,control_.bands(),u)) {
            // The same sentence again within 4 s (e.g. a second similar road user) adds nothing.
            if(w.now-lastSpokenAt_<4&&u.count==lastSpoken_.count&&u.priority<=lastSpoken_.priority&&
               std::equal(u.fragments.begin(),u.fragments.begin()+u.count,lastSpoken_.fragments.begin())) {
                decision_={};decision_.blocked=Block::Duplicate;policyReason_="SAME_INSTRUCTION_JUST_GIVEN";++stats_.duplicates;return decision_;
            }
            const size_t slot=utteranceSlot_++%utterances_.size();
            u.id=compositeBase+u.priority*100+int(slot);utterances_[slot]=u;ticket.candidate.utterance=u.id;
        } else {ticket.candidate.utterance=0;ticket.candidate.alsoCount=0;}
        decision_.ticket=ticket;
    }
    deferred_={};pending_=ticket;decision_.eligible=true;event.state=EventState::Active;drive_.selected(c,w.now);return decision_;
}
bool CoachingEngine::ticketValid(const Ticket& t,uint64_t sequence,int prompt,const WorldState& w,double now) const noexcept {
    return t.valid&&sequence==t.sequence&&samePrompt(t.candidate,prompt)&&t.sourceId==source_&&w.sourceId==source_&&
        t.revision==revision_&&now>=t.decidedAt&&validator_.validate(t.candidate,w,now)==Block::None&&task_.valid(t.candidate,w,now,ledger_)&&drive_.valid(t.candidate,w,now);
}
bool CoachingEngine::pending(uint64_t sequence,int prompt,const WorldState& w,double now) const noexcept {return ticketValid(pending_,sequence,prompt,w,now)&&!pending_.accepted;}
bool CoachingEngine::accept(uint64_t sequence,int prompt,const WorldState& w,double now,bool submitted) noexcept {
    if(!pending(sequence,prompt,w,now)||(submitted&&(!voice_||!w.audioReady)))return false;
    // Voice-on failure is not intentional visual-only delivery. Keep the event
    // eligible for a later fresh attempt and do not consume its routine interval.
    if(voice_&&!submitted){++stats_.submissionFailed;deliveryEvent(sequence,prompt,DeliveryStage::Failed,now);return false;}
    if(accepted_.valid&&accepted_.stage!=DeliveryStage::Completed&&accepted_.stage!=DeliveryStage::VisualOnly)
        deliveryEvent(accepted_.sequence,int(accepted_.candidate.prompt),DeliveryStage::Interrupted,now);
    auto& event=ledger_.observe(source_,pending_.candidate,now);
    event.state=EventState::Accepted;event.acceptedAt=now;event.acceptedSeverity=severity(pending_.candidate);
    event.acceptedPrompt=pending_.candidate.prompt;++event.acceptances;
    event.acceptedTti=pending_.candidate.tti;
    event.voiceSubmitted=submitted;event.visualOnly=!voice_;event.startedAt=-infinity;
    event.deliveryStage=submitted?DeliveryStage::Submitted:DeliveryStage::VisualOnly;event.retryable=false;
    if(event.visualOnly)drive_.delivered(pending_.candidate,now);
    accepted_=pending_;accepted_.accepted=true;accepted_.acceptedAt=now;accepted_.voiceSubmitted=submitted;
    accepted_.stage=event.deliveryStage;accepted_.priorOrdinary=lastOrdinary_;accepted_.priorWarning=lastWarning_;
    if(pending_.candidate.urgent)lastWarning_=now;
    else {lastOrdinary_=now;lastOrdinaryLesson_=pending_.candidate.lessonStep?pending_.candidate.identity:Identity{};lastOrdinaryLessonEpoch_=pending_.candidate.lessonEpoch;
        lastOrdinaryOrigin_=pending_.candidate.origin;}
    if(const auto* u=utterance(pending_.candidate.utterance)){lastSpoken_=*u;lastSpokenAt_=now;}
    // Instructions absorbed into a composed utterance were delivered too.
    for(size_t i=0;i<std::min(pending_.candidate.alsoCount,pending_.candidate.also.size());++i) {
        Candidate also=absorbed(pending_.candidate,i);
        auto& extra=ledger_.observe(source_,also,now);extra.state=EventState::Accepted;extra.acceptedAt=now;extra.acceptedSeverity=1;
        extra.acceptedPrompt=also.prompt;++extra.acceptances;extra.voiceSubmitted=submitted;extra.visualOnly=!voice_;
        extra.deliveryStage=submitted?DeliveryStage::Submitted:DeliveryStage::VisualOnly;extra.retryable=false;
    }
    ++stats_.logical;if(submitted)++stats_.voiceSubmitted;pending_={};return true;
}
bool CoachingEngine::playbackValid(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) const noexcept {
    return revision==revision_&&voice_&&w.audioReady&&accepted_.accepted&&!accepted_.onset&&accepted_.stage==DeliveryStage::Submitted&&accepted_.voiceSubmitted&&ticketValid(accepted_,sequence,prompt,w,now);
}
bool CoachingEngine::playbackStarted(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) noexcept {
    if(!playbackValid(sequence,prompt,w,now,revision)||accepted_.onset){++stats_.onsetRejected;return false;}
    accepted_.onset=true;++stats_.playbackOnsets;
    for(const auto& entry:ledger_.entries())if(entry.used&&entry.number==accepted_.eventNumber) {
        auto& event=ledger_.observe(source_,accepted_.candidate,now);event.startedAt=now;event.deliveryStage=DeliveryStage::Started;accepted_.stage=DeliveryStage::Started;break;
    }
    return true;
}
bool CoachingEngine::continuationValid(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) const noexcept {
    const char* r=continuationReason(sequence,prompt,w,now,revision);return r[0]=='O'&&r[1]=='K'&&!r[2];
}
const char* CoachingEngine::continuationReason(uint64_t sequence,int prompt,const WorldState& w,double now,uint64_t revision) const noexcept {
    const auto& t=accepted_;
    if(!t.valid||!t.onset||t.stage!=DeliveryStage::Started||t.sequence!=sequence||!samePrompt(t.candidate,prompt)||
        revision!=revision_||t.revision!=revision_||t.sourceId!=source_||w.sourceId!=source_)return "TICKET_OR_SOURCE";
    if(scheduling_) {
        // Started speech finishes its sentence, as an instructor does. Braking (often the very
        // response asked for), a jolt, a late camera frame or reaching the junction mid-sentence
        // never cut it off: on the Nov 6 drive those cut nearly every instruction mid-word, and
        // the retries repeated them. It stops only when the situation changed: perception or the
        // session stopped, the road user is gone, Google replaced the route, the junction
        // turn began (routine speech), the car stopped, or the reading itself was lost. The
        // player then stops non-urgent speech at its next pause, never mid-word.
        const auto& c=t.candidate;
        if(!w.active)return "SOURCE_INACTIVE";
        if(c.origin==7&&c.urgent) {
            double seen=-infinity;
            for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)if(w.actors[i].identity==c.identity)seen=std::max(seen,w.actors[i].captureTime);
            for(const auto& sample:memory_.samples())for(const auto& a:sample.observations)if(a.identity==c.identity)seen=std::max(seen,a.captureTime);
            if(c.identity.space==IdentitySpace::Optical)seen=std::max(seen,risk_.lastSeen(c.identity.id)); // Same road user under a new detector id.
            // A warning sentence that has started finishes about a road user seen in the time it takes
            // to say. With the slope or the turn named first that is up to about 5.5 s; at 3.5 s the
            // only thing ever cut was the last word of a sentence about a vehicle that had just passed
            // ("...Ease off the accel-": five times in 4.7 minutes of the Nov 6 replay).
            // ...unless a turn step is waiting to be said ("brake, change down", or the turn itself)
            // and the road user has been gone for 1.5 s: its few seconds are worth more than the rest
            // of a sentence about a vehicle that has passed (Nov 5, 4:00: "Oncoming vehicle. Keep
            // left. Ease off the accelerator." was said to its end and "Left turn here" never came).
            const Candidate step=task_.proposal(w);
            const bool stepWaiting=step.prompt!=Prompt::none&&step.lessonStep>=2;
            if(!std::isfinite(seen)||now-seen>(stepWaiting?1.5:6.))return "ACTOR_NOT_OBSERVED";
            return "OK";
        }
        if(c.kind==EventKind::Slope&&!w.telemetry.pitchValid)return "EVIDENCE_LOST"; // Phone knocked in its mount.
        if(c.origin==5&&c.lessonStep) {
            // A junction step stops if Google replaced the route or guidance ended. Planning and
            // approach (mirrors, signal, brake, gear) end at their next pause once the turn itself
            // is under way; the entry step is about the junction mouth and finishes.
            if(!w.route.routeId||c.identity.scope!=w.route.routeId)return "TASK_CONTEXT";
            const double lead=TaskCoach::leadSeconds(w,now);
            if(c.lessonStep!=3&&lead>=0&&lead<actByEntryClose)return "TURN_UNDER_WAY";
        } else if(!task_.continuing(c,w,now,ledger_))return "TASK_CONTEXT";
        const bool stopped=c.lessonStep&&w.telemetry.speedValid&&w.telemetry.speedKmh<=3;
        if(!stopped&&!drive_.valid(c,w,now,true))return "DRIVE_CONTEXT";
        return "OK";
    }
    // 0.7.1 comparison mode: once speaking, require fresh evidence for the SAME event to
    // continue, never a replacement actor.
    auto c=t.candidate;auto currentWorld=w;
    if(c.identity.space==IdentitySpace::Route)c.captureTime=w.route.captureTime;
    else if(c.kind==EventKind::Slope)c.captureTime=w.telemetry.time;
    else if(c.kind==EventKind::Bend)c.captureTime=w.roadCaptureTime;
    else if(c.kind==EventKind::Shift)c.captureTime=w.engineCaptureTime;
    else if(scheduling_&&c.origin==7&&c.urgent) {
        // An urgent risk instruction finishes if its road user was seen in the last
        // second (detector flicker), perception is live and nothing else changed.
        if(!w.active)return "SOURCE_INACTIVE";
        if(w.telemetry.shock)return "MEASURED_BRAKING_OR_SHOCK";
        if(!fresh(now,w.captureTime))return "EVIDENCE_STALE";
        double seen=-infinity;
        for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)if(w.actors[i].identity==c.identity)seen=std::max(seen,w.actors[i].captureTime);
        for(const auto& sample:memory_.samples())for(const auto& a:sample.observations)if(a.identity==c.identity)seen=std::max(seen,a.captureTime);
        if(c.identity.space==IdentitySpace::Optical)seen=std::max(seen,risk_.lastSeen(c.identity.id)); // Same road user under a new detector id.
        // Finish the warning sentence about a road user seen in the time it takes to say (see
        // above); a crossing vehicle often leaves the view within a second.
        if(!std::isfinite(seen)||now-seen>6)return "ACTOR_NOT_OBSERVED";
        return "OK";
    }
    else if(scheduling_&&!c.urgent) {
        // A routine road-user advisory validated at onset (<=350 ms evidence)
        // finishes its sentence. Truncation mid-word distracts more than a
        // slightly outdated "give space", and interrupted cues are retried,
        // which produced repeats. Perception loss, braking/shock, task quiet,
        // drive context, source/settings changes and urgent preemption still stop it.
        if(!w.active)return "SOURCE_INACTIVE";
        if(w.telemetry.shock||w.telemetry.braking||w.telemetry.hardBraking)return "MEASURED_BRAKING_OR_SHOCK";
        if(!task_.continuing(c,w,now,ledger_))return "TASK_CONTEXT";
        if(!drive_.valid(c,w,now))return "DRIVE_CONTEXT";
        return "OK";
    }
    else {
        const ActorObservation* current=nullptr;
        for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i)if(w.actors[i].identity==c.identity){current=&w.actors[i];break;}
        if(!current){
            // A brief detector dropout may retain the last independently
            // observed actor only within its original 350 ms evidence budget.
            for(const auto& sample:memory_.samples())for(const auto& a:sample.observations)
                if(a.identity==c.identity&&a.relevant&&fresh(now,a.captureTime)&&(!current||a.captureTime>current->captureTime))current=&a;
            if(!current||currentWorld.actorCount==currentWorld.actors.size())return "ACTOR_NOT_OBSERVED";
            currentWorld.actors[currentWorld.actorCount++]=*current;
        }
        c.captureTime=current->captureTime;c.confidence=current->confidence;c.tti=current->tti;
    }
    c.expiresAt=c.captureTime+c.maxAge;
    switch(validator_.validate(c,currentWorld,now,true)){
    case Block::None:break;case Block::Stale:return "EVIDENCE_STALE";case Block::Inactive:return "SOURCE_INACTIVE";
    case Block::DriverResponse:return "MEASURED_BRAKING_OR_SHOCK";case Block::CriticalDisabled:return "CRITICAL_DISABLED";default:return "ACTOR_OR_CONTEXT_UNSUPPORTED";}
    if(!task_.continuing(c,currentWorld,now,ledger_))return "TASK_CONTEXT";
    // A started junction step finishes when the learner stops at that junction.
    const bool stoppedAtJunction=scheduling_&&c.lessonStep&&currentWorld.telemetry.speedValid&&currentWorld.telemetry.speedKmh<=3;
    if(!stoppedAtJunction&&!drive_.valid(c,currentWorld,now))return "DRIVE_CONTEXT";
    return "OK";
}
bool CoachingEngine::deliveryEvent(uint64_t sequence,int prompt,DeliveryStage stage,double now) noexcept {
    Ticket* ticket=nullptr;
    if(accepted_.valid&&accepted_.sequence==sequence&&samePrompt(accepted_.candidate,prompt))ticket=&accepted_;
    else if(pending_.valid&&pending_.sequence==sequence&&samePrompt(pending_.candidate,prompt))ticket=&pending_;
    if(!ticket||!std::isfinite(now)||now<ticket->decidedAt||
       (stage!=DeliveryStage::Scheduled&&stage!=DeliveryStage::Interrupted&&stage!=DeliveryStage::Expired&&stage!=DeliveryStage::Failed&&stage!=DeliveryStage::Completed))return false;
    if(ticket->stage==DeliveryStage::Completed||ticket->stage==DeliveryStage::VisualOnly)return stage==ticket->stage;
    if(ticket->stage==DeliveryStage::Interrupted||ticket->stage==DeliveryStage::Expired||ticket->stage==DeliveryStage::Failed)return stage==ticket->stage;
    if(stage==DeliveryStage::Completed&&!ticket->onset)return false;
    ticket->stage=stage;
    auto& event=ledger_.observe(source_,ticket->candidate,now);event.deliveryStage=stage;
    if(stage!=DeliveryStage::Scheduled)event.endedAt=now;
    if(stage==DeliveryStage::Completed)drive_.delivered(ticket->candidate,now);
    const bool failed=stage==DeliveryStage::Interrupted||stage==DeliveryStage::Expired||stage==DeliveryStage::Failed;
    // What a composed sentence also said shares its fate: finished, or to be said again.
    if(ticket->accepted)for(size_t i=0;i<std::min(ticket->candidate.alsoCount,ticket->candidate.also.size());++i){
        auto& extra=ledger_.observe(source_,absorbed(ticket->candidate,i),now);extra.deliveryStage=stage;if(failed)extra.retryable=true;}
    if(failed) {
        event.retryable=true;
        if(ticket->accepted) {
            if(ticket->candidate.urgent&&lastWarning_==ticket->acceptedAt)lastWarning_=ticket->priorWarning;
            else if(!ticket->candidate.urgent&&lastOrdinary_==ticket->acceptedAt){lastOrdinary_=ticket->priorOrdinary;lastOrdinaryLesson_={};} // Conservative: full interval.
        }
    }
    return true;
}
size_t CoachingEngine::diagnostics(double now,double* out,size_t capacity) const noexcept {
    if(!out||capacity<20)return 0;
    const auto& t=decision_.ticket;const double capture=t.valid?t.candidate.captureTime:-1;
    const double v[]={double(configured_),double(profile_),double(voice_),double(revision_),double(decision_.mode),double(decision_.blocked),double(t.eventNumber),capture,
        capture>=0&&std::isfinite(now)?now-capture:-1,double(memory_.size(now)),double(ledger_.activeCount()),double(ledger_.resolvedCount()),double(stats_.logical),
        double(stats_.voiceSubmitted),double(stats_.playbackOnsets),double(stats_.onsetRejected),double(stats_.duplicates),double(stats_.deferred),double(memory_.speedTrend(now)),interval()};
    std::copy_n(v,20,out);return 20;
}
std::string CoachingEngine::diagnosticsJson(double now) const {
    double v[20];diagnostics(now,v,20);std::ostringstream out;out.imbue(std::locale::classic());
    const char* names[]={"enabled","profile","voice","revision","mode","block","event_id","capture_time","evidence_age","history_samples","active_events","resolved_events",
        "logical_accepted","voice_submitted","playback_onsets","onset_rejected","duplicates","deferred","speed_trend","ordinary_interval_s"};
    out<<"{";for(int i=0;i<20;++i){if(i)out<<',';out<<'"'<<names[i]<<"\":";if(std::isfinite(v[i]))out<<v[i];else out<<"null";}
    out<<",\"submission_failed\":"<<stats_.submissionFailed<<",\"reserved_for_task\":"<<stats_.reservedForTask<<",\"task_scheduling\":"<<(scheduling_?"true":"false")<<",\"delivery_stage\":"<<int(accepted_.valid?accepted_.stage:pending_.stage)<<",\"reason_code\":\""<<reasonCode()<<"\",\"heard\":false,\"timing_status\":\"experimental\",\"speed_trend_is_pedal_state\":false,\"task\":"<<task_.json()<<",\"risk\":"<<risk_.json()<<'}';return out.str();
}
const char* CoachingEngine::reasonCode() const noexcept {
    if(policyReason_&&*policyReason_>='A'&&*policyReason_<='Z')return policyReason_;
    switch(decision_.blocked){case Block::Inactive:return "SOURCE_INACTIVE";case Block::Stale:return "STALE_EVIDENCE";case Block::Unsupported:return "UNSUPPORTED_ACTION";case Block::Cooldown:return "PACING_OR_WORKLOAD";case Block::Duplicate:return "ALREADY_ACCEPTED_EVENT";case Block::Audio:return "AUDIO_UNAVAILABLE";case Block::Revision:return "REVISION_CHANGED";case Block::CriticalDisabled:return "CRITICAL_DISABLED";case Block::DriverResponse:return "MEASURED_BRAKING_OR_SHOCK";default:return decision_.mode==Mode::WARN?"IMPORTANT_SUPPORTED_EVENT":decision_.eligible?"NEW_SUPPORTED_EVENT":"NO_SUPPORTED_EVENT";}
}
const char* CoachingEngine::reason() const noexcept {
    if(policyReason_&&(decision_.blocked==Block::None||decision_.blocked==Block::Cooldown||decision_.mode==Mode::DEFER))return policyReason_;
    switch(decision_.blocked) {
    case Block::Inactive:return "coaching source inactive";case Block::Stale:return "original evidence expired or clock/source invalid";
    case Block::Unsupported:return "current evidence does not support this cue";case Block::Cooldown:return "routine cue deferred by experimental coaching interval";
    case Block::Duplicate:return "event already presented; waiting for a meaningful change";case Block::Audio:return "audio unavailable";
    case Block::Revision:return "settings revision changed";case Block::CriticalDisabled:return "experimental critical command disabled";
    case Block::DriverResponse:return "measured braking or motion shock suppresses routine speech";default:break;
    }
    return decision_.eligible?(decision_.mode==Mode::WARN?"fresh independently supported approach warning":"fresh event selected by coaching policy"):"no new supported coaching event";
}
} // namespace ld::coaching
