#include "learndrive/navigation/JunctionScout.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
namespace ld::navigation {
namespace {
constexpr double pi=3.14159265358979323846,lookAheadM=400;
double angleDiff(double a,double b) {return std::abs(std::fmod(a-b+540.,360.)-180.);}
// Signed heading change from a to b, degrees in (-180,180]; positive turns right.
double signedTurn(double a,double b) {return std::fmod(b-a+540.,360.)-180.;}
constexpr double bendWindowM=35,bendDegrees=55;
struct Local {double kx,ky,lat0,lon0;
    double x(double lon) const {return (lon-lon0)*kx;}double y(double lat) const {return (lat-lat0)*ky;}};
double bearingBetween(const Local& l,double lat1,double lon1,double lat2,double lon2) {
    const double dx=l.x(lon2)-l.x(lon1),dy=l.y(lat2)-l.y(lat1);
    return std::fmod(std::atan2(dx,dy)*180/pi+360.,360.);
}
}
void JunctionScout::clear() {nodes_.clear();ways_.clear();wayIds_.clear();nodeWays_.clear();bumps_.clear();ahead_={};bearing_=std::numeric_limits<double>::quiet_NaN();lastTime_=-std::numeric_limits<double>::infinity();}
bool JunctionScout::addWay(int64_t wayId,int roadClass,const int64_t* ids,const double* latLon,size_t count) {
    if(!ids||!latLon||count<2||count>5000||ways_.size()>=60000||wayIds_.count(wayId))return false;
    for(size_t i=0;i<count;++i)if(!std::isfinite(latLon[2*i])||!std::isfinite(latLon[2*i+1])||std::abs(latLon[2*i])>90||std::abs(latLon[2*i+1])>180)return false;
    Way way;way.id=wayId;way.cls=std::clamp(roadClass,0,9);way.nodes.assign(ids,ids+count);
    const size_t index=ways_.size();
    for(size_t i=0;i<count;++i) {
        auto& node=nodes_[ids[i]];node.lat=latLon[2*i];node.lon=latLon[2*i+1];
        node.branches+=(i>0)+(i+1<count); // Road segments meeting at this node.
        nodeWays_[ids[i]].push_back({index,i});
    }
    ways_.push_back(std::move(way));wayIds_.insert(wayId);return true;
}
JunctionAhead JunctionScout::current(double now) const noexcept {
    if(!(ahead_.valid||ahead_.matched)||!std::isfinite(now)||now<ahead_.fixTime||now-ahead_.fixTime>2.5)return {};
    return ahead_;
}
JunctionAhead JunctionScout::update(double time,double lat,double lon,double bearingDeg,bool bearingValid,double speed,double accuracy) {
    if(!std::isfinite(time)||!std::isfinite(lat)||!std::isfinite(lon)||ways_.empty()){ahead_={};return ahead_;}
    const double kx=111320*std::cos(lat*pi/180),ky=110540;const Local l{kx,ky,lat,lon};
    // Travel direction: GPS bearing when moving, else displacement, else the last one.
    double b=std::numeric_limits<double>::quiet_NaN();
    if(bearingValid&&std::isfinite(bearingDeg)&&std::isfinite(speed)&&speed>4)b=std::fmod(bearingDeg+360.,360.);
    else if(time-lastTime_<3&&std::hypot(l.x(lastLon_),l.y(lastLat_))>3)b=bearingBetween(l,lastLat_,lastLon_,lat,lon);
    if(!std::isfinite(b))b=bearing_;
    bearing_=b;lastLat_=lat;lastLon_=lon;lastTime_=time;
    ahead_={};ahead_.fixTime=time;
    if(!std::isfinite(b))return ahead_;
    // 1. Match to the nearest road segment aligned with the travel direction.
    size_t wi=0,seg=0;double bestScore=1e18,bestT=0;bool forward=true,found=false;
    const double gate=std::max(12.,1.5*(std::isfinite(accuracy)?accuracy:10.));
    for(size_t w=0;w<ways_.size();++w){const auto& way=ways_[w];
        for(size_t k=0;k+1<way.nodes.size();++k){const auto& A=nodes_.at(way.nodes[k]);const auto& B=nodes_.at(way.nodes[k+1]);
            const double ax=l.x(A.lon),ay=l.y(A.lat),dx=l.x(B.lon)-ax,dy=l.y(B.lat)-ay,len2=dx*dx+dy*dy;if(len2<1e-6)continue;
            const double t=std::clamp(-(ax*dx+ay*dy)/len2,0.,1.),dist=std::hypot(ax+t*dx,ay+t*dy);if(dist>gate)continue;
            const double sb=std::fmod(std::atan2(dx,dy)*180/pi+360.,360.),df=angleDiff(b,sb),db=angleDiff(b,sb+180);
            const double diff=std::min(df,db);if(diff>40)continue;
            const double score=dist+.3*diff;if(score<bestScore){bestScore=score;wi=w;seg=k;bestT=t;forward=df<=db;found=true;}}}
    if(!found)return ahead_;
    ahead_.matched=true;
    // 2. Walk ahead along the road to the first junction an instructor would prepare for,
    // noting a mapped speed bump and a sharp bend on the way.
    std::deque<std::pair<double,double>> turns; // (distance, signed turn) within the bend window
    double lastIn=std::numeric_limits<double>::quiet_NaN(),prevDist=0;int64_t prevId=0;
    size_t idx=forward?seg+1:seg;
    {const auto& A=nodes_.at(ways_[wi].nodes[seg]);const auto& B=nodes_.at(ways_[wi].nodes[seg+1]);
     const double len=std::hypot(l.x(B.lon)-l.x(A.lon),l.y(B.lat)-l.y(A.lat));ahead_.distanceM=0;
     double travelled=(forward?1-bestT:bestT)*len;double prevLat=lat,prevLon=lon;
     for(int steps=0;steps<80&&travelled<=lookAheadM;++steps) {
        const int64_t id=ways_[wi].nodes[idx];const auto& node=nodes_.at(id);
        const double inBearing=bearingBetween(l,prevLat,prevLon,node.lat,node.lon);
        if(!ahead_.bumpNode&&bumps_.count(id)){ahead_.bumpNode=id;ahead_.bumpDistanceM=travelled;}
        if(std::isfinite(lastIn)&&prevId) {
            turns.push_back({prevDist,signedTurn(lastIn,inBearing)});
            while(turns.size()>1&&turns.front().first<prevDist-bendWindowM)turns.pop_front();
            double sum=0;for(const auto& t:turns)sum+=t.second;
            if(!ahead_.bendNode&&std::abs(sum)>=bendDegrees){ahead_.bendNode=prevId;ahead_.bendDistanceM=turns.front().first;ahead_.bendRight=sum>0;}
        }
        lastIn=inBearing;prevDist=travelled;prevId=id;
        if(node.branches>=3) {
            bool straight=false,important=false;
            for(const auto& [w,p]:nodeWays_.at(id))for(int side:{-1,1}){const long q=long(p)+side;
                if(q<0||q>=long(ways_[w].nodes.size()))continue;const auto& nb=nodes_.at(ways_[w].nodes[size_t(q)]);
                if(std::abs(nb.lat-prevLat)<1e-9&&std::abs(nb.lon-prevLon)<1e-9)continue; // where we came from
                const double turn=angleDiff(inBearing,bearingBetween(l,node.lat,node.lon,nb.lat,nb.lon));
                const bool continuation=turn<35;straight|=continuation;
                // More important road, or a true crossroads with an equal road.
                if(!continuation&&(ways_[w].cls<ways_[wi].cls||(node.branches>=4&&ways_[w].cls<=ways_[wi].cls)))important=true;}
            if(!straight||important) {
                ahead_.valid=true;ahead_.roadEnds=!straight;ahead_.nodeId=id;ahead_.wayId=ways_[wi].id;
                ahead_.distanceM=travelled;ahead_.branches=node.branches;return ahead_;
            }
        }
        // Advance along this way, or continue onto the road that carries straight on.
        const long next=long(idx)+(forward?1:-1);
        if(next>=0&&next<long(ways_[wi].nodes.size())) {
            const auto& nn=nodes_.at(ways_[wi].nodes[size_t(next)]);
            travelled+=std::hypot(l.x(nn.lon)-l.x(node.lon),l.y(nn.lat)-l.y(node.lat));
            prevLat=node.lat;prevLon=node.lon;idx=size_t(next);continue;
        }
        double bestTurn=45;bool moved=false;size_t nw=0,nidx=0;bool nfwd=true;
        for(const auto& [w,p]:nodeWays_.at(id)){if(w==wi)continue;for(int side:{-1,1}){const long q=long(p)+side;
            if(q<0||q>=long(ways_[w].nodes.size()))continue;const auto& nb=nodes_.at(ways_[w].nodes[size_t(q)]);
            const double turn=angleDiff(inBearing,bearingBetween(l,node.lat,node.lon,nb.lat,nb.lon));
            if(turn<bestTurn){bestTurn=turn;nw=w;nidx=size_t(q);nfwd=side>0;moved=true;}}}
        if(!moved)break; // Dead end: nothing to prepare for.
        const auto& nn=nodes_.at(ways_[nw].nodes[nidx]);
        travelled+=std::hypot(l.x(nn.lon)-l.x(node.lon),l.y(nn.lat)-l.y(node.lat));
        prevLat=node.lat;prevLon=node.lon;wi=nw;idx=nidx;forward=nfwd;
     }}
    return ahead_;
}
}
