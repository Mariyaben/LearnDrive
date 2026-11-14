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
    points_.clear();bends_.clear();grade_.clear();slopes_.clear();gradeStart_=0;turnCount_=0;turnsTruncated_=false;turnsTime_=-inf;
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
double RouteLine::aim(double now,double seconds) const noexcept {
    if(points_.size()<2||!matched_||!std::isfinite(now)||!std::isfinite(seconds)||now<fixTime_||now-fixTime_>2.5)return std::numeric_limits<double>::quiet_NaN();
    const double here=progress_+speedMps_*std::clamp(now-fixTime_,0.,2.5),total=points_.back().s;
    const double ahead=std::clamp(speedMps_*seconds,15.,120.);
    if(here+ahead>total)return std::numeric_limits<double>::quiet_NaN();
    const auto p=at(here),q=at(here+ahead);
    const double bearing=std::atan2(q.x-p.x,q.y-p.y),course=heading(here)*pi/180;
    // Clockwise bearings: a point left of the course has a smaller bearing.
    return std::remainder(course-bearing,2*pi);
}
bool RouteLine::load(const double* latLon,size_t count) {
    clear();
    if(!count)return false; // No line: cleared, nothing loaded.
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
    // The road's direction over 8 m of it (a kink in the line is not a bend), and how much it turns
    // in the tightest 20 m of a stretch. A comfortable bend for a learner pulls about 1.5 m/s^2
    // sideways: its speed is sqrt(1.5 x radius), the radius being 20 m over that turn.
    const auto chord=[&](double a){const auto p=at(std::clamp(a,0.,total)),q=at(std::clamp(a+8,0.,total));
        return std::fmod(std::atan2(q.x-p.x,q.y-p.y)*180/pi+360.,360.);};
    const auto tightest=[&](double from,double to){double worst=0;
        for(double p=std::max(0.,from);p<=to;p+=2)worst=std::max(worst,std::abs(turn(chord(p),chord(p+20))));return worst;};
    const auto comfortable=[&](double degrees){const double radius=20/std::max(.05,degrees*pi/180);
        return std::clamp(3.6*std::sqrt(1.5*radius),15.,45.);};
    // A sharp bend is taken in second gear whatever the line makes of its radius.
    for(auto& b:bends_)b.adviseKmh=std::min(22.,comfortable(tightest(b.s-8,b.s+bendWindowM-12)));
    // Bends that are not sharp but still want the right speed: 35 degrees or more within 40 m.
    // Where a sharp bend stands (from 45 m before it to 25 m past its end) it is that bend.
    const size_t sharp=bends_.size();
    for(double s=18;s<total-48;) {
        const double h0=chord(s-8),change=turn(h0,chord(s+32));
        if(std::abs(change)>=35) {
            double start=s;
            for(double p=s;p<s+40;p+=1)if(std::abs(turn(h0,chord(p)))>=10){start=p;break;}
            start=std::max(s,start-3);
            bool taken=false;
            for(size_t i=0;i<sharp;++i)taken|=start>=bends_[i].s-45&&start<=bends_[i].s+bendWindowM+25;
            if(!taken)bends_.push_back({start,change,bendFlag|(generation_<<20)|(++index&0xFFFFF),false,comfortable(tightest(start-8,start+28))});
            s+=60;
        } else s+=2;
    }
    std::sort(bends_.begin(),bends_.end(),[](const Bend& a,const Bend& b){return a.s<b.s;});
    return true;
}
// The road's grade from elevation samples along the line. Elevation models are several metres
// noisy (trees and buildings in radar terrain models), so heights are resampled every 10 m,
// averaged over +-40 m, and the grade is the rise over 100 m. A climb or descent starts at >= 5 %
// and lasts while >= 3.5 % the same way (same-direction stretches <= 40 m apart are one slope); it
// counts when it averages >= 6 % over >= 100 m and the road stays that much higher or lower beyond
// it. Tuned on SRTM profiles of Kerala roads: flat highways and city streets, a river crossing,
// rolling hills and a ghat road (docs/coach/MAP_HILLS.md).
bool RouteLine::elevation(const double* v,size_t count) {
    const std::vector<Slope> before=slopes_;
    grade_.clear();slopes_.clear();gradeStart_=0;
    if(points_.empty()||!v||count<5||count>20000)return false;
    std::vector<std::pair<double,double>> along;along.reserve(count);
    size_t seg=0;
    for(size_t k=0;k<count;++k) {
        const double lat=v[3*k],lon=v[3*k+1],h=v[3*k+2];
        if(!coordinate(lat,lon)||!std::isfinite(h)||h<-500||h>9000)continue;
        const double x=(lon-lon0_)*kx_,y=(lat-lat0_)*ky_;
        double best=inf,bestS=0;size_t bestSeg=seg;
        // Samples follow the route: search onward from the last one (a little back for safety).
        for(size_t i=seg>20?seg-20:0;i+1<points_.size();++i) {
            const auto& a=points_[i];const auto& b=points_[i+1];const double dx=b.x-a.x,dy=b.y-a.y,l2=dx*dx+dy*dy;
            const double u=l2>0?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/l2,0.,1.):0.;
            const double d=std::hypot(x-a.x-u*dx,y-a.y-u*dy);
            if(d<best){best=d;bestS=a.s+u*(b.s-a.s);bestSeg=i;}
            if(best<2&&i>bestSeg+40)break;
        }
        if(best>25)continue;
        seg=bestSeg;along.push_back({bestS,h});
    }
    if(along.size()<5)return false;
    std::sort(along.begin(),along.end());
    const double s0=along.front().first,s1=along.back().first;
    if(s1-s0<100)return false;
    constexpr double step=10;
    const size_t n=size_t((s1-s0)/step+1e-6)+1;
    std::vector<double> h(n),smooth(n);
    for(size_t i=0,j=0;i<n;++i) {
        const double s=s0+double(i)*step;
        while(j+1<along.size()&&along[j+1].first<s)++j;
        const auto& a=along[j];const auto& b=along[std::min(j+1,along.size()-1)];
        h[i]=b.first>a.first?a.second+(b.second-a.second)*std::clamp((s-a.first)/(b.first-a.first),0.,1.):a.second;
    }
    for(size_t i=0;i<n;++i){double sum=0;int c=0;for(long d=-4;d<=4;++d){const long k=long(i)+d;if(k>=0&&k<long(n)){sum+=h[size_t(k)];++c;}}smooth[i]=sum/c;}
    grade_.assign(n,0);gradeStart_=s0;
    for(size_t i=0;i<n;++i){const size_t a=i>=5?i-5:0,b=std::min(n-1,i+5);if(b>a)grade_[i]=(smooth[b]-smooth[a])/(double(b-a)*step)*100;}
    for(size_t i=0;i<n;) {
        if(std::abs(grade_[i])<5){++i;continue;}
        const double sign=grade_[i]>0?1:-1;size_t k=i;double steepest=0;
        while(k<n&&grade_[k]*sign>=3.5){if(std::abs(grade_[k])>std::abs(steepest))steepest=grade_[k];++k;}
        // Its grade: the mean over its steady part (within 80 % of the steepest), not the
        // smoothing's ramps at either end, so a 12 % climb reads about 12 %, not 10 %.
        double sum=0;int core=0;for(size_t q=i;q<k;++q)if(std::abs(grade_[q])>=.8*std::abs(steepest)){sum+=grade_[q];++core;}
        const Slope run{s0+double(i)*step,s0+double(k-1)*step,sum/std::max(1,core),steepest,0};
        if(!slopes_.empty()&&(slopes_.back().grade>0)==(run.grade>0)&&run.start-slopes_.back().end<=40) {
            auto& p=slopes_.back();const double l1=p.end-p.start+step,l2=run.end-run.start+step;
            p.grade=(p.grade*l1+run.grade*l2)/(l1+l2);p.end=run.end;if(std::abs(run.steepest)>std::abs(p.steepest))p.steepest=run.steepest;
        } else slopes_.push_back(run);
        i=k;
    }
    // Elevation models give the ground, not the road deck: a bridge over a river or valley reads as
    // a short descent then a climb of about the same height, a tunnel or cutting as the reverse.
    // Real roads rarely dip and rise like that within a few hundred metres, so such a pair is left
    // out rather than announced as hills on a flat bridge.
    std::vector<bool> crossing(slopes_.size());
    const auto rise=[&](const Slope& q){return std::abs(q.grade)*(q.end-q.start+step)/100;};
    for(size_t i=0;i+1<slopes_.size();++i) {
        const auto& a=slopes_[i];const auto& b=slopes_[i+1];
        if((a.grade>0)==(b.grade>0)||b.start-a.end>300||a.end-a.start>200||b.end-b.start>200)continue;
        if(std::min(rise(a),rise(b))>=.6*std::max(rise(a),rise(b)))crossing[i]=crossing[i+1]=true;
    }
    // The road's height 30-150 m before a slope and 30-150 m beyond it (median): a climb or descent
    // leaves the road higher or lower, by >= 5 m and at least half its own rise; a clump of trees
    // or a building in the terrain model rises and falls back.
    const auto level=[&](double from,double to){std::vector<double> v;
        for(size_t i=0;i<n;++i){const double s=s0+double(i)*step;if(s>=from&&s<=to)v.push_back(smooth[i]);}
        if(v.size()<3)return std::numeric_limits<double>::quiet_NaN();
        std::nth_element(v.begin(),v.begin()+long(v.size()/2),v.end());return v[v.size()/2];};
    const auto lasts=[&](const Slope& q){
        double before=level(q.start-150,q.start-30),after=level(q.end+30,q.end+150);
        // At either end of what is known: the first or last height there is.
        if(!std::isfinite(before))before=q.start-s0<150?smooth.front():before;
        if(!std::isfinite(after))after=s1-q.end<150?smooth.back():after;
        if(!std::isfinite(before)||!std::isfinite(after))return false;
        return (after-before)*(q.grade>0?1:-1)>=std::max(5.,.5*rise(q));};
    {size_t k=0;for(size_t i=0;i<slopes_.size();++i){const auto& q=slopes_[i];
        if(!crossing[i]&&q.end-q.start>=100&&std::abs(q.grade)>=6&&lasts(q))slopes_[k++]=q;}slopes_.resize(k);}
    // A slope keeps its id when the profile is refreshed (the next stretch fetched): the new slope
    // that overlaps an old one the same way is that slope, so it is not announced twice.
    std::vector<bool> taken(before.size());
    for(auto& q:slopes_) {
        q.id=0;
        for(size_t i=0;i<before.size();++i){const auto& o=before[i];
            if(taken[i]||(o.grade>0)!=(q.grade>0))continue;
            if(std::min(o.end,q.end)-std::max(o.start,q.start)>=.5*std::min(o.end-o.start,q.end-q.start)){q.id=o.id;taken[i]=true;break;}}
        if(!q.id)q.id=slopeFlag|(generation_<<20)|(++nextSlope_&0xFFFFF);
    }
    return true;
}
double RouteLine::gradeAt(double s) const noexcept {
    if(grade_.empty())return std::numeric_limits<double>::quiet_NaN();
    const double i=(s-gradeStart_)/10;
    if(i<0||i>double(grade_.size()-1))return std::numeric_limits<double>::quiet_NaN();
    const size_t a=size_t(i),b=std::min(grade_.size()-1,a+1);
    return grade_[a]+(grade_[b]-grade_[a])*(i-double(a));
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
            out.bendDistanceM=b.s-progress_;out.bendAngle=b.angle;out.bendId=b.id;out.bendRight=b.angle>0;
            out.bendSharp=b.sharp;out.bendAdviseKmh=b.adviseKmh;break;
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
    // The road's grade here and the next climb or descent (where the car is now: the last fix
    // plus the distance driven since).
    const double where=progress_+speedMps_*std::clamp(now-fixTime_,0.,2.5);
    const double g=gradeAt(where);
    if(std::isfinite(g)){out.gradeKnown=true;out.gradePercent=g;}
    for(const auto& q:slopes_) {
        if(q.end<where)continue;
        if(q.start-where>600)break;
        out.slopeDistanceM=q.start-where;out.slopeGradePercent=q.grade;out.slopeSteepestPercent=q.steepest;
        out.slopeLengthM=q.end-std::max(q.start,where);out.slopeId=q.id;break;
    }
    return out;
}
}
