#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/RiskCoach.hpp"
#include "learndrive/SceneState.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace ld::coaching {
namespace {
bool roadUser(int c) noexcept {return c==0||c==1||c==2||c==3||c==5||c==7;}
bool vulnerable(int c) noexcept {return c==0||c==1||c==3;}
int vulnerability(int c) noexcept {return c==0?3:(c==1||c==3)?2:1;}
// Path corridor half-width (normalized image width) at a ground-contact row.
// Keeps the learner's lane ahead; widens toward the bottom of the image.
double corridor(double foot) noexcept {return std::clamp(.06+.4*(foot-.5),.05,.22);}
Prompt actionFor(const ActorRisk& r,double v,bool speedKnown,bool urgent) noexcept {
    switch(r.kind) {
    case RiskKind::Crossing:return r.category==0?Prompt::personCrossingStop:Prompt::vehicleCrossingWait;
    case RiskKind::Oncoming:return Prompt::oncomingKeepLeft;
    case RiskKind::Roadside:return r.left?Prompt::personLeftSpace:Prompt::personRightSpace;
    case RiskKind::Passed:return Prompt::crossedLookAgain;
    case RiskKind::InPath:
        // Unknown own speed: awareness, never a pedal/gear action.
        if(!speedKnown&&r.level<=1)return r.category==0?Prompt::pedestrianSpace:(r.category==1||r.category==3)?Prompt::riderSpace:Prompt::vehicleSpace;
        if(r.level>=2||urgent)return speedKnown&&v<lowAdviceSpeedKmh?Prompt::clutchHoldBrake:Prompt::brakeReadyStop;
        return r.category==0?Prompt::personEaseCover:(r.category==1||r.category==3)?Prompt::riderEaseCover:Prompt::vehicleEaseGap;
    default:return Prompt::none;
    }
}
int levelOf(Prompt p) noexcept {
    switch(p){case Prompt::vehicleCrossingWait:case Prompt::personCrossingStop:return 3;
    case Prompt::brakeReadyStop:case Prompt::clutchHoldBrake:case Prompt::oncomingKeepLeft:return 2;
    case Prompt::personLeftSpace:case Prompt::personRightSpace:case Prompt::personEaseCover:case Prompt::riderEaseCover:case Prompt::vehicleEaseGap:return 1;
    default:return 0;}
}
}
int group(int c) noexcept {return c==0?0:(c==1||c==3)?1:2;} // person, two-wheeler, vehicle
const RiskCoach::Motion* RiskCoach::associate(uint64_t optical,int category,float x,float y,float w,float h,double capture) noexcept {
    const int g=group(category);Motion* best=nullptr;double bestScore=0;
    // Same detector id: same road user (within 10 s), even across a detection gap.
    for(auto& m:motions_)if(m.id&&m.group==g&&m.optical==optical&&capture>=m.last&&capture-m.last<=10){best=&m;break;}
    if(!best)for(auto& m:motions_)if(m.id&&m.group==g&&m.count&&capture-m.last<=.6) {
        const size_t k=(m.cursor+m.t.size()-1)%m.t.size();
        // Predict the box centre from the last two samples (constant image velocity).
        double px=m.x[k]+m.w[k]*.5,py=m.y[k]+m.h[k]*.5;
        if(m.count>=2){const size_t j=(k+m.t.size()-1)%m.t.size();const double dt=m.t[k]-m.t[j];
            if(dt>1e-3){px+=(px-(m.x[j]+m.w[j]*.5))/dt*(capture-m.t[k]);py+=(py-(m.y[j]+m.h[j]*.5))/dt*(capture-m.t[k]);}}
        const double dist=std::hypot(x+w*.5-px,y+h*.5-py),gate=.6*std::max({double(w),double(h),double(m.w[k]),double(m.h[k])})+.04;
        const double size=std::min(double(w*h),double(m.w[k]*m.h[k]))/std::max(1e-6,std::max(double(w*h),double(m.w[k]*m.h[k])));
        if(dist<gate&&size>.35){const double score=(1-dist/gate)+size;if(score>bestScore){bestScore=score;best=&m;}}
    }
    if(!best) {
        for(auto& m:motions_)if(!m.id||capture-m.last>2){best=&m;break;}
        if(!best)best=&*std::min_element(motions_.begin(),motions_.end(),[](const Motion& a,const Motion& b){return a.last<b.last;});
        *best={};best->id=nextMotion_++;best->group=g;
    }
    best->optical=optical;
    if(std::find(best->opticals.begin(),best->opticals.end(),optical)==best->opticals.end()){best->opticals[best->opticalCursor]=optical;best->opticalCursor=(best->opticalCursor+1)%best->opticals.size();}
    if(capture>best->last+1e-6) {
        best->t[best->cursor]=capture;best->x[best->cursor]=x;best->y[best->cursor]=y;best->w[best->cursor]=w;best->h[best->cursor]=h;
        best->cursor=(best->cursor+1)%best->t.size();best->count=std::min(best->count+1,best->t.size());best->last=capture;
    }
    return best;
}
double RiskCoach::stoppingSeconds(double v,double pitch) noexcept {
    // Learner reaction (~1.2 s) plus braking at a firm-but-comfortable 3 m/s^2,
    // reduced downhill / increased uphill by gravity along the slope.
    const double g=9.81,grade=std::isfinite(pitch)?std::clamp(pitch,-20.,20.)*3.14159265358979/180:0;
    const double decel=std::max(1.,3.+g*std::sin(grade));
    return 1.2+std::max(0.,v)/3.6/decel;
}
const ActorRisk* RiskCoach::riskForOptical(uint64_t optical) const noexcept {
    for(const auto& m:motions_)if(m.id&&m.optical==optical)for(size_t i=0;i<count_;++i)if(risks_[i].id==m.id)return &risks_[i];
    return nullptr;
}
uint64_t RiskCoach::opticalFor(uint64_t roadUser) const noexcept {
    for(const auto& m:motions_)if(m.id==roadUser)return m.optical;
    return 0;
}
double RiskCoach::lastSeen(uint64_t optical) const noexcept {
    double seen=-1;for(const auto& m:motions_)if(m.id&&std::find(m.opticals.begin(),m.opticals.end(),optical)!=m.opticals.end())seen=std::max(seen,m.last);
    return seen;
}
bool RiskCoach::masked(double center,double foot) const noexcept {
    if(maskFrames_<60)return false;
    const int cx=std::clamp(int(center*maskW),0,maskW-1),cy=std::clamp(int(foot*maskH),0,maskH-1);
    // Never mask the lane straight ahead above the bonnet: a lead vehicle at a steady gap lives there.
    if(center>.38&&center<.62&&foot<.85)return false;
    return maskHits_[size_t(cy*maskW+cx)]>maskFrames_*.35;
}
void RiskCoach::learnMask(const WorldState& w,double v) noexcept {
    if(v<8||!(w.captureTime>maskCapture_))return;
    maskCapture_=w.captureTime;++maskFrames_;std::array<bool,maskW*maskH> seen{};
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i){const auto& a=w.actors[i];
        if(a.identity.space!=IdentitySpace::Optical||!cognition::fresh(w.now,a.captureTime))continue;
        const int cx=std::clamp(int((a.x+a.w*.5)*maskW),0,maskW-1),cy=std::clamp(int((a.y+a.h)*maskH),0,maskH-1);
        seen[size_t(cy*maskW+cx)]=true;}
    for(size_t k=0;k<seen.size();++k)if(seen[k]&&maskHits_[k]<65000)++maskHits_[k];
    // Forget slowly so a remounted phone re-learns (half-life ~ 2 minutes of driving).
    if(maskFrames_>=2000){maskFrames_/=2;for(auto& h:maskHits_)h/=2;}
}
void RiskCoach::reset() noexcept {maskHits_={};maskFrames_=0;maskCapture_=-1;motions_={};nextMotion_=1;risks_={};count_=0;top_={};episodes_={};proposal_=0;proposalLevel_=0;urgent_=proposalClosing_=false;proposalId_=0;proposalCapture_=-1;lastGapCue_=-1;}
RiskCoach::Episode* RiskCoach::episode(uint64_t id,double now) noexcept {
    for(auto& e:episodes_)if(e.id==id)return &e;
    Episode* slot=nullptr;
    for(auto& e:episodes_)if(!e.id||now-e.lastSeen>10){slot=&e;break;}
    if(!slot)slot=&*std::min_element(episodes_.begin(),episodes_.end(),[](const Episode& a,const Episode& b){return a.lastSeen<b.lastSeen;});
    *slot={};slot->id=id;return slot;
}
void RiskCoach::observe(const WorldState& w,const ShortMemory& memory,const ObjectTracker& tracks,const EventLedger& ledger,uint64_t source,int support) noexcept {
    proposal_=0;urgent_=false;count_=0;withHistory_=0;top_={};
    if(!enabled_||!w.active||!cognition::fresh(w.now,w.captureTime))return;
    const double now=w.now;
    const bool speedKnown=cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh);
    const double v=speedKnown?w.telemetry.speedKmh:-1;
    const bool pitchKnown=w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    // Physics, not fixed thresholds: can the learner stop in time on THIS slope?
    const double stopping=speedKnown?stoppingSeconds(v,pitchKnown?w.telemetry.pitchDegrees:0):2.5;
    // Measured deceleration over the last ~1 s (the learner's response, from speed history).
    double decel=0;
    if(speedKnown){const MemorySample* old=nullptr;for(const auto& sm:memory.samples())
        if(sm.speedValid&&now-sm.time>=.6&&now-sm.time<=1.5&&(!old||std::abs(now-sm.time-1)<std::abs(now-old->time-1)))old=&sm;
        if(old)decel=(old->speedKmh-v)/3.6/(now-old->time);}
    learnMask(w,v);
    // 1. Assess every fresh optical road user from its own track history.
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size())&&count_<risks_.size();++i) {
        const auto& a=w.actors[i];
        if(a.identity.space!=IdentitySpace::Optical||!roadUser(a.category)||!cognition::fresh(now,a.captureTime)||
           !std::isfinite(a.confidence)||a.confidence<.45||egoVehicle(a.x,a.y,a.w,a.h))continue;
        (void)tracks;
        const Motion* motion=associate(a.identity.id,a.category,a.x,a.y,a.w,a.h,a.captureTime);
        ActorRisk r;r.id=motion?motion->id:a.identity.id;r.category=a.category;
        r.center=a.x+a.w*.5;r.foot=a.y+a.h;r.left=r.center<.5;
        if(masked(r.center,r.foot))continue; // Part of the learner's own car.
        // Clipped at the left/right image edge near the bottom: alongside, being passed.
        if((a.x<.01f||a.x+a.w>.99f)&&r.foot>.9)continue;
        // Motion over the last ~0.3-1.2 s of this road user's own remembered history.
        int old=-1;
        if(motion)for(size_t k=0;k<motion->count;++k){const double age=a.captureTime-motion->t[k];
            if(age>=.3&&age<=1.2&&motion->w[k]>0&&motion->h[k]>0&&(old<0||std::abs(age-.7)<std::abs(a.captureTime-motion->t[size_t(old)]-.7)))old=int(k);}
        bool attached=false;
        if(old>=0) {++withHistory_;const size_t k=size_t(old);
            const double dt=a.captureTime-motion->t[k];
            r.expansion=std::log(std::sqrt(double(a.w)*a.h/(double(motion->w[k])*motion->h[k])))/dt;
            const double lateral=((a.x+a.w*.5)-(motion->x[k]+motion->w[k]*.5))/dt;
            // A static object drifts OUTWARD as it is approached (x' = x*expansion);
            // only motion beyond that is the actor's own lateral movement.
            const double own=lateral-(r.center-.5)*std::max(0.,r.expansion);
            r.inwardRate=own*(r.center<.5?1:-1);r.rawInward=lateral*(r.center<.5?1:-1);
            // A box clipped by the image edge shifts inward as the object leaves the
            // frame; that is not the object's own lateral motion.
            if(a.x<.01f||a.x+a.w>.99f)r.inwardRate=r.rawInward=0;
            // Fixed in the frame while the car moves, low in the image: part of the
            // learner's own car (dashboard ornament, mount, bonnet), not a road user.
            attached=v>5&&r.foot>.8&&dt>=.5&&std::abs(lateral)<.015&&std::abs(r.expansion)<.05;
        }
        if(attached)continue;
        // Flow-based TTC only for road users close enough for it to be plausible.
        if(std::isfinite(a.tti)&&a.tti>0&&(a.h>.15f||r.foot>.85))r.ttc=a.tti;
        if(r.expansion>.08)r.ttc=std::min(r.ttc,1/r.expansion);
        const double edge=std::max(0.,std::abs(r.center-.5)-a.w*.5),half=corridor(r.foot);
        r.inPath=edge<half&&r.foot>.45;
        if(!r.inPath&&r.inwardRate>.03)r.enterSeconds=(edge-half)/r.inwardRate;
        const bool near=r.foot>.52;
        // 2. Classify the conflict.
        // Keep-left traffic: a vehicle right of the path centre that is closing fast
        // is oncoming traffic passing on the right, not a lead vehicle to brake for.
        const bool rightSide=r.center>.52&&!r.left;
        const bool oncoming=rightSide&&r.category!=0&&r.expansion>.25&&edge<half+.12&&near&&v>5&&r.inwardRate<.20;
        // Crossing: the actor's own lateral motion is TOWARD/through the path (not
        // the outward drift of something being passed).
        // ...and it must actually move toward the path in the image (compensation alone
        // must not turn a pedestrian walking along the edge into a "crossing").
        if(near&&r.inwardRate>.10&&r.rawInward>.05&&r.expansion>-.1&&(r.inPath||r.enterSeconds<2.5)) {r.kind=RiskKind::Crossing;r.level=3;}
        else if(oncoming) {r.kind=RiskKind::Oncoming;r.level=2;}
        // Heavy vehicles stop and swerve slowly: give them extra margin.
        // ...unless the learner is already braking hard enough to stop short of it
        // (needed deceleration for a stationary hazard at this TTC: v / (2*TTC)).
        else if(r.inPath&&near&&r.ttc<stopping+(r.category==5||r.category==7?.5:0)&&v>5&&!(decel>.5&&decel>=.9*(v/3.6)/(2*r.ttc)))
            {r.kind=RiskKind::InPath;r.level=3;}
        else if(r.inPath&&near&&r.ttc<stopping+2.5+(r.category==5||r.category==7?1:0)&&v!=0) {r.kind=RiskKind::InPath;r.level=2;}
        // Crawling up to a vulnerable road user right in front: be ready to stop.
        else if(r.inPath&&r.foot>.62&&vulnerable(r.category)&&v>3&&v<15) {r.kind=RiskKind::InPath;r.level=2;}
        else if(r.category==0&&!r.inPath&&edge<half+.15&&r.foot>.55&&v>12) {r.kind=RiskKind::Roadside;r.level=1;}
        else if(r.inPath&&r.foot>.58&&v>5) {r.kind=RiskKind::InPath;r.level=1;}
        else if(r.inPath&&r.foot>.58&&!speedKnown) {r.kind=RiskKind::InPath;r.level=1;} // Awareness only (see actionFor).
        // Temporal confirmation: a road user must be seen consistently before it can
        // drive an instruction (single-frame flickers of the mount or reflections cannot).
        double span=0;if(motion)for(size_t k=0;k<motion->count;++k)span=std::max(span,a.captureTime-motion->t[k]);
        const size_t seen=motion?motion->count:0;
        if(r.level>=3?seen<2:(seen<3||span<.3))r.level=0;
        r.score=r.level*10+vulnerability(r.category)+2*std::clamp(r.foot,0.,1.)+(std::isfinite(r.ttc)?std::min(5.,5/r.ttc):0);
        risks_[count_++]=r;
        if(auto* e=episode(r.id,now)){e->lastSeen=now;e->kind=r.kind;if(r.kind==RiskKind::Crossing)e->crossing=true;
            if(e->crossing&&r.kind!=RiskKind::Crossing&&!r.inPath&&e->crossedOutAt<0)e->crossedOutAt=now;}
    }
    // 3. What has already been said about each road user (ledger, not guesses).
    for(auto& e:episodes_)if(e.id) {
        for(const auto& ev:ledger.entries())if(ev.used&&ev.sourceId==source&&ev.identity.space==IdentitySpace::Optical&&
            std::find(e.optical.begin(),e.optical.end(),ev.identity.id)!=e.optical.end()&&ev.acceptedAt>=0&&
            ev.acceptedAt>e.spokenAt&&levelOf(ev.acceptedPrompt)>=0){e.spokenAt=ev.acceptedAt;e.spokenPrompt=int(ev.acceptedPrompt);e.spokenLevel=std::max(e.spokenLevel,levelOf(ev.acceptedPrompt));}
        // A crossing actor that left the corridor while the learner waits.
        bool visible=false;for(size_t i=0;i<count_;++i)if(risks_[i].id==e.id){visible=true;if(risks_[i].kind!=RiskKind::Crossing&&!risks_[i].inPath&&e.crossing&&e.crossedOutAt<0)e.crossedOutAt=now;}
        if(!visible&&e.crossing&&e.crossedOutAt<0)e.crossedOutAt=now;
    }
    // 4. Rank: the most dangerous road user wins; then decide whether to speak.
    for(size_t i=0;i<count_;++i)if(risks_[i].level&&(!top_.level||risks_[i].score>top_.score))top_=risks_[i];
    // Minimal still speaks for a vulnerable road user who is closing on the path.
    const bool vulnerableClosing=vulnerable(top_.category)&&top_.inPath&&std::isfinite(top_.ttc)&&top_.ttc<10;
    const int minimum=support>=1||vulnerableClosing?1:2;
    // One risk instruction at a time: within 3 s of the last one, only a higher risk speaks.
    double lastAt=-1;int lastLevel=0;for(const auto& e:episodes_)if(e.id&&e.spokenAt>lastAt){lastAt=e.spokenAt;lastLevel=e.spokenLevel;}
    // Within 1.5 s of the last instruction only an urgent (level 3) risk may cut in.
    const bool settling=lastAt>=0&&((now-lastAt<3&&top_.level<=lastLevel)||(now-lastAt<1.5&&top_.level<3));
    // "Vehicle ahead, keep a gap" for non-closing traffic at most every 15 s.
    const bool gapCue=top_.kind==RiskKind::InPath&&top_.level==1&&!vulnerable(top_.category)&&!(std::isfinite(top_.ttc)&&top_.ttc<10);
    const bool gapRecent=gapCue&&lastGapCue_>=0&&now-lastGapCue_<15;
    if(!settling&&!gapRecent&&top_.level>=minimum&&!(support==1&&top_.level==1&&!vulnerable(top_.category))) {
        auto* e=episode(top_.id,now);
        // Give the learner time to respond to what was just said about this road
        // user before escalating, unless contact is truly imminent.
        const bool reacting=e->spokenAt>=0&&now-e->spokenAt<2&&!(std::isfinite(top_.ttc)&&top_.ttc<1.5);
        const bool rising=top_.level>e->spokenLevel&&!reacting;
        // Spoken already at this level: escalate once if the learner is not slowing.
        const bool unanswered=e->spokenLevel>=2&&top_.level>=2&&e->spokenAt>=0&&now-e->spokenAt>=2.5&&now-e->spokenAt<=8&&
            speedKnown&&v>5&&memory.speedTrend(now,e->spokenAt)!=SpeedTrend::Decreasing&&e->spokenLevel<3;
        if(rising||unanswered) {
            ActorRisk speak=top_;if(unanswered&&!rising)speak.level=3;
            urgent_=speak.level>=3&&speak.kind!=RiskKind::Crossing?v>5:speak.level>=3&&v>8;
            // Speak about the current detection; remember it under this road user's identity.
            uint64_t optical=0;for(const auto& m:motions_)if(m.id==top_.id){optical=m.optical;break;}
            if(std::find(e->optical.begin(),e->optical.end(),optical)==e->optical.end()){e->optical[e->opticalCursor]=optical;e->opticalCursor=(e->opticalCursor+1)%e->optical.size();}
            proposal_=int(actionFor(speak,v,speedKnown,urgent_));proposalId_=optical;proposalCapture_=w.captureTime;
            proposalLevel_=speak.level;proposalClosing_=std::isfinite(speak.ttc)&&speak.ttc<10;
            if(gapCue)lastGapCue_=now;
            ++counts_[size_t(speak.kind)];
        }
    }
    // 5. Waiting at a junction: the crossing road user has passed.
    if(!proposal_&&speedKnown&&v<=3)for(auto& e:episodes_)if(e.id&&e.crossing&&!e.passedCue&&e.spokenLevel>=3&&e.crossedOutAt>=0&&now-e.crossedOutAt>=1&&now-e.crossedOutAt<=6) {
        bool other=false;for(size_t i=0;i<count_;++i)other|=risks_[i].kind==RiskKind::Crossing||(risks_[i].inPath&&risks_[i].level>=2);
        if(other)break;
        e.passedCue=true;proposal_=int(Prompt::crossedLookAgain);proposalId_=e.optical[0]?e.optical[0]:e.id;proposalCapture_=w.captureTime;urgent_=false;proposalLevel_=0;proposalClosing_=false;++counts_[size_t(RiskKind::Passed)];break;
    }
}
size_t RiskCoach::proposals(const WorldState& w,Candidate* out,size_t capacity) const noexcept {
    if(!enabled_||!proposal_||!out||!capacity)return 0;
    Candidate c;c.prompt=static_cast<Prompt>(proposal_);c.origin=7;c.identity={IdentitySpace::Optical,proposalId_,0};
    c.kind=urgent_?EventKind::Approach:EventKind::Attention;c.urgent=urgent_;c.captureTime=proposalCapture_;c.maxAge=maxVisionAgeSeconds;
    c.expiresAt=c.captureTime+c.maxAge;c.supported=true;c.confidence=.9;c.tti=top_.ttc;
    c.evidenceKind=proposalLevel_*2+(proposalClosing_?1:0); // Risk level and closing, for ranking.
    if(c.urgent&&!std::isfinite(c.tti))c.tti=2.5;
    // "It has passed" is its own event about the same road user, not a repeat of "wait".
    if(c.prompt==Prompt::crossedLookAgain){c.kind=EventKind::Status;c.identity.scope=1;}
    if(!evidence(c,w,w.now))return 0;
    out[0]=c;return 1;
}
bool RiskCoach::evidence(const Candidate& c,const WorldState& w,double now) noexcept {
    if(c.origin!=7||c.identity.space!=IdentitySpace::Optical||!riskMayUse(c.prompt)||c.critical||!w.active)return false;
    if(!cognition::fresh(now,w.captureTime))return false; // Perception must be live.
    if(c.prompt==Prompt::crossedLookAgain)return cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&w.telemetry.speedKmh<=3;
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size());++i){const auto& a=w.actors[i];
        if(a.identity==c.identity)return cognition::fresh(now,a.captureTime)&&std::isfinite(a.confidence)&&a.confidence>=.4&&!egoVehicle(a.x,a.y,a.w,a.h);}
    return false;
}
std::string RiskCoach::json() const {
    std::ostringstream o;o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"assessed\":"<<count_<<",\"with_history\":"<<withHistory_<<",\"top\":{\"id\":"<<top_.id<<",\"category\":"<<top_.category
    <<",\"kind\":"<<int(top_.kind)<<",\"level\":"<<top_.level<<",\"in_path\":"<<(top_.inPath?"true":"false")<<",\"ttc_s\":";
    if(std::isfinite(top_.ttc))o<<top_.ttc;else o<<"null";
    o<<",\"foot\":"<<top_.foot<<",\"center\":"<<top_.center<<",\"expansion\":"<<top_.expansion<<",\"inward\":"<<top_.inwardRate
    <<",\"metric_distance\":\"UNAVAILABLE\"},\"spoken_by_kind\":["<<counts_[1]<<','<<counts_[2]<<','<<counts_[3]<<','<<counts_[4]<<','<<counts_[5]<<"]}";return o.str();
}
}
