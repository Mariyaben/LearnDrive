#include "learndrive/coaching/CoachingEngine.hpp"
#include "learndrive/coaching/RiskCoach.hpp"
#include "learndrive/coaching/LearnerRecord.hpp"
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
// Ground distance from the ground-contact row relative to a horizon at mid-image: a phone
// ~1.25 m above the road with a ~55 degree vertical field of view (focal ~0.96 image heights).
double groundDistance(double foot) noexcept {return foot>.505?1.2/(foot-.5):std::numeric_limits<double>::infinity();}
// Distance from apparent height: typical height of the class (person 1.7 m, rider 1.6 m,
// car 1.5 m, bus/truck 3 m) over its height in image heights, same focal assumption.
double sizeDistance(int category,double h) noexcept {
    const double real=category==0?1.7:(category==1||category==3)?1.6:(category==5||category==7)?3.0:1.5;
    return h>.01?.96*real/h:std::numeric_limits<double>::infinity();}
// Reaction (1.2 s) plus braking at 3 m/s^2, metres.
double stoppingDistance(double kmh) noexcept {const double v=std::max(0.,kmh)/3.6;return v*1.2+v*v/6;}
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
// The kind of situation a road user is, for the learner's record: oncoming traffic, else who is ahead.
static int skillOf(const ActorRisk& r) noexcept {return r.kind==RiskKind::Oncoming?3:group(r.category);}
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
bool RiskCoach::masked(double center,double foot,double bonnet) const noexcept {
    if(maskFrames_<60)return false;
    const int cx=std::clamp(int(center*maskW),0,maskW-1),cy=std::clamp(int(foot*maskH),0,maskH-1);
    // Never mask the lane straight ahead above the bonnet: a lead vehicle at a steady gap lives there.
    if(center>.38&&center<.62&&foot<std::max(.85,bonnet))return false;
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
// One road user's evidence about where the car is heading (see Heading). Only while driving
// straight at speed; the caller checks that.
void RiskCoach::sampleHeading(const Motion& m,const ActorObservation& a,const WorldState& w,double fx,double cx) noexcept {
    const double now=a.captureTime,foot=double(a.y)+a.h;
    // The mount moved (the horizon row changed): what was learned belongs to the old aim.
    if(w.horizonMeasured&&std::isfinite(w.horizon)) {
        if(heading_.horizon==heading_.horizon&&std::abs(w.horizon-heading_.horizon)>.06)heading_={};
        if(heading_.horizon!=heading_.horizon)heading_.horizon=w.horizon;
    }
    // Far ahead on the road: small, standing just under the horizon, clear of the image edges.
    if(w.horizonMeasured&&std::isfinite(w.horizon)&&a.h<.07f&&foot>w.horizon-.01&&foot<w.horizon+.06&&a.x>.05f&&a.x+a.w<.95f&&(m.farAt<0||now-m.farAt>=1)) {
        m.farAt=now;heading_.far[heading_.farCursor]=float(a.x+a.w*.5);
        heading_.farCursor=(heading_.farCursor+1)%heading_.far.size();heading_.farCount=std::min(heading_.farCount+1,heading_.far.size());
    }
    if(m.headAt>=0&&now-m.headAt<.5)return;
    // Least squares over this road user's last 1.2 s: sideways speed of the box centre and its growth rate.
    double st=0,sx=0,sl=0,stt=0,stx=0,stl=0,first=now,last=-1;int n=0;
    for(size_t k=0;k<m.count;++k) {
        const double age=now-m.t[k];
        if(age<0||age>1.2||m.w[k]<=0||m.h[k]<=0)continue;
        // Cut off by the image edge, or so close that its box is one side of it: no clean centre.
        if(m.x[k]<.02f||m.x[k]+m.w[k]>.98f||m.y[k]+m.h[k]>.97f||m.h[k]>.3f||m.w[k]>.3f)return;
        const double t=m.t[k]-now,x=m.x[k]+m.w[k]*.5,l=.5*std::log(double(m.w[k])*m.h[k]);
        st+=t;sx+=x;sl+=l;stt+=t*t;stx+=t*x;stl+=t*l;++n;first=std::min(first,m.t[k]);last=std::max(last,m.t[k]);
    }
    if(n<5||last-first<.6)return;
    const double den=n*stt-st*st;if(den<=1e-9)return;
    const double sideways=(n*stx-st*sx)/den,growth=(n*stl-st*sl)/den;
    if(growth<.15||growth>2.5)return; // Clearly approaching, and not a jump between two boxes.
    // The turn rate only selects straight driving (the caller's gate); it is not subtracted here.
    // Small turns left and right cancel over many road users, while a turn rate that is itself
    // slightly off (a gyroscope's bias; in replays, a rate estimated from the video of a camera
    // that is turned) would shift every sample the same way: on the Nov 8 replay subtracting it
    // moved the heading to 0.46 when the road users themselves say 0.53.
    (void)fx;
    const double head=sx/n-sideways/growth;
    if(!(std::abs(head-cx)<=.2))return;
    m.headAt=now;heading_.motion[heading_.motionCursor]=float(head);
    heading_.motionCursor=(heading_.motionCursor+1)%heading_.motion.size();heading_.motionCount=std::min(heading_.motionCount+1,heading_.motion.size());
}
void RiskCoach::updateHeading(double now,double cx) noexcept {
    if(heading_.updatedAt>=0&&now>=heading_.updatedAt&&now-heading_.updatedAt<1)return;
    heading_.updatedAt=now;
    const auto median=[](std::array<float,96> v,size_t n){std::nth_element(v.begin(),v.begin()+long(n/2),v.begin()+long(n));return double(v[n/2]);};
    const double none=std::numeric_limits<double>::quiet_NaN();
    const bool motion=heading_.motionCount>=12,far=heading_.farCount>=24;
    // Keep-left traffic: the far road users to the right of the car's own lane (oncoming traffic,
    // the far verge) outnumber those to its left, by about 2 % of the image width.
    heading_.byMotion=motion?median(heading_.motion,heading_.motionCount):none;
    heading_.byFar=far?median(heading_.far,heading_.farCount)-.02:none;
    if(!motion||!far)return; // Not enough to go on: the optical centre stands.
    const double apart=std::abs(heading_.byMotion-heading_.byFar);
    // The two cues agree to within 4 % of the image width (about 3 degrees): their mean is the
    // heading. More than 6 % apart: neither is believed, and the optical centre stands again.
    if(apart>.06){heading_.x=none;return;}
    if(apart>.04)return;
    // A mount turned more than ~10 degrees is beyond what this corrects; the driver should re-aim it.
    const double target=std::clamp(.5*(heading_.byMotion+heading_.byFar),cx-.12,cx+.12);
    heading_.x=heading_.x==heading_.x?heading_.x+.25*(target-heading_.x):target;
}
double RiskCoach::meanYaw(double from,double to) const noexcept {
    double sum=0;int n=0;
    for(size_t i=0;i<yawCount_;++i)if(yawT_[i]>=from-.02&&yawT_[i]<=to+.02){sum+=yawV_[i];++n;}
    return n?sum/n:0;
}
void RiskCoach::reset() noexcept {hold_={};brief_=false;stopCredit_=-1;for(auto& m:motions_){m.headAt=-1;m.farAt=-1;}heading_.updatedAt=-1;lastOncomingCue_=-1;yawT_={};yawV_={};yawCursor_=yawCount_=0;maskHits_={};maskFrames_=0;maskCapture_=-1;motions_={};nextMotion_=1;risks_={};count_=0;top_={};episodes_={};proposal_=0;proposalLevel_=0;urgent_=proposalClosing_=false;proposalId_=0;proposalCapture_=-1;lastGapCue_=-1;}
RiskCoach::Episode* RiskCoach::episode(uint64_t id,double now) noexcept {
    for(auto& e:episodes_)if(e.id==id)return &e;
    Episode* slot=nullptr;
    for(auto& e:episodes_)if(!e.id||now-e.lastSeen>10){slot=&e;break;}
    if(!slot)slot=&*std::min_element(episodes_.begin(),episodes_.end(),[](const Episode& a,const Episode& b){return a.lastSeen<b.lastSeen;});
    *slot={};slot->id=id;return slot;
}
void RiskCoach::observe(const WorldState& w,const ShortMemory& memory,const ObjectTracker& tracks,const EventLedger& ledger,uint64_t source,int support,LearnerRecord* learner) noexcept {
    proposal_=0;urgent_=false;count_=0;withHistory_=0;top_={};hold_={};brief_=false;proposalPathFirst_=false;
    if(!enabled_||!w.active||w.geometryUnknown||!cognition::fresh(w.now,w.captureTime))return;
    const double now=w.now;
    const bool speedKnown=cognition::fresh(now,w.telemetry.time,.15)&&w.telemetry.speedValid&&std::isfinite(w.telemetry.speedKmh);
    const double v=speedKnown?w.telemetry.speedKmh:-1;
    const bool pitchKnown=w.telemetry.calibrated&&w.telemetry.pitchValid&&std::isfinite(w.telemetry.pitchDegrees);
    // Physics, not fixed thresholds: can the learner stop in time on THIS slope?
    const double stopping=speedKnown?stoppingSeconds(v,pitchKnown?w.telemetry.pitchDegrees:0):2.5;
    // The latest moment braking must begin to stop short of something standing in the road: the
    // reaction time plus HALF the braking time (while it slows, the car covers half the distance
    // its speed would). `stopping` is the whole time to a standstill; as the limit on time to
    // contact it called "Brake now" 4.9 s from contact at 40 km/h, where 3.1 s is the latest.
    const double brakeBy=1.2+.5*(stopping-1.2);
    // Measured deceleration over the last ~1 s (the learner's response, from speed history).
    double decel=0;
    if(speedKnown){const MemorySample* old=nullptr;for(const auto& sm:memory.samples())
        if(sm.speedValid&&now-sm.time>=.6&&now-sm.time<=1.5&&(!old||std::abs(now-sm.time-1)<std::abs(now-old->time-1)))old=&sm;
        if(old)decel=(old->speedKmh-v)/3.6/(now-old->time);}
    learnMask(w,v);
    if(w.yawKnown&&std::isfinite(w.yawRate)&&(yawCount_==0||now>=yawT_[(yawCursor_+yawT_.size()-1)%yawT_.size()]+.02)){
        yawT_[yawCursor_]=now;yawV_[yawCursor_]=w.yawRate;yawCursor_=(yawCursor_+1)%yawT_.size();yawCount_=std::min(yawCount_+1,yawT_.size());}
    // Box geometry is read in the analysed image's own optics, mapped onto the 4:3 frame the
    // thresholds below were tuned on (~0.72 image widths and ~0.96 image heights per radian,
    // optical centre mid-image). The 0.8.1 preview fills a wide panel with the bottom of the
    // frame: the same angle covers ~1.3x more of the image and a level horizon sits ~0.31 down,
    // not 0.5. Read raw, road users beyond ~13 m "stood above the horizon" and were dropped as not
    // on the road; only close ones were coached (Nov 8). Edge and mask tests stay in raw pixels.
    const CameraModel cam=w.camera.valid()?w.camera:CameraModel{};
    const double kx=.72/cam.fx,ky=.96/cam.fy;
    // Sideways positions are measured from the column the car is heading for: the optical centre
    // of a camera aimed straight ahead, else what this drive has shown (see Heading).
    updateHeading(now,cam.cx);
    const double headX=headingColumn(cam.cx);
    auto X=[&](double x){return .5+(x-headX)*kx;};
    // Evidence for the heading is taken only while driving straight at speed.
    const bool straight=speedKnown&&v>=15&&w.yawKnown&&std::isfinite(w.yawRate)&&std::abs(w.yawRate)<.03&&std::abs(meanYaw(now-1.2,now))<.03;
    auto Y=[&](double y){return .5+(y-cam.cy)*ky;};
    // Image geometry is relative to the measured horizon (a level camera puts it at the optical centre).
    // (Up to 0.97 of the canonical frame: a phone leaning back 20-25 degrees on a dashboard mount
    // puts the horizon four fifths of the way down the preview. The limit used to be 0.85; below
    // it every distance came out two to five times too short, nothing could read as farther than
    // 23 m, and buses 30-60 m away got "Vehicle ahead. Brake now" - Nov 12, horizon at 0.82.)
    const double shift=std::isfinite(w.horizon)?std::clamp(Y(w.horizon),.25,.97)-.5:0;
    // Where the learner's own bonnet or dashboard begins in the image: 0.8 of the way down with a
    // level camera. With the phone tilted back the horizon itself is that low, and what stands just
    // under it is on the road, not on the bonnet: half-way from the horizon to the bottom edge.
    const double bonnet=std::max(.8,w.horizonMeasured&&std::isfinite(w.horizon)?.5*(w.horizon+1):.8);
    const double stopM=stoppingDistance(speedKnown?v:15);
    // "You first": is the learner's measured speed falling right now (>= 2 km/h over the last
    // 1-2.5 s, or sustained braking)? Speed only; pedals and eyes are not measured.
    const bool slowing=speedKnown&&(memory.speedTrend(now)==SpeedTrend::Decreasing||w.telemetry.braking);
    // Has the learner acted first for this road user? Their speed has fallen since the coach noticed
    // it and has not come back up by more than 2 km/h from its lowest since: easing off and then
    // speeding up again is not acting first. Followed until something is said about the road user.
    auto watch=[&](Episode& e){if(!speedKnown||e.spokenAt>=0)return;
        e.lowKmh=std::min(e.lowKmh,v);if(slowing)e.slowed=true;e.before=e.slowed&&v<=e.lowKmh+2;};
    // 1. Assess every fresh optical road user from its own track history.
    for(size_t i=0;i<std::min(w.actorCount,w.actors.size())&&count_<risks_.size();++i) {
        const auto& a=w.actors[i];
        if(a.identity.space!=IdentitySpace::Optical||!roadUser(a.category)||!cognition::fresh(now,a.captureTime)||
           !std::isfinite(a.confidence)||a.confidence<.45||egoVehicle(a.x,a.y,a.w,a.h))continue;
        (void)tracks;
        const Motion* motion=associate(a.identity.id,a.category,a.x,a.y,a.w,a.h,a.captureTime);
        ActorRisk r;r.id=motion?motion->id:a.identity.id;r.category=a.category;
        // A "person" sitting on a bicycle or motorcycle is its rider: judge it as a two-wheeler
        // (an oncoming rider needs "keep left, ease off", not "person ahead, brake now").
        if(a.category==0)for(size_t k=0;k<std::min(w.actorCount,w.actors.size());++k) {
            const auto& b=w.actors[k];
            if(k==i||b.identity.space!=IdentitySpace::Optical||(b.category!=1&&b.category!=3)||!cognition::fresh(now,b.captureTime))continue;
            const double overlap=std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x);
            // The rider's body stands on the bike: wide horizontal overlap, the bike under the person's
            // middle, reaching about where the person's box ends and not much smaller than the person.
            // A small two-wheeler box behind someone's legs is a bike further up the road, and the
            // person is on foot (Nov 8, 2:42: a pedestrian in the lane got "Oncoming two-wheeler.
            // Keep left" and then nothing more until "Brake now").
            if(overlap>.5*std::min(a.w,b.w)&&b.y<a.y+a.h&&b.y+b.h>a.y+a.h*.5&&
               std::abs((b.y+b.h)-(a.y+a.h))<=.25*a.h&&b.h>=.35*a.h){r.category=b.category;break;}
        }
        // Canonical box (angles; see X/Y above): left edge, width, height. r.foot stays the raw
        // image row (the bonnet, the frame edge and the learned mask are image facts).
        const double ax=X(a.x),aw=a.w*kx,ah=a.h*ky;
        r.center=ax+aw*.5;r.foot=a.y+a.h;r.left=r.center<.5;
        if(masked(a.x+a.w*.5,r.foot,bonnet))continue; // Part of the learner's own car.
        // Clipped at the left/right image edge near the bottom: alongside, being passed.
        if((a.x<.01f||a.x+a.w>.99f)&&r.foot>.9)continue;
        // Motion over the last ~0.3-1.2 s of this road user's own remembered history.
        int old=-1;
        if(motion)for(size_t k=0;k<motion->count;++k){const double age=a.captureTime-motion->t[k];
            if(age>=.3&&age<=1.2&&motion->w[k]>0&&motion->h[k]>0&&(old<0||std::abs(age-.7)<std::abs(a.captureTime-motion->t[size_t(old)]-.7)))old=int(k);}
        // Came from alongside: within the last 1.5 s its box was cut off by the side of the frame at
        // the very bottom of the image, beside the learner's own car. It is overtaking, or being
        // overtaken; a vehicle coming out of a side road enters the frame up the road (Nov 8, 11:26:
        // a car overtaking close on the right and pulling in read as "Vehicle crossing. Brake now").
        bool fromAlongside=false;
        if(motion)for(size_t k=0;k<motion->count;++k){const double age=a.captureTime-motion->t[k];
            if(age>=0&&age<=1.5&&(motion->x[k]<.01f||motion->x[k]+motion->w[k]>.99f)&&motion->y[k]+motion->h[k]>.95f)fromAlongside=true;}
        bool attached=false;
        if(old>=0) {++withHistory_;const size_t k=size_t(old);
            const double dt=a.captureTime-motion->t[k];
            // Cut off at the side edge (a vehicle passing beside), the width stops growing: use the
            // height; cut off at the bottom, the width.
            const bool sideCut=a.x<.01f||a.x+a.w>.99f||motion->x[k]<.01f||motion->x[k]+motion->w[k]>.99f;
            const bool bottomCut=a.y+a.h>.99f||motion->y[k]+motion->h[k]>.99f;
            // Cut off both ways (a vehicle coming into the frame from alongside): its box grows as
            // more of it comes into view, which says nothing about it coming closer.
            r.expansion=sideCut&&bottomCut?0:(sideCut&&!bottomCut?std::log(double(a.h)/motion->h[k]):bottomCut&&!sideCut?std::log(double(a.w)/motion->w[k]):
                std::log(std::sqrt(double(a.w)*a.h/(double(motion->w[k])*motion->h[k]))))/dt;
            const double imageLateral=((a.x+a.w*.5)-(motion->x[k]+motion->w[k]*.5))*kx/dt;
            // Turning slides every static object across the image (about 0.72 image widths per
            // radian; left turn -> rightward): remove it before judging the road user's own
            // movement (Nov 4 and 6: parked vehicles, a billboard and people walking along the
            // road "crossing" on bends).
            const double lateral=imageLateral-.72*meanYaw(motion->t[k],a.captureTime);
            // A static object drifts OUTWARD as it is approached (x' = x*expansion);
            // only motion beyond that is the actor's own lateral movement.
            const double own=lateral-(r.center-.5)*std::max(0.,r.expansion);
            r.inwardRate=own*(r.center<.5?1:-1);r.rawInward=lateral*(r.center<.5?1:-1);
            // A box clipped by the image edge shifts inward as the object leaves the
            // frame; that is not the object's own lateral motion.
            if(a.x<.01f||a.x+a.w>.99f)r.inwardRate=r.rawInward=0;
            // Fixed in the frame while the car moves, low in the image: part of the
            // learner's own car (dashboard ornament, mount, bonnet), not a road user.
            // (Fixed in the IMAGE: it turns with the car, so its raw image motion is used.)
            attached=v>5&&r.foot>bonnet&&dt>=.5&&std::abs(imageLateral)<.015&&std::abs(r.expansion)<.05;
            // ...or at the side edge of the frame at any height: a phone mount arm or a pillar.
            if(v>5&&dt>=.5&&(a.x<.02f||a.x+a.w>.98f)&&std::abs(imageLateral)<.012&&std::abs(r.expansion)<.04)attached=true;
        }
        if(attached)continue;
        if(motion&&straight)sampleHeading(*motion,a,w,cam.fx,cam.cx);
        // Moving toward the path must be sustained over both halves of the window, not one jump
        // (Nov 4: the detector alternating between two figures side by side read as crossing).
        bool steady=false;
        if(old>=0) {
            const size_t k=size_t(old);const double span=a.captureTime-motion->t[k];int mid=-1;
            for(size_t q=0;q<motion->count;++q){const double age=a.captureTime-motion->t[q];
                if(age>.1&&age<span-.1&&(mid<0||std::abs(age-span*.5)<std::abs(a.captureTime-motion->t[size_t(mid)]-span*.5)))mid=int(q);}
            // Raw boxes in, canonical geometry inside (growth is a ratio, unchanged by the mapping).
            // (raw: its movement across the image toward the path, before the allowance for the
            // outward drift of something being approached.)
            auto inward=[&](double t0,double rx0,double rw0,double h0,double t1,double rx1,double rw1,double h1,double& raw){
                raw=0;const double dt=t1-t0;if(dt<=.05||rw0<=0||h0<=0)return 0.;
                const double x0=X(rx0),w0=rw0*kx,x1=X(rx1),w1=rw1*kx;
                const double grow=std::log(std::sqrt(rw1*h1/(rw0*h0)))/dt,c1=x1+w1*.5;
                const double lat=(c1-(x0+w0*.5))/dt-.72*meanYaw(t0,t1);
                raw=lat*(c1<.5?1:-1);
                return (lat-(c1-.5)*std::max(0.,grow))*(c1<.5?1:-1);};
            // In both halves the road user must itself move across the image toward the path. One
            // half that "moves" only by the allowance for being approached, then a jump, is the
            // tracker going from one parked vehicle to the next (Nov 8, 8:45: "Vehicle crossing.
            // Brake now" for a parked car).
            if(mid>=0){const size_t m=size_t(mid);double raw0=0,raw1=0;
                steady=inward(motion->t[k],motion->x[k],motion->w[k],motion->h[k],motion->t[m],motion->x[m],motion->w[m],motion->h[m],raw0)>.03&&
                       inward(motion->t[m],motion->x[m],motion->w[m],motion->h[m],a.captureTime,a.x,a.w,a.h,raw1)>.03&&raw0>.02&&raw1>.02;}
        }
        // Feet hidden: by the learner's own bonnet (bottom of the image) or behind a vehicle whose box
        // top meets this box's bottom. Then the box bottom is not where it stands, so the two
        // "standing on the road" tests below do not apply: a pedestrian about to step out from
        // behind a parked car must never be dropped as a billboard.
        bool footHidden=r.foot>bonnet;double behindFoot=-1;
        for(size_t k=0;k<std::min(w.actorCount,w.actors.size())&&!footHidden;++k) {
            const auto& b=w.actors[k];
            if(k==i||b.identity.space!=IdentitySpace::Optical||(b.category!=2&&b.category!=3&&b.category!=5&&b.category!=7)||!cognition::fresh(now,b.captureTime))continue;
            const double overlap=std::min(a.x+a.w,b.x+b.w)-std::max(a.x,b.x);
            if(overlap>.3*a.w&&std::abs(double(b.y)-(a.y+a.h))<.04&&b.y>a.y){footHidden=true;behindFoot=b.y+b.h;}
        }
        // Standing on the road? As a road user's box grows (it or the learner approaching), its
        // bottom edge moves down the image by about camera height / its height for each unit of
        // growth. A person printed on a billboard or a figure on a wall keeps its bottom row while
        // it grows (Nov 4: "Person crossing. Stop and wait" for a roadside billboard). Needs no horizon.
        if(motion&&!footHidden) {
            int base=-1;double grow=1;
            for(size_t k=0;k<motion->count;++k)if(motion->h[k]>0&&a.captureTime-motion->t[k]<=2&&a.h/motion->h[k]>grow){grow=a.h/motion->h[k];base=int(k);}
            if(base>=0&&grow>=1.25) {
                const size_t k=size_t(base);
                const double slope=((a.y+a.h)-(motion->y[k]+motion->h[k]))/(a.h-motion->h[k]);
                const double tall=a.category==0?1.7:(a.category==1||a.category==3)?1.6:(a.category==5||a.category==7)?3.0:1.5;
                if(slope<.3*1.25/tall)continue;
            }
        }
        // Below-horizon position as if the camera were level, and the ground distance it implies.
        const double foot=Y(r.foot)-shift;r.distanceM=groundDistance(foot);
        // Behind a vehicle: about a metre beyond where that vehicle stands.
        if(behindFoot>0)r.distanceM=groundDistance(Y(behindFoot)-shift)+1;
        // The detector has no class for autorickshaws and calls them "truck": a "truck" whose box is
        // too short for its ground distance (under ~2.3 m tall) is a small vehicle (Nov 6: "Oncoming
        // truck" and "Truck ahead" for auto-rickshaws), with a light vehicle's margins and words.
        // (Not when its top is cut off by the image edge or by the sky the detector was not shown.)
        // A "bus" keeps its name. The height comes from the ground distance, so where the horizon is
        // off (a slope, a dip) a real bus reads small, and taken for a car its apparent size then
        // agrees with the wrong distance: three real buses lost their name on Nov 5 and one coming
        // the other way 40 m off became "Vehicle ahead. Brake now" on Nov 4. An autorickshaw called
        // a bus only sounds wrong.
        if(a.category==7&&w.horizonMeasured&&std::isfinite(r.distanceM)&&a.y>std::max(.01,w.visibleTop+.01)&&a.y+a.h<.99f&&r.distanceM*ah/.96<2.3)r.category=2;
        // Cut off at the top (image edge, or the sky the detector mosaic left out) or bottom:
        // the apparent height says nothing about distance.
        const bool clippedTop=a.y<.01f||a.y<=w.visibleTop+.01;
        const double sizeM=clippedTop||a.y+a.h>.99f?std::numeric_limits<double>::infinity():sizeDistance(r.category,ah);
        // A road user must stand on the road where its size says it is: a person printed on a
        // billboard, or anything raised above the road, has its "feet" far too high for its size.
        // Any unclipped box (Nov 4: the billboard figures were 15-49 % of the image tall); children
        // or people sitting differ from the size prior by well under the 2.2x allowed.
        if(w.horizonMeasured&&!footHidden&&std::isfinite(sizeM)&&(!std::isfinite(r.distanceM)||r.distanceM>2.2*sizeM||r.distanceM<sizeM/2.2))continue;
        // A "person" coming toward us faster than anyone walks or jogs (its own approach, after
        // removing ours, > 3.5 m/s) is a rider whose bike the detector missed (head-on riders
        // often show only the body): oncoming traffic, not a pedestrian to brake for.
        // (On two looks running: a walker's growth that is fast on one frame is noise, and called a
        // rider coming toward the learner they got "Oncoming two-wheeler. Keep left" - Nov 8, 15:56.)
        {
            const double before=motion&&motion->loomNowAt>=0&&a.captureTime>motion->loomNowAt&&a.captureTime-motion->loomNowAt<=.45?1/motion->loomNow:0;
            const double growth=std::min(r.expansion,before);
            if(r.category==0&&growth>0&&std::isfinite(sizeM)&&speedKnown&&sizeM*growth-std::max(0.,v)/3.6>3.5)r.category=3;
        }
        // ...and so is a "person" ahead in the lane travelling the learner's way faster than anyone
        // runs: a scooter rider seen from behind shows only the body (Nov 8, 6:34: "Person ahead"
        // for a rider). Someone standing D metres ahead grows by v*T/D in T seconds; this one's
        // growth over the last 0.8-1.3 s says it is moving away at 4.5 m/s (16 km/h) or more and at
        // least half the learner's speed. Only where a standing person's growth would be plain
        // (30 %: within about 25 m at 30 km/h) and close enough to measure (18 m); walkers and
        // joggers grow almost as someone standing does. People at the roadside are left as they are.
        if(r.category==0&&motion&&std::isfinite(sizeM)&&sizeM<=18&&speedKnown&&v>10&&ah>.05&&std::abs(r.center-.5)<.12) {
            int far=-1;
            for(size_t k=0;k<motion->count;++k){const double age=a.captureTime-motion->t[k];
                if(age>=.8&&age<=1.3&&motion->h[k]>0&&(far<0||age>a.captureTime-motion->t[size_t(far)]))far=int(k);}
            if(far>=0) {
                const double T=a.captureTime-motion->t[size_t(far)],mps=v/3.6,standing=mps*T/sizeM,grown=double(a.h)/motion->h[size_t(far)]-1;
                if(standing>=.3&&grown<=standing-std::max(4.5,.5*mps)*T/sizeM)r.category=3;
            }
        }
        // Far away the ground cue is fragile (a fraction of a degree of horizon error); where
        // both cues agree, their geometric mean is the distance.
        const double groundM=r.distanceM;
        if(std::isfinite(sizeM)&&std::isfinite(r.distanceM)&&r.distanceM>15)r.distanceM=std::sqrt(r.distanceM*sizeM);
        // Flow-based TTC only for road users close enough for it to be plausible.
        // "Close" is measured from the horizon: with the phone pitched up (Nov 8: horizon 0.70 of the
        // preview), a foot low in the image can still be 28 m away, where flow TTC is noise.
        if(std::isfinite(a.tti)&&a.tti>0&&(ah>.15||foot>.85))r.ttc=a.tti;
        // A small, distant box's growth is too noisy to time a collision for "Brake now" (Nov 4
        // and 6: far vehicles, a person 24 m ahead); it may still ask the learner to slow down.
        const double looming=r.expansion>.08?1/r.expansion:std::numeric_limits<double>::infinity();
        // ...and one frame's box a little larger than the last is not something closing: the growth
        // must show on two looks running (within 0.45 s). A bus followed at a steady 20-25 m got
        // "Vehicle ahead. Brake now" five times in a minute, each from a single frame (Nov 12).
        double sustained=std::numeric_limits<double>::infinity();
        if(motion) {
            if(a.captureTime>motion->loomNowAt){motion->loomPrev=motion->loomNow;motion->loomPrevAt=motion->loomNowAt;motion->loomNow=looming;motion->loomNowAt=a.captureTime;}
            if(motion->loomPrevAt>=0&&motion->loomNowAt-motion->loomPrevAt<=.45)sustained=std::max(motion->loomNow,motion->loomPrev);
        }
        if(ah>.12||foot>.75)r.ttc=std::min(r.ttc,sustained);
        const double slowTtc=ah>.05?std::min(r.ttc,sustained):r.ttc;
        const double edge=std::max(0.,std::abs(r.center-.5)-aw*.5),half=corridor(foot);
        // The learner's path in metres where the distance is known: the car's half-width (~0.85 m)
        // plus a margin, widening slowly for a road that bends. The image-fraction corridor was
        // ~1.8 m wide at 16 m, so people walking along the road edge were "in the path" (Nov 6).
        const bool metric=std::isfinite(r.distanceM)&&r.distanceM<60;
        const double edgeM=edge/.72*r.distanceM,lateralM=std::abs(r.center-.5)/.72*r.distanceM,pathM=1.1+.03*r.distanceM;
        r.inPath=metric?edgeM<pathM&&foot>.45:edge<half&&foot>.45;
        // Braking advice needs the road user itself in the path, not one edge of its box, and not
        // drifting out of it as the learner passes (Nov 4: a car parked at the roadside).
        const bool squarely=r.inPath&&(!metric||lateralM<pathM)&&!(r.rawInward<-.05&&std::abs(r.center-.5)>.12);
        if(!r.inPath&&r.inwardRate>.03)r.enterSeconds=metric?(edgeM-pathM)/(r.inwardRate/.72*r.distanceM):(edge-half)/r.inwardRate;
        // On a collision course: within about the car's own width of the path centre (a person's
        // centre within ~1 m, a vehicle's near edge within ~0.95 m). "Brake now" needs this (Nov 6:
        // a man walking toward us 1.4 m right of the centre line got "Brake now").
        const bool collision=squarely&&(!metric||(r.category==0?lateralM<1.:edgeM<.95));
        // Near enough to matter now: within ~6 s at this speed (an instructor names a road user
        // in the path well before braking is needed), at least ~25 m or 2.5 stopping distances.
        const double mps=(speedKnown?std::max(0.,v):15)/3.6,reach=std::max({25.,2.5*stopM,6*mps});
        const bool near=r.distanceM<=reach;
        // "Brake now" is for road users inside stopping distance plus a margin; a distant
        // box's noisy growth must not read as an imminent collision.
        // Two independent cues must agree before braking advice: ground contact below the
        // horizon (fragile near the horizon) and apparent size (unusable when clipped).
        const double confirmM=std::isfinite(sizeM)?std::max(r.distanceM,sizeM):r.distanceM;
        // (Firm braking is needed within the stopping distance and a little more: 45 m at 40 km/h,
        // 18 m at 20. It was half as far again; with the braking time above that is what made "Brake
        // now" the coach's commonest road-user sentence on Nov 12.)
        const bool brakeRange=confirmM<=1.2*stopM+4,slowRange=confirmM<=3*stopM+12;
        // 2. Classify the conflict.
        // Keep-left traffic: a vehicle right of the path centre that is closing fast
        // is oncoming traffic passing on the right, not a lead vehicle to brake for.
        const bool rightSide=r.center>.52&&!r.left;
        // Oncoming: the road user itself approaches (its closing speed from size and growth, beyond
        // the learner's own), ahead or on the right in keep-left traffic. A parked or slower car
        // being caught up is not oncoming (Nov 4); a rider head-on in the middle of a narrow road
        // is (Nov 6: it got "Brake now"). Without a measurable size: the image rule.
        const double ownApproach=speedKnown&&std::isfinite(sizeM)&&ah>.045?sizeM*r.expansion-std::max(0.,v)/3.6:std::numeric_limits<double>::quiet_NaN();
        // Only a clear approach speed decides (above +1.5 m/s oncoming, below -1.5 m/s being caught
        // up): up close the size cue is noisy, and then the image rule decides. Its own lane counts,
        // up to 3 m beyond the path (Nov 6: a scooter 3.9 m right lost its "keep left").
        const bool clear=std::isfinite(ownApproach)&&std::abs(ownApproach)>1.5;
        // How fast the gap to it is closing: its distance (by the ground where that is near enough to
        // trust, else by its size) times its growth. Something standing in the road closes at the
        // learner's own speed; a vehicle driving the learner's way, slower; one coming the other way,
        // much faster. Braking does not help with that last one, so it is never "Vehicle ahead.
        // Brake..."; and catching up a moving vehicle is a matter of the gap, not of stopping.
        // (From the growth seen on two looks running, as for braking: one noisy frame must not
        // decide that a vehicle standing in the road is coming toward the learner.)
        const double closing=std::isfinite(sustained)&&ah>.045?(std::isfinite(groundM)&&groundM<=25?groundM:std::isfinite(sizeM)?sizeM:groundM)/sustained:
            std::numeric_limits<double>::quiet_NaN();
        // Coming toward the learner: closing at least 60 % faster than their own speed (a vehicle
        // driving toward them at over half their speed). A distance off by a quarter, which a
        // horizon error of a degree makes at 25 m, must not turn a vehicle standing in the road into
        // one coming the other way.
        const bool approaching=r.category!=0&&speedKnown&&std::isfinite(closing)&&closing>std::max(mps+2.5,1.6*mps);
        const bool movingAhead=r.category!=0&&speedKnown&&v>15&&std::isfinite(closing)&&closing<.6*mps;
        const bool heavy=r.category==5||r.category==7,weak=vulnerable(r.category);
        // While the learner's own car is turning left (a measured 0.06 rad/s or more: a bend, or a
        // left turn at a junction) its path is an arc, not the line it points along now: at this
        // road user's distance the path lies R - sqrt(R^2 - d^2) to the left (R = speed / turn
        // rate), or never gets that far ahead at all. A vehicle that is already a metre or more to
        // the right of that arc and sliding right is traffic on the far side of the road ahead,
        // which will pass on the right: not something crossing (Nov 8, 11:35 and 12:07: "Vehicle
        // crossing. Brake now" for cars coming round a left-hand bend, 21-23 m ahead). One still
        // left of the arc is coming into the path: a truck crossing from the left 4 m ahead is
        // crossing whether or not the learner has begun to turn (Nov 4, 0:40, 3:54). A person is
        // never "oncoming".
        const double turn=w.yawKnown&&std::isfinite(w.yawRate)?meanYaw(now-1.,now):0;
        bool roundBend=false;
        if(r.category!=0&&r.left&&r.rawInward>.05&&turn>.06&&speedKnown&&v>5&&metric) {
            const double radius=mps/turn,arcLeftM=r.distanceM<radius?radius-std::sqrt(radius*radius-r.distanceM*r.distanceM):std::numeric_limits<double>::infinity();
            roundBend=(r.center-.5)/.72*r.distanceM+arcLeftM>1.;
        }
        // Where the learner's own path is at this road user's distance: it curves with the measured
        // turn rate (+ left of the line the car points along now). Something coming toward the learner
        // more than half a metre LEFT of that path is not traffic keeping to its own side: "keep left"
        // would steer toward it (Nov 8, 15:56: a man walking on the left edge of a bend, taken for a
        // rider, got "Oncoming two-wheeler. Keep left"). It is judged as what it is, in the path or
        // beside it.
        double pathLeftM=0;
        if(turn>.01&&speedKnown&&v>5&&metric){const double R=mps/turn;pathLeftM=r.distanceM<R?R-std::sqrt(R*R-r.distanceM*r.distanceM):R;}
        const bool leftOfPath=metric&&(r.center-.5)/.72*r.distanceM+pathLeftM<-.5;
        const bool oncoming=r.category!=0&&near&&v>5&&!leftOfPath&&(clear?
            ownApproach>1.5&&(roundBend?r.center>.35:r.inwardRate<.20&&r.center>.45)&&(metric?edgeM<pathM+3.:edge<half+.12):
            r.inwardRate<.20&&rightSide&&r.expansion>.25&&edge<half+.12);
        // Crossing: the actor's own lateral motion is TOWARD/through the path (not
        // the outward drift of something being passed).
        // ...and it must actually move toward the path in the image (compensation alone
        // must not turn a pedestrian walking along the edge into a "crossing").
        // ...and its box must grow as something on the road does while the learner approaches (at
        // least ~15 % of speed / distance above 12 km/h): a box that barely grows 12 m ahead at
        // 28 km/h is the detector switching between figures, not a road user (Nov 4: a billboard
        // "crossing").
        const bool growing=!(speedKnown&&v>12&&std::isfinite(r.distanceM)&&r.expansion<.15*(v/3.6)/r.distanceM);
        // "Brake now" only where firm braking is needed to stop short of it (stopping distance plus a
        // margin, by the farther of the two distance cues); farther away, "brake gently, be ready to
        // stop". Up to 0.8.2 the urgent range was twice the stopping distance (~78 m at 40 km/h):
        // seeing further (0.8.3) turned cars crossing junctions 50-80 m ahead into "Brake now" (Nov 8).
        if(!roundBend&&!fromAlongside&&near&&steady&&growing&&r.inwardRate>.10&&r.rawInward>.05&&r.expansion>-.1&&(r.inPath||r.enterSeconds<2.5)) {r.kind=RiskKind::Crossing;r.level=confirmM<=stopM+3?3:2;}
        else if(oncoming) {
            r.kind=RiskKind::Oncoming;
            // Worth saying: a bus or truck, which takes more than its lane and is named early, as far
            // off as it is seen; or a car or two-wheeler that will pass close, its near edge within
            // 2 m of the learner's line, judged within 50 m where that can be measured. Ordinary
            // traffic keeping to the other lane of a road wide enough for both needs no words: on
            // Nov 12 it was a third of everything the coach said (27 of 84 sentences).
            const bool tight=metric&&r.distanceM<=50&&edgeM<2.;
            // Head-on, close and closing: urgent "keep left" (Nov 6: a rider in the middle of a
            // narrow road). Only where that can be measured: at 60 m a vehicle in the other lane
            // and one in the learner's own are a couple of pixels apart.
            const bool headOn=collision&&metric&&r.distanceM<=35&&r.ttc<2.5;
            // About to pass (its box growing as something under 1.5 s away does): it will be gone
            // before "keep left" can be heard.
            const bool passing=r.expansion>1/1.5;
            r.level=headOn?3:(heavy||tight)&&!passing?2:0;
        }
        // Heavy vehicles stop and swerve slowly: give them extra margin; people and riders, more.
        // ...unless the learner is already braking hard enough to stop short of it
        // (needed deceleration for a stationary hazard at this TTC: v / (2*TTC)).
        // ("Brake now" is never held back by a judgement of how fast the gap closes: on a collision
        // course, within range and that close in time, braking is right whatever is coming.)
        else if(collision&&near&&brakeRange&&r.ttc<brakeBy+(weak?1.:.6)+(heavy?.5:0)&&v>5&&!(decel>.5&&decel>=.9*(v/3.6)/(2*r.ttc)))
            {r.kind=RiskKind::InPath;r.level=3;}
        // "Brake gently, be ready to stop", early, for what stands in the road or is weak (as far
        // ahead as before); for a vehicle moving the learner's way only when the gap is closing fast.
        else if(!approaching&&squarely&&near&&slowRange&&slowTtc<(movingAhead?brakeBy+1.:stopping+2.5)+(heavy?1:0)&&v!=0) {r.kind=RiskKind::InPath;r.level=2;}
        // Crawling up to a vulnerable road user right in front: be ready to stop.
        else if(squarely&&foot>.62&&vulnerable(r.category)&&v>3&&v<15) {r.kind=RiskKind::InPath;r.level=2;}
        // A person at the edge of the path or beside it: give them space.
        else if(r.category==0&&!squarely&&(metric?edgeM<pathM+1.5:edge<half+.15)&&r.distanceM<=std::max(24.,5*mps)&&foot>.52&&v>12) {r.kind=RiskKind::Roadside;r.level=1;}
        else if(r.inPath&&r.distanceM<=std::max(15.,.8*reach)&&foot>.52&&v>5) {r.kind=RiskKind::InPath;r.level=1;}
        else if(r.inPath&&foot>.58&&!speedKnown) {r.kind=RiskKind::InPath;r.level=1;} // Awareness only (see actionFor).
        // About to be passed: a pedestrian the car is closing on (the box growing as fast as
        // something under 2.5 s away does) and less than 1.2 s ahead at this speed, or under 1.5 s
        // by that growth, who is not on a collision course, will be behind the car before a
        // sentence about them can be heard (Nov 8, 8:38: "Person ahead. Brake gently. Be ready to
        // stop." for a man on the verge 5 m ahead). Someone keeping pace ahead is not being passed.
        if(r.category==0&&r.level>0&&r.level<3&&speedKnown&&v>15&&r.expansion>.4&&(r.distanceM<1.2*mps||r.expansion>1/1.5))r.level=0;
        // Temporal confirmation: a road user must be seen consistently before it can
        // drive an instruction (single-frame flickers of the mount or reflections cannot).
        double span=0;if(motion)for(size_t k=0;k<motion->count;++k)span=std::max(span,a.captureTime-motion->t[k]);
        const size_t seen=motion?motion->count:0;
        if(r.level>=3?seen<2:(seen<3||span<.3))r.level=0;
        r.score=r.level*10+vulnerability(r.category)+2*std::clamp(r.foot,0.,1.)+(std::isfinite(r.ttc)?std::min(5.,5/r.ttc):0);
        risks_[count_++]=r;
        if(auto* e=episode(r.id,now)){e->lastSeen=now;e->kind=r.kind;e->group=group(r.category);if(r.kind==RiskKind::Crossing)e->crossing=true;
            if(e->crossing&&r.kind!=RiskKind::Crossing&&!r.inPath&&e->crossedOutAt<0)e->crossedOutAt=now;
            // "You first": the coach notices a road user in or beside the path up to ~8 s ahead, well
            // before an instruction is due, and from then watches whether the learner's speed falls
            // before anything is said about it.
            const bool soon=speedKnown&&v>5&&std::isfinite(r.distanceM)&&r.distanceM<=8*mps&&
                (r.inPath||oncoming||(metric?edgeM<pathM+1.5:edge<half+.15));
            if(soon&&e->noticedAt<0){e->noticedAt=now;e->skill=skillOf(r);e->dueM=std::max(15.,.8*reach);}
            if(e->noticedAt>=0)watch(*e);
            if(r.level>=1)e->skill=skillOf(r);
            // The record counts a road user once an instruction about it is said, shortened or left
            // unsaid (below). Without any instruction it counts one case only: the learner, having
            // slowed, has stopped short of a road user in their path, inside the distance at which
            // an instruction would have come at the speed they had when the coach noticed it. Once
            // per stop: a queue of three is one stop. (Counting every road user near the path while
            // the speed happened to be falling credited 22 "slowed first" in the 4.7 minutes of Nov 6.)
            if(e->before&&!e->situation&&speedKnown&&v<=3&&r.inPath&&r.distanceM<=e->dueM&&(stopCredit_<0||now-stopCredit_>10)){e->situation=true;stopCredit_=now;}}
    }
    // 3. What has already been said about each road user (ledger, not guesses).
    for(auto& e:episodes_)if(e.id) {
        for(const auto& ev:ledger.entries())if(ev.used&&ev.sourceId==source&&ev.identity.space==IdentitySpace::Optical&&
            std::find(e.optical.begin(),e.optical.end(),ev.identity.id)!=e.optical.end()&&ev.acceptedAt>=0&&
            ev.acceptedAt>e.spokenAt&&levelOf(ev.acceptedPrompt)>=0){if(e.firstSpokenAt<0)e.firstSpokenAt=ev.acceptedAt;if(ev.identity.id)e.situation=true;e.spokenKind=e.kind;e.spokenGroup=e.group;e.spokenAt=ev.acceptedAt;e.spokenPrompt=int(ev.acceptedPrompt);
            // ("Brake now" is the urgent form of "brake gently": the same instruction id, said as an approach warning.)
            e.spokenLevel=std::max(e.spokenLevel,ev.kind==EventKind::Approach?3:levelOf(ev.acceptedPrompt));}
        // A crossing actor that left the corridor while the learner waits.
        bool visible=false;for(size_t i=0;i<count_;++i)if(risks_[i].id==e.id){visible=true;if(risks_[i].kind!=RiskKind::Crossing&&!risks_[i].inPath&&e.crossing&&e.crossedOutAt<0)e.crossedOutAt=now;}
        if(!visible&&e.crossing&&e.crossedOutAt<0)e.crossedOutAt=now;
    }
    // 3b. What the learner's speed did about each road user the coach had something to say about.
    // Recorded once, when the road user has gone, 5 s after the instruction, or at the stop.
    for(auto& e:episodes_)if(e.id&&e.situation&&!e.counted) {
        if(e.spokenAt>=0&&!e.measured&&speedKnown&&now-e.spokenAt<1)e.measured=true;
        if(e.spokenAt>=0&&speedKnown&&now-e.spokenAt>=.8&&now-e.spokenAt<=5&&memory.speedTrend(now,e.spokenAt)==SpeedTrend::Decreasing)e.after=true;
        // ...or when the learner, having slowed before any instruction, has stopped short of it.
        const bool gone=now-e.lastSeen>1.5,settled=e.spokenAt>=0&&now-e.spokenAt>5,stopped=e.before&&e.spokenAt<0&&speedKnown&&v<=3;
        if(!gone&&!settled&&!stopped)continue;
        e.counted=true;
        if(!learner||e.skill<0)continue;
        if(e.before)learner->note(e.skill,LearnerOutcome::Before);
        else if(e.spokenAt>=0&&e.after)learner->note(e.skill,LearnerOutcome::After);
        // "No slowing seen" needs a measured speed throughout and time for the learner to act
        // (counted from the first instruction: a later, stronger one must not hide the first).
        else if(e.spokenAt>=0&&e.measured&&speedKnown&&now-e.firstSpokenAt>=2.5)learner->note(e.skill,LearnerOutcome::NoResponse);
    }
    // 4. Rank: the most dangerous road user wins; then decide whether to speak.
    for(size_t i=0;i<count_;++i)if(risks_[i].level&&(!top_.level||risks_[i].score>top_.score))top_=risks_[i];
    // ...but once everything has been said about someone passing by (oncoming, or at the roadside),
    // a road user in the way who has not been spoken about comes first, although the one passing
    // is nearer or closing faster (Nov 8, 2:42: a person in the lane waited behind an oncoming rider).
    if(top_.level&&(top_.kind==RiskKind::Oncoming||top_.kind==RiskKind::Roadside)) {
        const auto said=[&](const ActorRisk& r){for(const auto& e:episodes_)if(e.id==r.id)return e.spokenLevel>=r.level;return false;};
        if(said(top_)) {
            ActorRisk next{};
            for(size_t i=0;i<count_;++i){const auto& r=risks_[i];
                if(r.level<2||r.id==top_.id||(r.kind!=RiskKind::InPath&&r.kind!=RiskKind::Crossing)||said(r))continue;
                if(!next.level||r.score>next.score)next=r;}
            if(next.level)top_=next;
        }
    }
    // Minimal still speaks for a vulnerable road user who is closing on the path.
    const bool vulnerableClosing=vulnerable(top_.category)&&top_.inPath&&std::isfinite(top_.ttc)&&top_.ttc<10;
    const int minimum=support>=1||vulnerableClosing?1:2;
    // One risk instruction at a time: within 3 s of the last one, only a higher risk speaks.
    double lastAt=-1;int lastLevel=0,lastGroup=-1;uint64_t lastId=0;RiskKind lastKind=RiskKind::None;
    for(const auto& e:episodes_)if(e.id&&e.spokenAt>lastAt){lastAt=e.spokenAt;lastLevel=e.spokenLevel;lastId=e.id;lastKind=e.spokenKind;lastGroup=e.spokenGroup;}
    // The next one of a group: the same kind of road user in the same kind of conflict, no worse,
    // within 8 s of the instruction about the first. Saying the same sentence again for each
    // person standing together, or each truck in a line, adds nothing (Nov 8, 8:29-8:38: "Person
    // ahead. Brake gently. Be ready to stop." three times).
    // A person and a rider count as alike here: the instruction is the same, and people standing
    // by a parked motorcycle are "person" in one frame and "rider" in the next (Nov 8, 8:33). After
    // "Brake now" the window is the 3 s of the sentence itself: someone else stepping out later is
    // a new emergency.
    const auto alike=[](int a,int b){return a==b||(a>=0&&b>=0&&a<=1&&b<=1);};
    const bool sameAgain=lastAt>=0&&now-lastAt<(top_.level>=3?3.:8.)&&top_.id!=lastId&&top_.kind==lastKind&&alike(group(top_.category),lastGroup)&&top_.level<=lastLevel;
    // ...except that a person or two-wheeler in the path who needs braking for is not kept waiting
    // behind what was just said about someone else passing by or at the roadside (Nov 8, 2:42:
    // "Oncoming two-wheeler. Keep left" held "Person ahead" back until it was "Brake now" at 10 m).
    const bool pathFirst=top_.kind==RiskKind::InPath&&top_.level>=2&&vulnerable(top_.category)&&top_.id!=lastId&&
        (lastKind==RiskKind::Oncoming||lastKind==RiskKind::Roadside)&&now-lastAt>=1.5;
    // Within 1.5 s of the last instruction only an urgent (level 3) risk may cut in.
    const bool settling=sameAgain||(lastAt>=0&&!pathFirst&&((now-lastAt<3&&top_.level<=lastLevel)||(now-lastAt<1.5&&top_.level<3)));
    // "Vehicle ahead, keep a gap" for non-closing traffic at most every 15 s.
    const bool gapCue=top_.kind==RiskKind::InPath&&top_.level==1&&!vulnerable(top_.category)&&!(std::isfinite(top_.ttc)&&top_.ttc<10);
    const bool gapRecent=gapCue&&lastGapCue_>=0&&now-lastGapCue_<15;
    // "Keep left" for passing traffic is for the occasional close car, not every car on a busy road
    // (Nov 4: seven in 75 s): a routine one waits 20 s; a bus, a truck or an urgent one still speaks.
    // (0.8.6: every 30 s for a car or two-wheeler passing close, every 20 s for buses and trucks,
    // which used to be exempt: three "Oncoming truck. Keep left" in 18 s on Nov 12.)
    const bool oncomingCue=top_.kind==RiskKind::Oncoming&&top_.level<3;
    const bool oncomingRecent=oncomingCue&&lastOncomingCue_>=0&&now-lastOncomingCue_<(top_.category==5||top_.category==7?20:30);
    if(!settling&&!gapRecent&&!oncomingRecent&&top_.level>=minimum&&!(support==1&&top_.level==1&&!vulnerable(top_.category))) {
        auto* e=episode(top_.id,now);
        // Give the learner time to respond to what was just said about this road
        // user before escalating, unless contact is truly imminent.
        const bool reacting=e->spokenAt>=0&&now-e->spokenAt<2&&!(std::isfinite(top_.ttc)&&top_.ttc<1.5);
        // Rising to the very same words about the same road user within 3 s adds nothing: it would
        // only cut off the sentence just started (Nov 5: "Left turn ahead. Oncoming vehicle..." then
        // "Oncoming vehicle. Keep left..." 0.1 s later).
        // Rising to urgent is never "the same words": "Brake now" after "Brake gently" is the warning
        // that the gentle braking is not enough (on the instruction id they are the same).
        const bool sameWords=e->spokenAt>=0&&now-e->spokenAt<3&&e->spokenPrompt==int(actionFor(top_,v,speedKnown,top_.level>=3))&&
            !(top_.level>=3&&e->spokenLevel<3);
        // ...or the conflict is now of another kind: someone spoken about as passing by (oncoming,
        // at the roadside) who is now in the way needs the other instruction, at the same level.
        const bool turnedInto=e->spokenAt>=0&&top_.level>=2&&top_.level==e->spokenLevel&&e->spokenKind!=top_.kind&&
            (e->spokenKind==RiskKind::Oncoming||e->spokenKind==RiskKind::Roadside)&&(top_.kind==RiskKind::InPath||top_.kind==RiskKind::Crossing);
        const bool rising=(top_.level>e->spokenLevel||turnedInto)&&!reacting&&!sameWords;
        // Spoken already at this level: escalate once if the learner is not slowing. Not for
        // traffic coming the other way: what was asked is to keep left, which speed does not show,
        // and a steady speed past an oncoming car is no reason to say it again, urgently (a
        // head-on collision course still rises to urgent by itself).
        const bool unanswered=e->spokenLevel>=2&&top_.level>=2&&top_.kind!=RiskKind::Oncoming&&e->spokenAt>=0&&now-e->spokenAt>=2.5&&now-e->spokenAt<=8&&
            speedKnown&&v>5&&memory.speedTrend(now,e->spokenAt)!=SpeedTrend::Decreasing&&e->spokenLevel<3;
        // "You first" (0.8.5). An instruction about a road user is due now (the first, or a stronger
        // one), which is about the last comfortable moment for it, so nothing is ever delayed. What
        // changes, for a learner whose record shows they respond, is what is said once their
        // measured speed shows the slowing is already done:
        //  - a record of slowing when told (level 1): the instruction without its slowing part
        //    ("Oncoming bus. Keep left." - what speed cannot show is still said);
        //  - a record of often slowing before being told (level 2): nothing is said, and the
        //    screen shows that the coach has seen the road user. A bus or truck coming the other
        //    way still gets the short instruction.
        // A learner who has not slowed is told in full, exactly as a new learner is, and so is one
        // who stops slowing while it still matters. Never for "Brake now", a crossing road user,
        // or without a measured speed.
        bool wait=false,brief=false;
        if(rising) {
            if(e->spokenAt<0){if(e->dueAt<0)e->dueAt=now;if(e->noticedAt<0)e->noticedAt=now;watch(*e);}
            const int trust=learner&&e->skill>=0?learner->waitLevel(e->skill):0;
            if(trust>=1&&speedKnown&&v>5&&top_.level<=2&&top_.kind!=RiskKind::Crossing) {
                // The slowing is done when:
                //  - "ease off" (and oncoming traffic, which is not stopped for): their speed has
                //    fallen since the coach noticed this road user and has stayed down;
                //  - "brake gently, be ready to stop": it is falling now, fast enough that this
                //    braking alone stops the car 5 m short of the road user, or they are crawling
                //    and not speeding up. Braking that eases off is answered with the instruction.
                const double mpsNow=std::max(.5,v/3.6);
                const double needed=std::isfinite(top_.distanceM)?mpsNow*mpsNow/(2*std::max(2.,top_.distanceM-5)):std::numeric_limits<double>::infinity();
                const bool eased=e->spokenAt<0&&e->before;
                const bool braking=slowing&&(decel>=needed||(v<10&&decel>=0))&&(e->spokenAt>=0||e->before);
                const bool acted=top_.level==1||top_.kind==RiskKind::Oncoming?eased:braking;
                const bool heavyOncoming=top_.kind==RiskKind::Oncoming&&(top_.category==5||top_.category==7);
                wait=acted&&trust>=2&&!heavyOncoming;
                brief=acted&&!wait;
            }
            if(wait){hold_={e->skill,slowing,e->dueAt>=0?e->dueAt:now,now};e->situation=true;}
        }
        if((rising||unanswered)&&!wait) {
            brief_=brief;
            ActorRisk speak=top_;if(unanswered&&!rising)speak.level=3;
            urgent_=speak.level>=3&&speak.kind!=RiskKind::Crossing?v>5:speak.level>=3&&v>8;
            // Speak about the current detection; remember it under this road user's identity.
            uint64_t optical=0;for(const auto& m:motions_)if(m.id==top_.id){optical=m.optical;break;}
            if(std::find(e->optical.begin(),e->optical.end(),optical)==e->optical.end()){e->optical[e->opticalCursor]=optical;e->opticalCursor=(e->opticalCursor+1)%e->optical.size();}
            proposal_=int(actionFor(speak,v,speedKnown,urgent_));proposalId_=optical;proposalCapture_=w.captureTime;
            proposalLevel_=speak.level;proposalClosing_=std::isfinite(speak.ttc)&&speak.ttc<10;
            proposalPathFirst_=speak.kind==RiskKind::InPath&&speak.level>=2&&vulnerable(speak.category);
            if(gapCue)lastGapCue_=now;
            if(speak.kind==RiskKind::Oncoming)lastOncomingCue_=now;
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
    c.brief=brief_;c.pathFirst=proposalPathFirst_;
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
    o<<",\"distance_m\":"<<(std::isfinite(top_.distanceM)?top_.distanceM:-1)<<",\"foot\":"<<top_.foot<<",\"center\":"<<top_.center<<",\"expansion\":"<<top_.expansion<<",\"inward\":"<<top_.inwardRate
    <<",\"metric_distance\":\"UNAVAILABLE\"},\"spoken_by_kind\":["<<counts_[1]<<','<<counts_[2]<<','<<counts_[3]<<','<<counts_[4]<<','<<counts_[5]<<"]"
    // "You first": the kind of situation whose instruction is left unsaid this tick (-1 none),
    // whether the learner's speed is falling, and whether the proposed instruction is the short form.
    <<",\"heading_x\":";if(headingKnown())o<<heading_.x;else o<<"null";
    o<<",\"heading_samples\":["<<heading_.motionCount<<','<<heading_.farCount<<"]"<<",\"heading_cues\":[";
    if(heading_.byMotion==heading_.byMotion)o<<heading_.byMotion;else o<<"null";o<<',';
    if(heading_.byFar==heading_.byFar)o<<heading_.byFar;else o<<"null";o<<"]"
    <<",\"held_kind\":"<<hold_.skill<<",\"held_slowing\":"<<(hold_.responding?"true":"false")<<",\"brief\":"<<(brief_?"true":"false")<<"}";return o.str();
}
}
