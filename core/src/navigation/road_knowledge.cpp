#include "learndrive/navigation/RoadKnowledge.hpp"
#include <algorithm>
#include <cmath>
namespace ld::navigation {
namespace {
constexpr double pi=3.14159265358979323846,ky=110540,inf=std::numeric_limits<double>::infinity();
double kxAt(double lat) {return 111320*std::cos(lat*pi/180);}
bool coordinate(double lat,double lon) {return std::isfinite(lat)&&std::isfinite(lon)&&std::abs(lat)<=85&&std::abs(lon)<=180;}
// Signed change from heading a to b, degrees in (-180,180]; positive turns right.
double turn(double a,double b) {return std::fmod(b-a+540.,360.)-180.;}
double angleDiff(double a,double b) {return std::abs(turn(a,b));}
// Same rule as the junction scout and the route simulator.
constexpr double bendWindowM=35,bendDegrees=55,lookAheadM=400,bumpLateralM=8;
}

// ------------------------------------------------------------------ bumps
bool BumpMap::addMapped(uint64_t id,double lat,double lon) {
    if(!id||!coordinate(lat,lon)||bumps_.size()>=capacity)return false;
    for(auto& b:bumps_)if(b.mapped&&b.id==id)return true;
    // A learned bump at a mapped one is the same bump: keep one, as mapped.
    const double kx=kxAt(lat);
    bumps_.erase(std::remove_if(bumps_.begin(),bumps_.end(),[&](const Bump& b){
        return !b.mapped&&std::hypot((b.lon-lon)*kx,(b.lat-lat)*ky)<=15;}),bumps_.end());
    Bump b;b.id=id;b.lat=lat;b.lon=lon;b.mapped=true;bumps_.push_back(b);return true;
}
bool BumpMap::restore(double lat,double lon,double heading,int observations,int misses) {
    if(!coordinate(lat,lon)||!std::isfinite(heading)||observations<1||observations>1000||misses<0||misses>1000||bumps_.size()>=capacity)return false;
    Bump b;b.id=learnedFlag|nextLearned_++;b.lat=lat;b.lon=lon;b.heading=std::fmod(heading+360.,360.);
    b.observations=observations;b.misses=misses;bumps_.push_back(b);return true;
}
uint64_t BumpMap::felt(double time,double lat,double lon,double heading) {
    if(!std::isfinite(time)||!coordinate(lat,lon)||!std::isfinite(heading))return 0;
    const double kx=kxAt(lat);Bump* nearest=nullptr;double best=15;
    for(auto& b:bumps_){const double d=std::hypot((b.lon-lon)*kx,(b.lat-lat)*ky);if(d<=best){best=d;nearest=&b;}}
    if(nearest) {
        if(nearest->mapped)return nearest->id;
        // The same pass (both axles, or a second jolt) is one observation; a later pass is another.
        if(time-nearest->lastFelt>20) {
            const double w=1./(nearest->observations+1);
            nearest->lat+=(lat-nearest->lat)*w;nearest->lon+=(lon-nearest->lon)*w;
            ++nearest->observations;
        }
        nearest->lastFelt=time;return nearest->id;
    }
    if(bumps_.size()>=capacity)return 0;
    Bump b;b.id=learnedFlag|nextLearned_++;b.lat=lat;b.lon=lon;b.heading=std::fmod(heading+360.,360.);
    b.observations=1;b.lastFelt=time;bumps_.push_back(b);return b.id;
}
void BumpMap::travel(double time,double lat,double lon,double heading,bool headingValid,double speedKmh,double accuracyM,bool sensing) noexcept {
    if(!std::isfinite(time)||!coordinate(lat,lon))return;
    const bool usableFix=headingValid&&std::isfinite(heading)&&std::isfinite(speedKmh)&&speedKmh>=8&&std::isfinite(accuracyM)&&accuracyM<=15;
    const double kx=kxAt(lat),hx=std::sin(heading*pi/180),hy=std::cos(heading*pi/180);
    for(auto& b:bumps_) {
        if(b.mapped)continue;
        // A pass is settled 4 s after it: felt within 4 s of passing, or a miss.
        if(std::isfinite(b.passedAt)&&time>b.passedAt+4) {
            if(!(b.lastFelt>=b.passedAt-4))++b.misses;
            b.passedAt=-inf;
        }
        if(!usableFix){b.lastAlong=std::numeric_limits<double>::quiet_NaN();continue;}
        const double dx=(b.lon-lon)*kx,dy=(b.lat-lat)*ky;
        if(std::hypot(dx,dy)>40){b.lastAlong=std::numeric_limits<double>::quiet_NaN();continue;}
        // Along its road only (either direction: a speed bump spans the road).
        const double h=angleDiff(heading,b.heading);
        if(h>30&&h<150){b.lastAlong=std::numeric_limits<double>::quiet_NaN();continue;}
        const double along=dx*hx+dy*hy,lateral=std::abs(dx*hy-dy*hx);
        if(lateral<=bumpLateralM&&std::isfinite(b.lastAlong)&&b.lastAlong>0&&along<=0&&time-b.lastAlongTime<3&&sensing&&!std::isfinite(b.passedAt))
            b.passedAt=time;
        b.lastAlong=lateral<=bumpLateralM?along:std::numeric_limits<double>::quiet_NaN();b.lastAlongTime=time;
    }
    // Forget learned bumps that keep not being there.
    bumps_.erase(std::remove_if(bumps_.begin(),bumps_.end(),[](const Bump& b){return !b.mapped&&b.misses>=b.observations+2;}),bumps_.end());
}
size_t BumpMap::learnedCount() const noexcept {return size_t(std::count_if(bumps_.begin(),bumps_.end(),[](const Bump& b){return !b.mapped;}));}
size_t BumpMap::usableLearnedCount() const noexcept {return size_t(std::count_if(bumps_.begin(),bumps_.end(),[](const Bump& b){return !b.mapped&&b.usable();}));}
BumpMap::Hit BumpMap::along(const GeoPoint* path,size_t n,double maxS) const noexcept {
    Hit hit;
    if(!path||n<2||bumps_.empty()||!coordinate(path[0].lat,path[0].lon))return hit;
    const double lat0=path[0].lat,lon0=path[0].lon,kx=kxAt(lat0);
    auto x=[&](double lon){return (lon-lon0)*kx;};auto y=[&](double lat){return (lat-lat0)*ky;};
    for(const auto& b:bumps_) {
        if(!b.usable())continue;
        const double bx=x(b.lon),by=y(b.lat);
        if(std::hypot(bx,by)>maxS+bumpLateralM+10)continue;
        for(size_t i=0;i+1<n&&path[i].s<=maxS;++i) {
            const double ax=x(path[i].lon),ay=y(path[i].lat),dx=x(path[i+1].lon)-ax,dy=y(path[i+1].lat)-ay,len2=dx*dx+dy*dy;
            if(len2<1e-6)continue;
            const double t=std::clamp(((bx-ax)*dx+(by-ay)*dy)/len2,0.,1.);
            if(std::hypot(ax+t*dx-bx,ay+t*dy-by)>bumpLateralM)continue;
            const double s=path[i].s+t*(path[i+1].s-path[i].s);
            if(s>=0&&s<=maxS&&s<hit.distanceM){hit.distanceM=s;hit.id=b.id;hit.learned=!b.mapped;}
            break;
        }
    }
    return hit;
}

// ------------------------------------------------------------------ route line
void RouteLine::clear() noexcept {
    points_.clear();bends_.clear();turnCount_=0;turnsTruncated_=false;turnsTime_=-inf;
    progress_=0;fixTime_=-inf;matched_=false;consistent_=0;
}
RouteLine::Point RouteLine::at(double s) const noexcept {
    auto it=std::lower_bound(points_.begin(),points_.end(),s,[](const Point& p,double v){return p.s<v;});
    if(it==points_.begin())return points_.front();
    if(it==points_.end())return points_.back();
    const auto& a=*(it-1);const double f=(s-a.s)/(it->s-a.s);
    return {a.x+(it->x-a.x)*f,a.y+(it->y-a.y)*f,s};
}
double RouteLine::heading(double s) const noexcept {
    const double total=points_.back().s;
    const double a=std::clamp(s,0.,std::max(0.,total-2)),b=std::min(total,a+2);
    const auto p=at(a),q=at(b);
    return std::fmod(std::atan2(q.x-p.x,q.y-p.y)*180/pi+360.,360.);
}
bool RouteLine::load(const double* latLon,size_t count) {
    clear();
    if(!count)return true;
    if(!latLon||count<2||count>100000)return false;
    if(!coordinate(latLon[0],latLon[1]))return false;
    lat0_=latLon[0];lon0_=latLon[1];kx_=kxAt(lat0_);ky_=ky;
    points_.reserve(count);double total=0;
    for(size_t i=0;i<count;++i) {
        const double lat=latLon[2*i],lon=latLon[2*i+1];
        if(!coordinate(lat,lon)){clear();return false;}
        Point p{(lon-lon0_)*kx_,(lat-lat0_)*ky_,0};
        if(!points_.empty()) {
            const double d=std::hypot(p.x-points_.back().x,p.y-points_.back().y);
            if(d>2000){clear();return false;} // A broken line, not a route.
            if(d<.05)continue;
            total+=d;
        }
        p.s=total;points_.push_back(p);
    }
    if(points_.size()<2||total<20){clear();return false;}
    ++generation_;
    // Sharp bends: the route's own heading turns >= 55 degrees within 35 m. Placed where
    // the road starts turning, as an instructor would see it coming.
    uint32_t index=0;
    for(double s=10;s<total-10;) {
        const double h0=heading(std::max(0.,s-1)),change=turn(h0,heading(std::min(total,s+bendWindowM)));
        if(std::abs(change)>=bendDegrees) {
            double start=s;
            for(double p=s;p<s+bendWindowM;p+=1)if(std::abs(turn(h0,heading(p)))>=10){start=p;break;}
            start=std::max(s,start-3);
            bends_.push_back({start,change,bendFlag|(generation_<<20)|(++index&0xFFFFF)});
            s+=bendWindowM+20;
        } else s+=2;
    }
    return true;
}
void RouteLine::maneuvers(double time,const double* aheadM,size_t count) noexcept {
    if(points_.empty()||!std::isfinite(time)||(count&&!aheadM))return;
    if(!matched_||!std::isfinite(fixTime_)){turnCount_=0;turnsTime_=-inf;return;}
    // Where the car is now along the line (the last fix plus the distance since).
    const double here=progress_+speedMps_*std::clamp(time-fixTime_,0.,2.5);
    turnCount_=0;turnsTruncated_=count>turns_.size();
    for(size_t i=0;i<count&&turnCount_<turns_.size();++i)
        if(std::isfinite(aheadM[i])&&aheadM[i]>=0)turns_[turnCount_++]=here+aheadM[i];
    turnsTime_=time;
}
bool RouteLine::fix(double time,double lat,double lon,double accuracy,double bearing,bool bearingValid,double speedKmh) noexcept {
    if(points_.empty()||!std::isfinite(time)||time<=fixTime_||!coordinate(lat,lon))return false;
    const double previous=fixTime_;
    if(!std::isfinite(accuracy)||accuracy<0||accuracy>25){matched_=false;consistent_=0;fixTime_=time;return false;}
    const double x=(lon-lon0_)*kx_,y=(lat-lat0_)*ky_;
    size_t begin=0,end=points_.size()-1;
    if(matched_) {
        const auto lo=std::lower_bound(points_.begin(),points_.end(),std::max(0.,progress_-50),[](const Point& p,double s){return p.s<s;});
        begin=lo==points_.begin()?0:size_t(lo-points_.begin()-1);
        const auto hi=std::upper_bound(points_.begin(),points_.end(),progress_+600,[](double s,const Point& p){return s<p.s;});
        end=std::min(points_.size()-1,size_t(hi-points_.begin()));
    }
    double best=inf,bestS=0,segHeading=0;
    for(size_t i=begin;i<end;++i) {
        const auto& a=points_[i];const auto& b=points_[i+1];const double dx=b.x-a.x,dy=b.y-a.y;
        const double u=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
        const double d=std::hypot(x-a.x-u*dx,y-a.y-u*dy);
        if(d<best){best=d;bestS=a.s+u*(b.s-a.s);segHeading=std::atan2(dx,dy)*180/pi;}
    }
    const bool moving=std::isfinite(speedKmh)&&speedKmh>8;
    const bool off=best>std::clamp(1.5*accuracy,15.,30.)||(moving&&bearingValid&&std::isfinite(bearing)&&angleDiff(bearing,segHeading)>60);
    const bool jump=matched_&&(bestS<progress_-20||bestS-progress_>std::max(60.,(time-previous)*55+2*accuracy));
    fixTime_=time;
    if(off||jump){matched_=false;consistent_=0;return false;}
    progress_=bestS;speedMps_=std::isfinite(speedKmh)&&speedKmh>0?speedKmh/3.6:0;
    consistent_=std::min(consistent_+1,100);matched_=consistent_>=2;
    return matched_;
}
RouteAhead RouteLine::ahead(double now,const BumpMap& bumps) const noexcept {
    RouteAhead out;
    if(points_.empty()||!matched_||!std::isfinite(now)||now<fixTime_||now-fixTime_>2.5)return out;
    out.matched=true;out.fixTime=fixTime_;out.progressM=progress_;
    out.bendsKnown=now-turnsTime_<=5;
    if(out.bendsKnown) {
        const double lastKnown=turnsTruncated_&&turnCount_?turns_[turnCount_-1]:inf;
        for(const auto& b:bends_) {
            if(b.s<progress_-5)continue;
            if(b.s-progress_>lookAheadM||b.s>lastKnown)break;
            bool turnHere=false;
            for(size_t i=0;i<turnCount_;++i) {
                const double t=turns_[i];
                // The turn's own heading change, and road shape just before or after it.
                if((b.s>=t-50&&b.s<=t+30)||(t>=b.s&&t<=b.s+bendWindowM)){turnHere=true;break;}
            }
            if(turnHere)continue;
            out.bendDistanceM=b.s-progress_;out.bendAngle=b.angle;out.bendId=b.id;out.bendRight=b.angle>0;break;
        }
    }
    // Bumps on the route itself, up to 400 m ahead.
    std::array<GeoPoint,256> path;size_t n=0;
    auto it=std::lower_bound(points_.begin(),points_.end(),progress_,[](const Point& p,double s){return p.s<s;});
    const auto here=at(progress_);
    path[n++]={lat0_+here.y/ky_,lon0_+here.x/kx_,0};
    for(;it!=points_.end()&&n<path.size();++it) {
        if(it->s<=progress_)continue;
        path[n++]={lat0_+it->y/ky_,lon0_+it->x/kx_,it->s-progress_};
        if(it->s-progress_>lookAheadM)break;
    }
    const auto hit=bumps.along(path.data(),n,lookAheadM);
    out.bumpDistanceM=hit.distanceM;out.bumpId=hit.id;out.bumpLearned=hit.learned;
    return out;
}
}
