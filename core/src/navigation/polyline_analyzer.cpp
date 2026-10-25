#include "learndrive/navigation.hpp"
#include <algorithm>
#include <cmath>

namespace ld::navigation {
namespace {
constexpr double earth=6371008.8,rad=pi/180;
bool coordinate(double lat,double lon) {return std::isfinite(lat)&&std::isfinite(lon)&&std::abs(lat)<=85&&std::abs(lon)<=180;}
double wrap(double d) {return std::remainder(d,360.);}
Feature maneuver(int t) {
    switch(t) {
    case 9:case 10:case 11:return Feature::rightTurn;
    case 14:case 15:case 16:return Feature::leftTurn;
    // U-turns, ramps, exits, roundabouts and merges need their own policies.
    // Do not mislabel them as ordinary junctions.
    default:return Feature::none;
    }
}
}
void RouteCoach::clear() noexcept {points_.clear();events_.clear();steps_.clear();routeId_=0;invalidateFix();match_=Match::noRoute;}
void RouteCoach::invalidateFix() noexcept {
    fix_={};matched_=false;consistent_=0;progress_=0;lastFix_=acquiringSince_=-infinity;pending_=0;
    match_=routeId_?Match::noFix:Match::noRoute;
}
RouteCoach::Point RouteCoach::pointAt(double s) const noexcept {
    auto it=std::lower_bound(points_.begin(),points_.end(),s,[](const Point& p,double v){return p.s<v;});
    if(it==points_.begin())return *it;
    if(it==points_.end())return points_.back();
    const auto& a=*(it-1);double f=(s-a.s)/(it->s-a.s);
    return {a.x+(it->x-a.x)*f,a.y+(it->y-a.y)*f,s};
}
bool RouteCoach::load(uint64_t id,const Coordinate* coords,size_t count,const Maneuver* turns,size_t turnCount) {
    clear();
    if(!id||!coords||count<2||count>100000||turnCount>10000||(!turns&&turnCount))return false;
    latitude0_=coords[0].latitude;longitude0_=coords[0].longitude;cosLatitude_=std::cos(latitude0_*rad);
    points_.reserve(count);double total=0;
    // Routers can repeat a vertex (including the destination). Keep the original
    // shape-index distances for maneuver metadata, but no zero-length segments
    // in the hot matcher or tangent interpolation.
    std::vector<double> shapeDistances;shapeDistances.reserve(count);
    for(size_t i=0;i<count;++i) {
        if(!coordinate(coords[i].latitude,coords[i].longitude)){clear();return false;}
        Point p{wrap(coords[i].longitude-longitude0_)*rad*earth*cosLatitude_,(coords[i].latitude-latitude0_)*rad*earth,0};
        if(!points_.empty()) {
            const auto& a=points_.back();double d=std::hypot(p.x-a.x,p.y-a.y);
            // Reject broken/discontinuous shapes, not merely unknown coverage.
            if(d>2000){clear();return false;}
            if(d<.05){shapeDistances.push_back(total);continue;}
            total+=d;
        }
        p.s=total;points_.push_back(p);shapeDistances.push_back(total);
    }
    if(total<20||total>1000000){clear();return false;}
    for(size_t i=0;i<turnCount;++i) {
        if(turns[i].shapeIndex>=count||turns[i].type<0||turns[i].type>100){clear();return false;}
        steps_.push_back({shapeDistances[turns[i].shapeIndex],turns[i].type,int(turns[i].shapeIndex)});
        auto kind=maneuver(turns[i].type);
        if(kind!=Feature::none)events_.push_back({shapeDistances[turns[i].shapeIndex],0,infinity,kind,0,false});
    }
    std::sort(steps_.begin(),steps_.end(),[](const Step& a,const Step& b){return a.s<b.s;});
    // Compare 20 m tangents across a 40 m neighborhood, sampled every 5 m.
    // Unlike a raw vertex-angle test, this does not change when a straight
    // segment is subdivided or when a heading crosses north/360 degrees.
    for(double s=20;s+20<total;s+=5) {
        const auto a=pointAt(s-20),b=pointAt(s),c=pointAt(s+20);
        const double ab=std::hypot(b.x-a.x,b.y-a.y),bc=std::hypot(c.x-b.x,c.y-b.y);
        if(ab<5||bc<5)continue;
        const double angle=wrap((std::atan2(c.x-b.x,c.y-b.y)-std::atan2(b.x-a.x,b.y-a.y))/rad);
        if(std::abs(angle)<=35||std::abs(angle)>110)continue;
        // Maneuver metadata owns its neighborhood: a turn is not also a bend.
        bool junction=false;
        const auto nearby=std::lower_bound(steps_.begin(),steps_.end(),s-40,[](const Step& step,double distance){return step.s<distance;});
        for(auto step=nearby;step!=steps_.end()&&step->s<s+40;++step)if(step->type>=8){junction=true;break;}
        if(junction)continue;
        const auto kind=angle>0?Feature::rightBend:Feature::leftBend;
        if(!events_.empty()&&events_.back().kind==kind&&s-events_.back().s<35) {
            if(std::abs(angle)>std::abs(events_.back().angle))events_.back()={s,angle,40/(std::abs(angle)*rad),kind,0,false};
        } else events_.push_back({s,angle,40/(std::abs(angle)*rad),kind,0,false});
    }
    std::sort(events_.begin(),events_.end(),[](const Event& a,const Event& b){return a.s<b.s;});
    for(size_t i=0;i<events_.size();++i)events_[i].id=i+1;
    routeId_=id;invalidateFix();return true;
}
bool RouteCoach::fix(const Fix& f) noexcept {
    if(!routeId_||!std::isfinite(f.time)||f.time<0||f.time<=lastFix_)return false;
    const double previousTime=lastFix_;lastFix_=f.time;fix_=f;pending_=0;
    if(!coordinate(f.latitude,f.longitude)||!std::isfinite(f.accuracyM)||f.accuracyM<0||f.accuracyM>20) {
        match_=Match::poorFix;matched_=false;consistent_=0;return false;
    }
    const double x=wrap(f.longitude-longitude0_)*rad*earth*cosLatitude_,y=(f.latitude-latitude0_)*rad*earth;
    double best=infinity,second=infinity,bestS=0,heading=0,secondS=0;
    size_t begin=0,end=points_.size()-1;
    if(matched_) {
        const auto lo=std::lower_bound(points_.begin(),points_.end(),std::max(0.,progress_-50),[](const Point& p,double s){return p.s<s;});
        begin=lo==points_.begin()?0:size_t(lo-points_.begin()-1);
        const auto hi=std::upper_bound(points_.begin(),points_.end(),progress_+750,[](double s,const Point& p){return s<p.s;});
        end=std::min(points_.size()-1,size_t(hi-points_.begin()));
    }
    for(size_t i=begin;i<end;++i) {
        const auto a=points_[i],b=points_[i+1];double dx=b.x-a.x,dy=b.y-a.y;
        const double u=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
        const double d=std::hypot(x-a.x-u*dx,y-a.y-u*dy),s=a.s+u*(b.s-a.s);
        if(d<best) {
            if(std::abs(s-bestS)>60){second=best;secondS=bestS;}
            best=d;bestS=s;heading=std::atan2(dx,dy)/rad;
        } else if(std::abs(s-bestS)>60&&d<second){second=d;secondS=s;}
    }
    if(best>std::clamp(f.accuracyM*1.5,12.,25.)) {match_=Match::offRoute;matched_=false;consistent_=0;return false;}
    if(second<best+std::max(5.,f.accuracyM)&&std::abs(secondS-bestS)>60) {
        match_=Match::ambiguous;matched_=false;consistent_=0;return false;
    }
    if((f.speedValid&&f.speedKmh>8&&(!f.bearingValid||!std::isfinite(f.bearingDegrees)||std::abs(wrap(f.bearingDegrees-heading))>60))||
       (matched_&&(bestS<progress_-15||bestS-progress_>std::max(60.,(f.time-previousTime)*55+2*f.accuracyM)))) {
        match_=Match::ambiguous;matched_=false;consistent_=0;return false;
    }
    if(!matched_||f.time-previousTime>3){consistent_=1;acquiringSince_=f.time;}
    else ++consistent_;
    progress_=bestS;matched_=true;
    match_=consistent_>=2&&f.time-acquiringSince_>=.5?Match::ready:Match::acquiring;
    if(points_.back().s-progress_<15)match_=Match::arrived;
    return true;
}
MacroContext RouteCoach::context(double now) const noexcept {
    MacroContext out;out.routeId=routeId_;out.match=match_;out.fixTime=fix_.time;out.progressM=progress_;
    out.gpsSpeedKmh=fix_.speedValid?fix_.speedKmh:infinity;out.speedAccuracyMps=fix_.speedAccuracyMps;out.horizontalAccuracyM=fix_.accuracyM;
    if(!routeId_)return out;
    if(match_==Match::noFix)return out;
    if(!std::isfinite(now)||now<fix_.time||now-fix_.time>2.5) {out.match=Match::stale;return out;}
    if(match_!=Match::ready)return out;
    out.valid=true;
    const auto step=std::lower_bound(steps_.begin(),steps_.end(),progress_+5,[](const Step& e,double s){return e.s<s;});
    if(step!=steps_.end()){out.maneuverType=step->type;out.maneuverShapeIndex=step->shapeIndex;out.maneuverDistanceM=step->s-progress_;}
    const auto it=std::lower_bound(events_.begin(),events_.end(),progress_+5,[](const Event& e,double s){return e.s<s;});
    for(auto e=it;e!=events_.end()&&e->s-progress_<=200;++e) {
        if(e->spoken)continue;
        out.feature=e->kind;out.featureId=e->id;out.distanceM=e->s-progress_;
        out.headingChangeDegrees=e->angle;out.estimatedRadiusM=e->radius;break;
    }
    return out;
}
Prompt RouteCoach::evaluate(double now,bool braking,bool shock) const noexcept {
    pending_=0;const auto c=context(now);
    if(!c.valid||braking||shock||now-lastAccepted_<8||!fix_.speedValid||!std::isfinite(fix_.speedKmh)||
       fix_.speedKmh<0||fix_.speedKmh>160||!std::isfinite(fix_.speedAccuracyMps)||fix_.speedAccuracyMps>2)return Prompt::none;
    const bool bend=c.feature==Feature::leftBend||c.feature==Feature::rightBend;
    const double maximum=bend?100.:50.;
    // A prompt must finish before the event; these are provisional timing gates,
    // not a calculation of a safe speed or a required driving action.
    if(c.feature==Feature::none||c.distanceM>maximum||c.distanceM<fix_.speedKmh/3.6*3.5||fix_.speedKmh<=(bend?30:3))return Prompt::none;
    pending_=c.featureId;
    switch(c.feature) {
    case Feature::leftBend:return Prompt::mapLeftBend;
    case Feature::rightBend:return Prompt::mapRightBend;
    case Feature::leftTurn:
        if(c.distanceM>=8&&c.distanceM<=12&&fix_.accuracyM<=3)return Prompt::mapLeftTurnNear;
        if(c.distanceM>=40&&c.distanceM<=50&&fix_.accuracyM<=5)return Prompt::mapLeftTurn50;
        return Prompt::mapLeftTurn;
    case Feature::rightTurn:
        if(c.distanceM>=8&&c.distanceM<=12&&fix_.accuracyM<=3)return Prompt::mapRightTurnNear;
        if(c.distanceM>=40&&c.distanceM<=50&&fix_.accuracyM<=5)return Prompt::mapRightTurn50;
        return Prompt::mapRightTurn;
    default:return Prompt::mapJunction;
    }
}
void RouteCoach::accepted(double now) noexcept {
    if(!pending_||!std::isfinite(now)||now<lastAccepted_)return;
    for(auto& e:events_)if(e.id==pending_){e.spoken=true;lastAccepted_=now;break;}
    pending_=0;
}
const char* RouteCoach::status(Match m) noexcept {
    switch(m) {
    case Match::noRoute:return "No offline route selected";
    case Match::noFix:return "Waiting for GPS position";
    case Match::poorFix:return "GPS accuracy insufficient; route coaching paused";
    case Match::offRoute:return "Off route; route coaching paused";
    case Match::ambiguous:return "Route position uncertain; coaching paused";
    case Match::acquiring:return "Confirming route position";
    case Match::ready:return "Offline route position confirmed";
    case Match::stale:return "GPS position stale; route coaching paused";
    case Match::arrived:return "Near route end; check your destination";
    }
    return "Route unavailable";
}

bool ProviderTurnCoach::update(const TurnInput& next) noexcept {
    if(next.provider<=0||!next.session||!next.routeEpoch||!next.sequence||
       !std::isfinite(next.receivedAt)||next.receivedAt<0||
       int(next.state)<0||int(next.state)>4||int(next.kind)<0||int(next.kind)>9||next.step< -1||
       std::isnan(next.remainingM)||next.remainingM<0||std::isnan(next.remainingSeconds)||next.remainingSeconds<0)return false;
    if(next.session<input_.session||next.receivedAt<input_.receivedAt)return false;
    if(next.session==input_.session&&(retired_||next.provider!=input_.provider||
       next.routeEpoch<input_.routeEpoch||next.sequence<=input_.sequence))return false;
    // An older step from a delayed callback must not bring back a passed turn.
    // A reroute that renumbers steps must advance the route epoch.
    if(next.session==input_.session&&next.routeEpoch==input_.routeEpoch&&
       next.state==TurnState::navigating&&next.step>=0&&next.step<highestStep_)return false;
    const bool changed=next.session!=input_.session||next.routeEpoch!=input_.routeEpoch||
        next.step!=input_.step||next.kind!=input_.kind;
    if(next.session!=input_.session||next.routeEpoch!=input_.routeEpoch)highestStep_=-1;
    if(next.state==TurnState::navigating&&next.step>=0)highestStep_=std::max(highestStep_,next.step);
    if(changed)++eventId_;
    input_=next;active_=true;available_=true;retired_=false;pending_=0;return true;
}
void ProviderTurnCoach::clear() noexcept {
    active_=false;available_=false;retired_=input_.session!=0;pending_=0;
    // Keep session/sequence tombstones and cooldown so late callbacks cannot
    // reactivate a cleared journey or use a provider switch to repeat speech.
}
void ProviderTurnCoach::invalidateFix() noexcept {
    fix_={};consistent_=0;lastFix_=acquiringSince_=-infinity;pending_=0;
}
bool ProviderTurnCoach::fix(const Fix& f) noexcept {
    if(!std::isfinite(f.time)||f.time<0||f.time<=lastFix_)return false;
    const double previous=lastFix_;lastFix_=f.time;fix_=f;pending_=0;
    if(!coordinate(f.latitude,f.longitude)||!std::isfinite(f.accuracyM)||f.accuracyM<0||f.accuracyM>15||
       !f.speedValid||!std::isfinite(f.speedKmh)||f.speedKmh<0||f.speedKmh>160||
       !std::isfinite(f.speedAccuracyMps)||f.speedAccuracyMps<0||f.speedAccuracyMps>2) {
        consistent_=0;acquiringSince_=-infinity;return false;
    }
    if(!consistent_||f.time-previous>2.5){consistent_=1;acquiringSince_=f.time;}
    else ++consistent_;
    return true;
}
MacroContext ProviderTurnCoach::context(double now) const noexcept {
    MacroContext out;out.routeId=active_?input_.session:0;out.featureId=eventId_;out.progressM=infinity;
    out.fixTime=fix_.time;out.gpsSpeedKmh=fix_.speedValid?fix_.speedKmh:infinity;
    out.speedAccuracyMps=fix_.speedAccuracyMps;out.horizontalAccuracyM=fix_.accuracyM;
    if(!active_)return out;
    out.match=Match::noFix;
    if(!available_)return out;
    if(!std::isfinite(now)||now<input_.receivedAt||now-input_.receivedAt>3||
       now<fix_.time||now-fix_.time>2.5){out.match=Match::stale;return out;}
    if(input_.state==TurnState::arrived){out.match=Match::arrived;return out;}
    if(input_.state!=TurnState::navigating||input_.step<0)return out;
    if(consistent_<2||fix_.time-acquiringSince_<.5){out.match=consistent_?Match::acquiring:Match::poorFix;return out;}
    out.match=Match::ready;out.valid=true;
    if(input_.kind==TurnKind::left)out.feature=Feature::leftTurn;
    else if(input_.kind==TurnKind::right)out.feature=Feature::rightTurn;
    // Provider estimates may be displayed as estimates. No shape index, bend,
    // exact-distance phrase, posted speed limit or gear can be derived here.
    out.distanceM=out.maneuverDistanceM=input_.remainingM;
    return out;
}
Prompt ProviderTurnCoach::evaluate(double now,bool braking,bool shock) const noexcept {
    pending_=0;const auto c=context(now);
    const bool spoken=input_.session==acceptedSession_&&input_.routeEpoch==acceptedEpoch_&&
        input_.step==acceptedStep_&&input_.kind==acceptedKind_;
    if(!c.valid||spoken||braking||shock||now-lastAccepted_<8||fix_.speedKmh<=3||
       !std::isfinite(input_.remainingM)||!std::isfinite(input_.remainingSeconds))return Prompt::none;
    // Provisional preparation budget: roughly three seconds of speech plus
    // learner response and estimation margin. Require both independent speed
    // and the provider's ETA to leave at least eight seconds, with a bounded
    // approach window so slow traffic does not produce distant turn lessons.
    const double age=now-input_.receivedAt;
    const double measuredLead=(input_.remainingM-fix_.accuracyM)/(fix_.speedKmh/3.6)-age;
    const double providerLead=input_.remainingSeconds-age;
    if(measuredLead<8||measuredLead>35||providerLead<8||providerLead>25)return Prompt::none;
    const auto prompt=c.feature==Feature::leftTurn?Prompt::mapLeftTurn:
        c.feature==Feature::rightTurn?Prompt::mapRightTurn:Prompt::none;
    if(prompt!=Prompt::none)pending_=eventId_;
    return prompt;
}
void ProviderTurnCoach::accepted(double now) noexcept {
    if(!pending_||pending_!=eventId_||!std::isfinite(now)||now<lastAccepted_)return;
    acceptedSession_=input_.session;acceptedEpoch_=input_.routeEpoch;
    acceptedStep_=input_.step;acceptedKind_=input_.kind;lastAccepted_=now;pending_=0;
}
const char* ProviderTurnCoach::status(double now) const noexcept {
    if(!active_)return "No provider route selected";
    if(!available_)return "Navigation feed unavailable; turn coaching paused";
    if(input_.state==TurnState::rerouting)return "Route changing; turn coaching paused";
    if(input_.state==TurnState::stopped)return "Navigation stopped";
    if(input_.state==TurnState::unknown)return "Navigation state unavailable";
    if(input_.state==TurnState::arrived)return "Destination reached; check your surroundings";
    if(input_.step<0)return "Turn identity unavailable; turn coaching paused";
    const auto c=context(now);
    if(c.match==Match::ready)return "Provider turn estimates; measured GPS confirmed";
    if(c.match==Match::stale)return "Navigation or GPS stale; turn coaching paused";
    return "Confirming measured GPS; turn coaching paused";
}
} // namespace ld::navigation
