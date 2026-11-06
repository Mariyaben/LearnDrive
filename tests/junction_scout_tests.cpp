// Junction-ahead without a destination: synthetic road geometry (metres around a
// Kerala-latitude origin converted to lat/lon) and GPS fixes.
#include "learndrive/navigation/JunctionScout.hpp"
#include "learndrive/c_api.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace ld::navigation;
namespace {
int passed=0;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void test(const char* name,F f){f();++passed;std::cout<<"PASS "<<name<<'\n';}
constexpr double lat0=12.31,lon0=75.09,pi=3.14159265358979323846;
double lat(double y){return lat0+y/110540;}
double lon(double x){return lon0+x/(111320*std::cos(lat0*pi/180));}
void way(JunctionScout& s,int64_t id,int cls,std::vector<std::pair<int64_t,std::pair<double,double>>> pts){
    std::vector<int64_t> ids;std::vector<double> ll;for(auto& [n,xy]:pts){ids.push_back(n);ll.push_back(lat(xy.second));ll.push_back(lon(xy.first));}
    check(s.addWay(id,cls,ids.data(),ll.data(),ids.size()),"way");}
// Driving north (bearing 0) along x=0 at speed 20 km/h, fix at y metres.
JunctionAhead at(JunctionScout& s,double t,double y,double bearing=0){return s.update(t,lat(y),lon(0),bearing,true,20,5);}
}
int main(){try{
    test("a minor side road joining is not a lesson; the road ending at a T is",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{0,250}}});   // our road ends at node 3
        way(s,2,Service,{{2,{0,100}},{4,{-80,100}}});              // service lane joins at 100 m
        way(s,3,Tertiary,{{5,{-200,250}},{3,{0,250}},{6,{200,250}}}); // T: more important road
        auto j=at(s,1,10);check(j.valid&&j.nodeId==3&&j.roadEnds,"T-junction not found past a minor side road");
        check(std::abs(j.distanceM-240)<2,"distance along the road wrong");});
    test("a more important road or a real crossroads is prepared for; travel direction matters",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,150}},{3,{0,300}}});
        way(s,2,Secondary,{{4,{-150,150}},{2,{0,150}},{5,{150,150}}}); // crossing a secondary road at 150 m
        auto j=at(s,1,20);check(j.valid&&j.nodeId==2&&!j.roadEnds&&std::abs(j.distanceM-130)<2,"crossing a more important road missed");
        // Driving south on the same road from y=280: the same crossing is ahead at 130 m.
        JunctionScout r;way(r,1,Residential,{{1,{0,0}},{2,{0,150}},{3,{0,300}}});way(r,2,Secondary,{{4,{-150,150}},{2,{0,150}},{5,{150,150}}});
        auto k=r.update(1,lat(280),lon(0),180,true,20,5);check(k.valid&&k.nodeId==2&&std::abs(k.distanceM-130)<2,"direction of travel ignored");});
    test("continues onto the connecting road and gives up beyond 400 m or with no matching road",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,100}}});
        way(s,2,Residential,{{2,{0,100}},{3,{5,200}},{4,{5,350}}}); // the road carries on as another way
        way(s,3,Primary,{{6,{-100,350}},{4,{5,350}},{7,{100,350}}});
        auto j=at(s,1,10);check(j.valid&&j.nodeId==4&&std::abs(j.distanceM-340)<5,"did not follow the road onto its continuation");
        JunctionScout far;way(far,1,Residential,{{1,{0,0}},{2,{0,900}}});way(far,2,Primary,{{3,{-50,900}},{2,{0,900}},{4,{50,900}}});
        check(!at(far,1,10).valid,"looked further than 400 m");
        check(!at(s,2,10,90).valid,"matched a road the car is not travelling along");});
    test("the result expires without fresh GPS",[]{JunctionScout s;way(s,1,Residential,{{1,{0,0}},{2,{0,100}}});
        way(s,2,Primary,{{3,{-50,100}},{2,{0,100}},{4,{50,100}}});
        check(at(s,10,10).valid&&s.current(11).valid,"fresh result");check(!s.current(13).valid,"stale junction kept");});
    test("C API: no route, a junction ahead becomes a generic junction lesson context",[]{
        std::unique_ptr<ld_engine,decltype(&ld_destroy)> e(ld_create(),ld_destroy);
        check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"camera source");
        const int64_t ids[]={1,2};const double ll[]={lat(0),lon(0),lat(200),lon(0)};check(ld_road_way(e.get(),1,Residential,ids,ll,2),"road");
        const int64_t cross[]={3,2,4};const double cll[]={lat(200),lon(-100),lat(200),lon(0),lat(200),lon(100)};check(ld_road_way(e.get(),2,Primary,cross,cll,3),"cross");
        ld_navigation_fix(e.get(),1,5,lat(20),lon(0),5,20,.5,1,0,1); // Returns route acceptance; the scout updates regardless.
        double out[4]{};check(ld_road_junction(e.get(),5.1,out,4)&&out[0]==1&&std::abs(out[1]-180)<2&&out[2]==2,"junction not exposed");});
    test("a mapped speed bump and a sharp bend on the road ahead are reported before the junction",[]{JunctionScout s;
        // North 120 m, then a 90-degree bend right (drawn over ~20 m), east 200 m to a T with a primary road.
        way(s,1,Residential,{{1,{0,0}},{2,{0,60}},{3,{0,120}},{4,{6,134}},{5,{20,140}},{6,{120,140}},{7,{220,140}}});
        way(s,2,Primary,{{8,{220,0}},{7,{220,140}},{9,{220,300}}});
        s.addBump(2);
        auto j=at(s,1,10);
        check(j.matched&&j.bumpNode==2&&std::abs(j.bumpDistanceM-50)<2,"speed bump ahead missed");
        check(j.bendNode!=0&&j.bendRight&&j.bendDistanceM>100&&j.bendDistanceM<125,"sharp bend right missed or misplaced");
        check(j.valid&&j.nodeId==7,"junction after the bend lost");
        // A gently curving road (about 15 degrees over 100 m) is not a sharp bend; no bump mapped.
        JunctionScout g;way(g,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{8,150}},{4,{26,250}}});
        auto k=at(g,1,10);check(k.matched&&!k.bendNode&&!k.bumpNode,"gentle curve reported as a sharp bend");
        check(g.current(2).matched,"matched road without a junction is not reported");});
    test("a road that bends where two map segments meet is followed to the junction beyond",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,100}}});                 // north, ends at the bend
        way(s,2,Residential,{{2,{0,100}},{3,{100,100}},{4,{200,100}}}); // carries on east (a 90-degree bend)
        way(s,3,Primary,{{5,{200,0}},{4,{200,100}},{6,{200,200}}});     // T with a primary road
        auto j=at(s,1,10);check(j.valid&&j.nodeId==4&&std::abs(j.distanceM-290)<3,"walk stopped at a bend on a way boundary");
        check(j.bendNode!=0&&j.bendRight,"bend on the way boundary not reported");});
    test("where our road turns away and another road carries straight on, that is a junction",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{100,100}}});  // bends right at node 2
        way(s,2,Residential,{{2,{0,100}},{4,{0,200}}});               // another road straight on
        auto j=at(s,1,10);check(j.valid&&j.nodeId==2&&!j.roadEnds&&std::abs(j.distanceM-90)<2,"junction on a bending road missed");
        // A driveway straight on is not a junction lesson; the bend is still reported.
        JunctionScout d;way(d,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{100,100}}});way(d,2,Service,{{2,{0,100}},{4,{0,140}}});
        auto k=at(d,1,10);check(!k.valid&&k.matched&&k.bendNode!=0,"driveway made a junction or hid the bend");
        // Our road straight on with a minor road leaving at an angle stays quiet (unchanged).
        JunctionScout m;way(m,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{0,200}}});way(m,2,Residential,{{2,{0,100}},{4,{100,100}}});
        check(!at(m,1,10).valid,"minor side road on a straight road became a lesson");});
    test("a junction is judged the same from far back and from just before it",[]{
        // A main road crossed by a minor road: not a lesson, whether 90 m or 5 m away (the road
        // behind the car is not a side road).
        JunctionScout s;way(s,1,Primary,{{1,{0,0}},{2,{0,100}},{3,{0,200}}});way(s,2,Residential,{{4,{-100,100}},{2,{0,100}},{5,{100,100}}});
        check(!at(s,1,10).valid&&!at(s,2,95).valid,"minor crossing on a main road became a lesson near it");
        // Crossing a more important road: a lesson from both places.
        JunctionScout r;way(r,1,Residential,{{1,{0,0}},{2,{0,100}},{3,{0,200}}});way(r,2,Primary,{{4,{-100,100}},{2,{0,100}},{5,{100,100}}});
        check(at(r,1,10).valid&&at(r,2,95).valid,"crossing a main road lost near it");});
    test("a road ending at a T is still a T with a driveway straight ahead",[]{JunctionScout s;
        way(s,1,Residential,{{1,{0,0}},{2,{0,100}}});
        way(s,2,Tertiary,{{3,{-100,100}},{2,{0,100}},{4,{100,100}}});
        way(s,3,Service,{{2,{0,100}},{5,{0,160}}}); // driveway opposite
        auto j=at(s,1,10);check(j.valid&&j.nodeId==2&&j.roadEnds&&std::abs(j.distanceM-90)<2,"driveway hid the end of the road");});
    std::cout<<passed<<" junction scout groups passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL "<<ex.what()<<'\n';return 1;}}
