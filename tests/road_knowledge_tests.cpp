// Phase 2 road knowledge: speed bumps felt by the phone's motion sensor (synthetic 100 Hz
// signals), learned and confirmed over separate passes, and sharp bends on the provider's
// route line (a turn at one of the provider's maneuvers is never a bend).
#include "learndrive/BumpDetector.hpp"
#include "learndrive/navigation/RoadKnowledge.hpp"
#include "learndrive/navigation/JunctionScout.hpp"
#include "learndrive/c_api.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace ld;
using namespace ld::navigation;
namespace {
int passed=0;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int failed=0;
// Every group runs: one failure no longer hides the rest of the suite.
template<class F>void test(const char* name,F f){try{f();++passed;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
constexpr double pi=3.14159265358979323846,lat0=10.52,lon0=76.21,wheelbase=2.6;
double lat(double y){return lat0+y/110540;}
double lon(double x){return lon0+x/(111320*std::cos(lat0*pi/180));}
// Deterministic road vibration.
struct Noise {uint32_t s=12345;double next(){s=s*1664525u+1013904223u;return (double(s>>8)/16777216.-.5)*2;}};
// A damped oscillation starting at t0 (one wheel pair hitting something).
double pulse(double t,double t0,double amp,double hz,double tau){if(t<t0)return 0;const double x=t-t0;return amp*std::exp(-x/tau)*std::sin(2*pi*hz*x);}
struct Motion {double vertical{},pitch{},roll{};};
enum class Kind {bump,pothole,single,none};
// What the phone feels at time t when the front wheels reach the feature at t0, at v m/s.
Motion feel(Kind k,double t,double t0,double v,Noise& n,double noise=.25) {
    Motion m{noise*n.next(),.01*n.next(),.01*n.next()};
    const double rear=t0+wheelbase/v;
    if(k==Kind::bump){m.vertical+=pulse(t,t0,6,5,.12)+pulse(t,rear,5,5,.12);m.pitch+=pulse(t,t0,.4,2.5,.15)-pulse(t,rear,.35,2.5,.15);m.roll+=.03*n.next();}
    if(k==Kind::pothole){m.vertical+=pulse(t,t0,6,5,.12)+pulse(t,rear,5,5,.12);m.pitch+=pulse(t,t0,.15,2.5,.15);m.roll+=pulse(t,t0,.5,3,.15)-pulse(t,rear,.45,3,.15);}
    if(k==Kind::single){m.vertical+=pulse(t,t0,6,5,.12);m.pitch+=pulse(t,t0,.3,2.5,.15);}
    return m;
}
std::vector<BumpEvent> run(Kind k,double kmh,double noise=.25,bool handled=false,bool speedKnown=true) {
    BumpDetector d;Noise n;std::vector<BumpEvent> out;
    for(int i=0;i<1500;++i){const double t=i*.01;const auto m=feel(k,t,10,kmh/3.6,n,noise);
        d.sample(t,m.vertical,m.pitch,m.roll,speedKnown?kmh:std::nan(""),handled);BumpEvent e;while(d.pop(e))out.push_back(e);}
    return out;
}
int accepted(const std::vector<BumpEvent>& v){int n=0;for(auto& e:v)n+=e.accepted();return n;}
}
int main(){try{
    test("a road-wide bump is felt: front then rear wheels one wheelbase apart, nose up and down",[]{
        for(double kmh:{10.,20.,35.}) {
            const auto ev=run(Kind::bump,kmh);check(ev.size()==1&&ev[0].accepted(),"speed bump not accepted");
            const double gap=wheelbase/(kmh/3.6);check(std::abs(ev[0].axleGapS-gap)<.08,"axle gap does not match the speed");
            check(std::abs(ev[0].time-10)<.1,"bump time is not the front-wheel jolt");
        }});
    test("a pothole (one side), a single jolt, a rough road, a hand-held phone and unknown speed are not bumps",[]{
        auto pothole=run(Kind::pothole,20);check(!accepted(pothole)&&pothole.size()==1&&pothole[0].reason==BumpReason::oneSided,"pothole taken as a bump");
        auto single=run(Kind::single,20);check(!accepted(single)&&single.size()==1&&single[0].reason==BumpReason::oneAxle,"single jolt taken as a bump");
        check(!accepted(run(Kind::bump,20,1.8)),"bump on a rough road accepted");
        auto hand=run(Kind::bump,20,.25,true);check(!accepted(hand)&&hand[0].reason==BumpReason::handled,"hand-held jolt accepted");
        auto unknown=run(Kind::bump,20,.25,false,false);check(!accepted(unknown)&&unknown[0].reason==BumpReason::speed,"bump with unknown speed accepted");
        check(run(Kind::none,20).empty(),"ordinary road vibration made an event");
        check(!accepted(run(Kind::bump,60)),"bump at 60 km/h (outside the learning range) accepted");});
    test("a learned bump is used only after two separate passes, and is forgotten when it is not there",[]{
        BumpMap m;const double y=100;
        const auto id=m.felt(10,lat(y),lon(0),0);check(id&&m.learnedCount()==1&&!m.at(0).usable(),"one pass made a usable bump");
        check(m.felt(12,lat(y+3),lon(0),0)==id&&m.at(0).observations==1,"the same pass counted twice");
        check(m.felt(200,lat(y-4),lon(0),180)==id&&m.at(0).usable()&&m.usableLearnedCount()==1,"second pass (other direction) not confirmed");
        // Driven over twice at 30 km/h with the sensor working and nothing felt: no longer used.
        for(int pass=0;pass<2;++pass) {
            const double t0=400+pass*100;
            for(int k=0;k<=10;++k)m.travel(t0+k,lat(y-50+k*8.3),lon(0),0,true,30,5,true);
            m.travel(t0+20,lat(y+200),lon(0),0,true,30,5,true);
        }
        check(m.at(0).misses==2&&!m.at(0).usable(),"misses not counted or bump still used");
        for(int pass=2;pass<4;++pass){const double t0=400+pass*100;for(int k=0;k<=10;++k)m.travel(t0+k,lat(y-50+k*8.3),lon(0),0,true,30,5,true);m.travel(t0+20,lat(y+200),lon(0),0,true,30,5,true);}
        check(m.learnedCount()==0,"a bump that keeps not being there was kept");
        // Not a miss: sensor off, or passing on a crossing road.
        BumpMap q;q.felt(10,lat(y),lon(0),0);q.felt(100,lat(y),lon(0),0);
        for(int k=0;k<=10;++k)q.travel(300+k,lat(y-50+k*8.3),lon(0),0,true,30,5,false);q.travel(320,lat(y+200),lon(0),0,true,30,5,false);
        for(int k=0;k<=10;++k)q.travel(400+k,lat(y),lon(-50+k*8.3),90,true,30,5,true);q.travel(420,lat(y),lon(200),90,true,30,5,true);
        check(q.at(0).misses==0&&q.at(0).usable(),"miss counted with the sensor off or on a crossing road");
        // A mapped bump replaces a learned one at the same place.
        BumpMap r;r.felt(10,lat(y),lon(0),0);check(r.addMapped(777,lat(y+5),lon(0))&&r.size()==1&&r.at(0).mapped,"learned duplicate of a mapped bump kept");});
    test("bumps along a path ahead: within 8 m of the road, in path order, usable only",[]{
        BumpMap m;m.addMapped(5,lat(180),lon(3));m.addMapped(6,lat(90),lon(20)); // on the road; 20 m to the side
        m.felt(1,lat(60),lon(0),0); // learned once: not usable yet
        std::vector<GeoPoint> path;for(int i=0;i<=40;++i)path.push_back({lat(i*10.),lon(0),i*10.});
        const auto hit=m.along(path.data(),path.size(),400);check(hit.id==5&&std::abs(hit.distanceM-180)<1,"wrong bump ahead");
        check(m.along(path.data(),path.size(),150).id==0,"bump beyond the range reported");});
    test("route line: a sharp bend on the route is found where it starts; a turn at a maneuver is not a bend",[]{
        // North 300 m, a 90-degree bend right over ~20 m, then east 300 m.
        std::vector<double> ll;
        for(int i=0;i<=30;++i){ll.push_back(lat(i*10.));ll.push_back(lon(0));}
        for(int k=1;k<=4;++k){const double a=k*pi/10;ll.push_back(lat(300+12*std::sin(a)));ll.push_back(lon(12-12*std::cos(a)));}
        for(int i=1;i<=30;++i){ll.push_back(lat(312.));ll.push_back(lon(12+i*10.));}
        RouteLine r;check(r.load(ll.data(),ll.size()/2)&&r.bendCount()==1,"bend not found on the route line");
        BumpMap none;
        for(int k=0;k<3;++k)r.fix(1+k,lat(100+k*8.),lon(0),5,0,true,30);
        auto a=r.ahead(3.2,none);check(a.matched&&!a.bendsKnown&&!a.bendId,"bend reported before the maneuver positions are known");
        r.maneuvers(3.2,nullptr,0); // No maneuvers on this stretch.
        a=r.ahead(3.3,none);check(a.bendsKnown&&a.bendId&&a.bendRight&&a.bendDistanceM>175&&a.bendDistanceM<200,"route bend distance wrong");
        // The provider says there is a turn at that corner: it is the turn, not a bend.
        const double turnAt[]={300-116.};r.maneuvers(3.4,turnAt,1);
        a=r.ahead(3.5,none);check(a.bendsKnown&&!a.bendId,"a turn at a maneuver announced as a bend");
        // Off the route (another road 60 m away): not matched.
        r.fix(10,lat(200),lon(60),5,0,true,30);check(!r.ahead(10.2,none).matched,"matched while off the route");
        // A gentle curve (15 degrees over 100 m) is not a sharp bend.
        std::vector<double> g;for(int i=0;i<=40;++i){const double h=i*10*std::sin(15*pi/180*std::min(1.,i/20.));g.push_back(lat(i*10.));g.push_back(lon(h*.3));}
        RouteLine c;check(c.load(g.data(),g.size()/2)&&c.bendCount()==0,"gentle curve became a sharp bend");
        check(!c.load(nullptr,0)&&!c.loaded(),"empty line not cleared");});
    test("C API: a bump felt on two passes is warned about on the route ahead on the third",[]{
        std::unique_ptr<ld_engine,decltype(&ld_destroy)> e(ld_create(),ld_destroy);
        check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"camera source");
        // Phone upright in a windscreen mount: gravity along +y, rear camera forward (-z).
        const double g[3]={0,0,0};double a[3]={0,9.81,0},l[3]={0,0,0};
        for(int i=0;i<=700;++i)ld_imu(e.get(),i*.01,a,g,l,1); // still: the phone plane is normalized
        std::vector<double> line;for(int i=0;i<=40;++i){line.push_back(lat(i*10.));line.push_back(lon(0));}
        check(ld_route_line(e.get(),line.data(),line.size()/2),"route line");
        const double bumpY=150,v=5; // 18 km/h
        Noise n;double t=8;
        auto drive=[&](bool bumpThere,double& clock){
            const double start=clock;double nextFix=start;
            while(true) {
                const double y=(clock-start)*v;if(y>350)break;
                const double t0=start+bumpY/v;
                auto m=bumpThere?feel(Kind::bump,clock,t0,v,n):feel(Kind::none,clock,t0,v,n);
                double acc[3]={0,9.81+m.vertical,0},lin[3]={0,m.vertical,0},gyro[3]={m.pitch,0,-m.roll};
                ld_imu(e.get(),clock,acc,gyro,lin,1);
                if(clock>=nextFix){ld_gps(e.get(),clock,v,.5);ld_navigation_fix(e.get(),1,clock,lat(y),lon(0),5,v*3.6,.5,1,0,1);
                    ld_route_maneuvers(e.get(),clock,nullptr,0);nextFix+=1;}
                clock+=.01;
            }
            clock+=60;};
        double k[10]{};
        drive(true,t);ld_road_knowledge(e.get(),t,k,10);check(k[7]==1&&k[8]==0,"first pass: bump not learned, or used already");
        double ev[12]{};int events=0,stored=0;while(ld_bump_event(e.get(),ev,12)){++events;stored+=ev[1]==1&&ev[10]==1&&ev[11]==1;}
        check(events>=1&&stored==1,"bump event not logged and stored");
        drive(true,t);ld_road_knowledge(e.get(),t,k,10);check(k[7]==1&&k[8]==1,"second pass did not confirm the bump");
        // Third pass: 100 m along, the bump is 50 m ahead on the route.
        double out[10]{};const double s=t;double nextFix=s;bool seen=false;
        for(double c=s;(c-s)*v<=100;c+=.01) {
            double acc[3]={0,9.81,0},lin[3]={0,0,0},gyro[3]={0,0,0};ld_imu(e.get(),c,acc,gyro,lin,1);
            if(c>=nextFix){const double y=(c-s)*v;ld_gps(e.get(),c,v,.5);ld_navigation_fix(e.get(),1,c,lat(y),lon(0),5,v*3.6,.5,1,0,1);
                ld_route_maneuvers(e.get(),c,nullptr,0);nextFix+=1;
                if(ld_road_knowledge(e.get(),c,out,10)&&out[1]&&y>=95){seen=out[6]==1&&std::abs(out[5]-(bumpY-y))<12;}}
        }
        check(seen,"learned bump not reported ahead on the route");
        double saved[10]{};check(ld_bumps_learned(e.get(),saved,10)==1&&saved[3]==2&&saved[4]==0,"learned bump not exported for saving");
        // Restored on a fresh engine (the next drive on this phone).
        std::unique_ptr<ld_engine,decltype(&ld_destroy)> next(ld_create(),ld_destroy);
        check(ld_bump_restore(next.get(),saved[0],saved[1],saved[2],int(saved[3]),int(saved[4]))&&ld_bumps_learned(next.get(),saved,10)==1,"restore failed");
        check(std::string(ld_bump_reason(4)).find("rear")!=std::string::npos,"reason text");});
    test("the road walked by the scout carries learned bumps (no destination)",[]{
        JunctionScout s;
        const int64_t ids[]={1,2,3};const double ll[]={lat(0),lon(0),lat(200),lon(0),lat(400),lon(0)};check(s.addWay(1,6,ids,ll,3),"road");
        s.update(1,lat(20),lon(0),0,true,20,5);
        size_t n=0;const auto* path=s.path(n);check(n>=2&&std::abs(path[0].lat-lat(20))<1e-9,"path not recorded");
        BumpMap m;m.felt(1,lat(120),lon(1),0);m.felt(100,lat(120),lon(-1),0);
        const auto hit=m.along(path,n,400);check(hit.learned&&std::abs(hit.distanceM-100)<2,"learned bump along the scout path");});
    std::cout<<passed<<" road knowledge groups passed\n";if(failed)std::cerr<<failed<<" groups failed\n";return failed?1:0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
