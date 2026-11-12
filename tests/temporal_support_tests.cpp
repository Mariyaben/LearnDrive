#include "learndrive/coaching/TemporalSupport.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ld::coaching;
namespace {
int passed=0;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void test(const char* name,F block){block();++passed;std::cout<<"PASS "<<name<<'\n';}
TrackingObservation actor(double t,uint64_t id=7,int space=1){
    TrackingObservation a;a.key={space,1,id};a.time=t;a.confidence=.85;a.category=0;
    a.x=.3f;a.y=.3f;a.w=.15f;a.h=.25f;a.relevant=true;return a;
}
}
int main(){try {
    test("independent channels retain original capture clock",[]{
        ObservationSynchronizer s;s.beginSource(1);
        check(s.accept(ObservationChannel::Camera,1,1,1,1.1)==ObservationResult::Accepted,"fresh camera rejected");
        check(s.accept(ObservationChannel::Camera,1,1,1,1.2)==ObservationResult::Duplicate,"tick recounted frame");
        check(s.accept(ObservationChannel::Telemetry,1,1,1.2,1.2)==ObservationResult::Accepted,"camera duplicate blocked telemetry");
        check(s.accept(ObservationChannel::Navigation,1,1,1.0,2.0,3)==ObservationResult::Accepted,"route clock incorrectly uses camera age");
        check(s.accept(ObservationChannel::Camera,1,2,1.01,1.5)==ObservationResult::Stale,"capture re-dated as completion");
    });
    test("out-of-order inconsistent sequence future and source rejected",[]{
        ObservationSynchronizer s;s.beginSource(2);
        check(s.accept(ObservationChannel::Camera,2,4,2,2)==ObservationResult::Accepted,"fixture");
        check(s.accept(ObservationChannel::Camera,2,5,1.9,2)==ObservationResult::OutOfOrder,"older capture accepted");
        check(s.accept(ObservationChannel::Camera,2,3,2.1,2.1)==ObservationResult::OutOfOrder,"older sequence accepted");
        check(s.accept(ObservationChannel::Camera,1,6,2.1,2.1)==ObservationResult::Invalid,"old source accepted");
        check(s.accept(ObservationChannel::Camera,2,6,3,2.5)==ObservationResult::Invalid,"future capture accepted");
        s.beginSource(3);check(s.accept(ObservationChannel::Camera,3,1,3,3)==ObservationResult::Accepted,"new source could not reset sequence");
    });
    test("same numeric actor in another namespace is not the same event",[]{
        ObjectTracker t;t.beginSource(1);auto a=actor(1),b=actor(1,7,2);
        check(t.observe(a,1)==ObservationResult::Accepted&&t.observe(b,1)==ObservationResult::Accepted,"namespaces conflated");
        check(t.count(1)==2,"one identity lost");check(t.observe(a,1.1)==ObservationResult::Duplicate,"one observation counted twice");
        check(t.find(a.key,1.1)->observations==1,"duplicate advanced dwell");
        auto old=a;old.time=.9;check(t.observe(old,1)==ObservationResult::OutOfOrder,"old box accepted");
    });
    test("position history survives but never becomes live evidence",[]{
        ObjectTracker t;t.beginSource(1);auto a=actor(1);t.observe(a,1);a.time=1.3;a.x=.32f;t.observe(a,1.3);
        const auto* history=t.find(a.key,1.3);check(history&&history->count==2&&history->continuous,"history/continuity missing");
        check(history->history[0].x!=history->latest.x,"positions collapsed to one frame");
        check(t.find(a.key,2)!=nullptr,"history disappeared before retention bound");
        check(!ObjectTracker::current(history->latest,2),"historical actor became live");
        a.time=2.2;t.observe(a,2.2);check(!t.find(a.key,2.2)->continuous,"missing observations extended identity continuity");
    });
    test("unknown pose and class changes do not produce tracking evidence",[]{
        ObjectTracker t;t.beginSource(1);auto a=actor(1);t.observe(a,1);
        a.time=1.1;a.w=0;check(t.observe(a,1.1)==ObservationResult::Invalid,"zero-width box accepted");
        a=actor(1.1);a.category=2;check(t.observe(a,1.1)==ObservationResult::Invalid,"same ID changed class silently");
        a=actor(1.1);a.confidence=std::numeric_limits<double>::quiet_NaN();check(t.observe(a,1.1)==ObservationResult::Invalid,"NaN confidence accepted");
        t.expire(.5);check(t.find(actor(1).key,1)!=nullptr,"backward expiry erased newer state");
    });
    test("ten-second history stays bounded across variable frame intervals",[]{
        ObjectTracker t;t.beginSource(1);auto a=actor(0);
        for(int i=0;i<150;++i){a.time=i*.31;t.observe(a,a.time);}
        auto* h=t.find(a.key,a.time);check(h&&h->count<=41,"unbounded history");
        for(size_t i=0;i<h->count;++i){const auto index=(h->cursor+h->history.size()-h->count+i)%h->history.size();check(a.time-h->history[index].time<=10,"expired geometry retained");}
        t.expire(a.time+10);check(t.count(a.time+10)==0,"history never expires");
        t.beginSource(2);check(!t.find(a.key,a.time),"old source geometry retained");
    });
    test("motion trend needs a contiguous valid measured interval",[]{
        VehicleStateEstimator v;v.beginSource(1);
        check(v.trend(0)==MotionTrend::Unknown,"missing speed inferred");
        for(int i=0;i<=20;++i)v.observe(1,i*.1,35-i*.3,true,i*.1);
        check(v.trend(2)==MotionTrend::Decreasing,"supported decreasing speed lost");
        check(v.trend(2.4)==MotionTrend::Unknown,"stale speed trend still active");
        v.observe(1,2.1,0,false,2.1);check(v.trend(2.1)==MotionTrend::Unknown,"unknown speed treated as zero response");
        v.observe(1,3.0,30,true,3.0);check(v.trend(3)==MotionTrend::Unknown,"gap bridged into a response");
        check(v.observe(1,2.9,20,true,3)==ObservationResult::OutOfOrder,"old speed accepted");
    });
    test("jump uncertainty and source reset cannot become driver response",[]{
        VehicleStateEstimator v;v.beginSource(1);
        for(int i=0;i<=20;++i)v.observe(1,i*.1,30,true,i*.1);
        check(v.trend(2)==MotionTrend::Steady,"steady interval lost");
        v.observe(1,2.1,200,true,2.1);check(v.trend(2.1)==MotionTrend::Unknown,"implausible jump became acceleration");
        v.beginSource(2);check(v.trend(2.2)==MotionTrend::Unknown,"old source trend retained");
        check(v.observe(1,2.2,30,true,2.2)==ObservationResult::Invalid,"old source telemetry accepted");
        for(int i=0;i<=200;++i)v.observe(2,3+i*.1,20+i*.05,true,3+i*.1);
        check(v.sampleCount(23)<=101,"unbounded telemetry retention");
        check(v.trend(23)==MotionTrend::Steady,"small speed noise became response");
    });
    test("stepped speed readings still show a learner slowing down or speeding up",[]{
        // Whole km/h twice a second (a replay's readings), read 100 times a second: 24 -> 9 km/h.
        const double steps[]={24,24,23,22,20,17,15,12,11,9};
        VehicleStateEstimator v;v.beginSource(1);bool decreasing=false;
        for(int i=0;i<500;++i){const double t=i*.01;v.observe(1,t,steps[std::min(9,int(t/.5))],true,t);
            if(t>=2.5)decreasing|=v.trend(t)==MotionTrend::Decreasing;}
        check(decreasing,"braking from 24 to 9 km/h in stepped readings was never seen as slowing");
        check(v.trend(4.99)==MotionTrend::Decreasing,"still slowing at the end of the braking");
        // The phone's filter: smooth between GPS fixes, a 3 km/h correction at each one-second fix.
        VehicleStateEstimator p;p.beginSource(1);
        for(int i=0;i<=120;++i){const double t=i/30.;p.observe(1,t,40-1.5*t-3*std::floor(t),true,t);}
        check(p.trend(4)==MotionTrend::Decreasing,"GPS corrections of 3 km/h hid the slowing");
        VehicleStateEstimator up;up.beginSource(1);
        for(int i=0;i<400;++i){const double t=i*.01;up.observe(1,t,10+2*std::floor(t/.5),true,t);}
        check(up.trend(3.99)==MotionTrend::Increasing,"stepped readings hid the speeding up");
        // A glitch is still not driving: 30 km/h for two seconds, then one reading of 75.
        VehicleStateEstimator g;g.beginSource(1);
        for(int i=0;i<=200;++i)g.observe(1,i*.01,30,true,i*.01);
        g.observe(1,2.01,75,true,2.01);check(g.trend(2.01)==MotionTrend::Unknown,"a 45 km/h jump in one reading became a trend");
        // ...nor is a 12 km/h jump between two readings of a smoothly changing speed.
        VehicleStateEstimator s;s.beginSource(1);
        for(int i=0;i<=200;++i)s.observe(1,i*.01,30+.01*i,true,i*.01);
        s.observe(1,2.01,44.1,true,2.01);check(s.trend(2.01)==MotionTrend::Unknown,"a 12 km/h jump in 10 ms became a trend");
    });
    std::cout<<passed<<" temporal support groups passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
