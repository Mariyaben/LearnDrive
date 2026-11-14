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
        // Rough by the detector's own measure: background vibration above 1.5 m/s^2 RMS (+-3 m/s^2 here).
        check(!accepted(run(Kind::bump,20,3.)),"bump on a rough road accepted");
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
    test("route line: a bend that is not sharp is found with the speed it is comfortable at; a gentle curve and a wobbly line are not bends",[]{
        // North 200 m, a 50-degree bend to the left on a 60 m radius (52 m of road), then straight on.
        std::vector<double> ll;
        for(int i=0;i<=20;++i){ll.push_back(lat(i*10.));ll.push_back(lon(0));}
        const double R=60,total=50*pi/180;
        for(int k=1;k<=10;++k){const double a=k*total/10;ll.push_back(lat(200+R*std::sin(a)));ll.push_back(lon(-(R-R*std::cos(a))));}
        const double ex=-(R-R*std::cos(total)),ny=200+R*std::sin(total);
        for(int i=1;i<=30;++i){ll.push_back(lat(ny+i*10*std::cos(total)));ll.push_back(lon(ex-i*10*std::sin(total)));}
        RouteLine r;check(r.load(ll.data(),ll.size()/2)&&r.bendCount()==1,"a 50-degree bend on a 60 m radius was not found, or found twice");
        BumpMap none;for(int k=0;k<3;++k)r.fix(1+k,lat(60+k*8.),lon(0),5,0,true,30);
        r.maneuvers(3.2,nullptr,0);
        const auto a=r.ahead(3.3,none);
        std::cout<<"    50 degrees on a 60 m radius: "<<a.bendDistanceM<<" m ahead, "<<(a.bendRight?"right":"left")<<(a.bendSharp?", sharp":", not sharp")<<", comfortable at "<<a.bendAdviseKmh<<" km/h"<<std::endl;
        check(a.bendId&&!a.bendRight&&!a.bendSharp,"the bend's direction or kind is wrong");
        check(a.bendDistanceM>105&&a.bendDistanceM<140,"the bend is not placed where the road starts turning");
        check(a.bendAdviseKmh>28&&a.bendAdviseKmh<40,"the bend's comfortable speed is off (60 m radius: about 34 km/h)");
        // A sharp one (90 degrees on a 12 m radius) is found once, marked sharp, with a second-gear speed.
        std::vector<double> s;for(int i=0;i<=30;++i){s.push_back(lat(i*10.));s.push_back(lon(0));}
        for(int k=1;k<=4;++k){const double b=k*pi/10;s.push_back(lat(300+12*std::sin(b)));s.push_back(lon(12-12*std::cos(b)));}
        for(int i=1;i<=30;++i){s.push_back(lat(312.));s.push_back(lon(12+i*10.));}
        RouteLine q;check(q.load(s.data(),s.size()/2)&&q.bendCount()==1,"a sharp bend was counted twice or lost");
        for(int k=0;k<3;++k)q.fix(1+k,lat(100+k*8.),lon(0),5,0,true,30);
        q.maneuvers(3.2,nullptr,0);
        const auto b=q.ahead(3.3,none);check(b.bendSharp&&b.bendRight&&b.bendAdviseKmh<=22,"a sharp bend is not marked sharp, or its speed is too high");
        // A long gentle curve (25 degrees over 100 m) and a straight road drawn with a wobble are not bends.
        std::vector<double> g;{double x=0,y=0;for(int i=0;i<=40;++i){g.push_back(lat(y));g.push_back(lon(x));
            const double h=std::clamp((i-10)/10.,0.,1.)*25*pi/180;x+=10*std::sin(h);y+=10*std::cos(h);}}
        RouteLine c;check(c.load(g.data(),g.size()/2)&&c.bendCount()==0,"a gentle curve became a bend");
        std::vector<double> z;for(int i=0;i<=60;++i){z.push_back(lat(i*10.));z.push_back(lon(i%2?.6:-.6));}
        RouteLine d;check(d.load(z.data(),z.size()/2)&&d.bendCount()==0,"a wobble in the line became a bend");});
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
    // Elevation along a 2 km route north, sampled every 20 m like Google's Elevation API, with a
    // metre of model noise: flat, an 8 % descent over 300 m, flat, a 12 % climb over 200 m, flat,
    // then a gentle 4 % rise.
    auto profile=[](double s){
        if(s<500)return 50.;if(s<800)return 50-.08*(s-500);if(s<1200)return 26.;if(s<1400)return 26+.12*(s-1200);
        if(s<1600)return 50.;return 50+.04*(s-1600);};
    auto samples=[](auto height,double from,double to,double east=0){Noise n;std::vector<double> v;
        for(double s=from;s<=to+1e-9;s+=20){v.push_back(lat(s));v.push_back(lon(east));v.push_back(height(s)+n.next());}return v;};
    auto northLine=[]{std::vector<double> ll;for(int i=0;i<=200;++i){ll.push_back(lat(i*10.));ll.push_back(lon(0));}return ll;};
    test("route elevation: climbs and descents are found where they start, with their grade; gentle grades and noise are not hills",[&]{
        const auto ll=northLine();RouteLine r;check(r.load(ll.data(),ll.size()/2),"route line");
        const auto v=samples(profile,0,2000);
        check(r.elevation(v.data(),v.size()/3)&&r.elevationKnown()&&r.slopeCount()==2,"not exactly one descent and one climb");
        BumpMap none;double clock=1;
        // Three fixes at y-16, y-8, y (a jump along the line needs two consistent fixes to match again).
        auto at=[&](double y){for(int k=2;k>=0;--k)r.fix(clock++,lat(y-8*k),lon(0),5,0,true,30);return r.ahead(clock-1,none);};
        auto a=at(300);std::cout<<"    at 300 m: grade "<<a.gradePercent<<" %, next "<<a.slopeGradePercent<<" % in "<<a.slopeDistanceM<<" m for "<<a.slopeLengthM<<" m\n";
        check(a.matched&&a.gradeKnown&&std::abs(a.gradePercent)<1.5,"flat road read as a slope");
        check(a.slopeId&&a.slopeDistanceM>185&&a.slopeDistanceM<230,"descent not found where it starts");
        check(std::abs(a.slopeGradePercent+8)<1.5&&a.slopeLengthM>250&&a.slopeLengthM<340,"descent grade or length wrong");
        const uint64_t descent=a.slopeId;
        a=at(600);check(a.slopeId==descent&&a.slopeDistanceM<-80&&a.slopeDistanceM>-110&&std::abs(a.gradePercent+8)<1.5,"on the descent: not reported as on it");
        a=at(1000);std::cout<<"    at 1000 m: next "<<a.slopeGradePercent<<" % (steepest "<<a.slopeSteepestPercent<<") in "<<a.slopeDistanceM<<" m\n";
        check(a.slopeId&&a.slopeId!=descent&&a.slopeDistanceM>180&&a.slopeDistanceM<230&&std::abs(a.slopeGradePercent-12)<2,"steep climb wrong");
        a=at(1500);check(!a.slopeId&&!std::isfinite(a.slopeDistanceM)&&a.gradeKnown,"a gentle 4 % rise became a hill");
        // Refreshed (the app fetched the rest of the route): the same slopes keep their ids.
        RouteLine p;check(p.load(ll.data(),ll.size()/2),"route line");
        const auto first=samples(profile,0,1000);check(p.elevation(first.data(),first.size()/3)&&p.slopeCount()==1,"first stretch");
        for(int k=2;k>=0;--k)p.fix(clock++,lat(300-8*k),lon(0),5,0,true,30);const uint64_t before=p.ahead(clock-1,none).slopeId;
        check(p.elevation(v.data(),v.size()/3)&&p.slopeCount()==2,"refresh");
        for(int k=2;k>=0;--k)p.fix(clock++,lat(310-8*k),lon(0),5,0,true,30);check(before&&p.ahead(clock-1,none).slopeId==before,"slope id changed on refresh");
        // Samples off the route are not this road; a new route forgets the old profile.
        const auto aside=samples(profile,0,2000,60);check(!r.elevation(aside.data(),aside.size()/3)&&!r.elevationKnown(),"samples 60 m off the route used");
        check(r.elevation(v.data(),v.size()/3)&&r.load(ll.data(),ll.size()/2)&&!r.elevationKnown()&&!r.slopeCount(),"new route kept the old profile");});
    test("route line aim: where the road will be a few seconds ahead (aims the detector's zoomed views)",[&]{
        // North 200 m, then a left curve of radius 60 m (to the west).
        std::vector<double> ll;for(int i=0;i<=20;++i){ll.push_back(lat(i*10.));ll.push_back(lon(0));}
        for(int i=1;i<=30;++i){const double a=i*3*pi/180;ll.push_back(lat(200+60*std::sin(a)));ll.push_back(lon(-60+60*std::cos(a)));}
        RouteLine r;check(r.load(ll.data(),ll.size()/2),"route line");
        check(!std::isfinite(r.aim(1,5)),"aimed before the car was matched to the line");
        double clock=1;auto at=[&](double y){for(int k=2;k>=0;--k)r.fix(clock++,lat(y-8*k),lon(0),5,0,true,36);return clock-1;};
        const double straight=r.aim(at(60),5);   // 10 m/s: 50 m ahead, still the straight
        check(std::isfinite(straight)&&std::abs(straight)<.02,"a straight road aimed off-centre");
        const double bend=r.aim(at(185),5);      // 50 m ahead is 35 m into the left curve
        std::cout<<"    aim 50 m ahead: straight "<<straight*180/pi<<" deg, entering the left curve "<<bend*180/pi<<" deg"<<std::endl;
        check(bend>.15&&bend<.6,"the curve ahead was not aimed to the left");
        check(!std::isfinite(r.aim(clock+5,5)),"aimed from a stale fix");});
    test("route elevation: a bridge over a river (the ground model dips under the deck) is not a descent and a climb",[&]{
        const auto ll=northLine();RouteLine r;check(r.load(ll.data(),ll.size()/2),"route line");
        // The banks fall 8 m to the water in 40 m, 160 m of river, and rise again.
        auto river=[](double s){if(s<400)return 30.;if(s<440)return 30-.2*(s-400);if(s<600)return 22.;if(s<640)return 22+.2*(s-600);return 30.;};
        const auto v=samples(river,0,1500);check(r.elevation(v.data(),v.size()/3)&&r.slopeCount()==0,"bridge read as hills");
        // The same descent that stays down is a real descent.
        auto valley=[](double s){if(s<400)return 30.;if(s<500)return 30-.1*(s-400);return 20.;};
        const auto d=samples(valley,0,1500);check(r.elevation(d.data(),d.size()/3)&&r.slopeCount()==1,"real descent dropped");
        // A clump of trees or a building in a radar terrain model: 9 m up and down again within 140 m.
        auto trees=[](double s){const double x=s-600;return 30+(x>-70&&x<70?9*(1-std::abs(x)/70):0);};
        const auto c=samples(trees,0,1500);check(r.elevation(c.data(),c.size()/3)&&r.slopeCount()==0,"trees read as a hill");
        // A real hill: 8 % up for 200 m, 300 m along the top, 8 % down for 200 m.
        auto hill=[](double s){if(s<400)return 30.;if(s<600)return 30+.08*(s-400);if(s<900)return 46.;if(s<1100)return 46-.08*(s-900);return 30.;};
        const auto k=samples(hill,0,1500);check(r.elevation(k.data(),k.size()/3)&&r.slopeCount()==2,"a real hill's climb and descent not both found");});
    test("C API: the map's grade and the next hill along Google's route",[&]{
        std::unique_ptr<ld_engine,decltype(&ld_destroy)> e(ld_create(),ld_destroy);
        check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"camera source");
        const auto ll=northLine();check(ld_route_line(e.get(),ll.data(),ll.size()/2),"route line");
        const auto v=samples(profile,0,2000);check(ld_route_elevation(e.get(),v.data(),v.size()/3)==1,"elevation");
        double cover[4]{};ld_route_elevation_cover(e.get(),cover);std::cout<<"    covers "<<cover[0]<<"-"<<cover[1]<<" m, car "<<cover[2]<<", hills "<<cover[3]<<std::endl;
        check(cover[0]<1&&cover[1]>1980&&cover[2]==-1&&cover[3]==2,"coverage");
        double out[16]{};
        for(int k=0;k<4;++k){const double t=1+k;ld_gps(e.get(),t,30/3.6,.5);ld_navigation_fix(e.get(),1,t,lat(276+8*k),lon(0),5,30,.5,1,0,1);}
        check(ld_road_knowledge(e.get(),4,out,16)&&out[1]==1&&out[10]==1&&std::abs(out[11])<1.5,"grade here");
        check(out[12]>180&&out[12]<230&&std::abs(out[13]+8)<1.5&&out[14]>250&&out[15]==2,"next hill");
        ld_route_elevation_cover(e.get(),cover);check(std::abs(cover[2]-300)<5,"progress");
        check(!ld_route_elevation(e.get(),nullptr,0)&&ld_road_knowledge(e.get(),4,out,16)&&out[10]==0&&!std::isfinite(out[12]),"cleared profile still used");});
    test("the road walked by the scout carries learned bumps (no destination)",[]{
        JunctionScout s;
        const int64_t ids[]={1,2,3};const double ll[]={lat(0),lon(0),lat(200),lon(0),lat(400),lon(0)};check(s.addWay(1,6,ids,ll,3),"road");
        s.update(1,lat(20),lon(0),0,true,20,5);
        size_t n=0;const auto* path=s.path(n);check(n>=2&&std::abs(path[0].lat-lat(20))<1e-9,"path not recorded");
        BumpMap m;m.felt(1,lat(120),lon(1),0);m.felt(100,lat(120),lon(-1),0);
        const auto hit=m.along(path,n,400);check(hit.learned&&std::abs(hit.distanceM-100)<2,"learned bump along the scout path");});
    std::cout<<passed<<" road knowledge groups passed\n";if(failed)std::cerr<<failed<<" groups failed\n";return failed?1:0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
