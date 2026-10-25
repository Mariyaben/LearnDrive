#include "learndrive/navigation.hpp"
#include "learndrive/c_api.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
static std::atomic<size_t> allocations{0};
void* operator new(size_t size){++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using namespace ld;using namespace ld::navigation;
static int checks=0;
static void check(bool b,const char* why){++checks;if(!b){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
static Coordinate xy(double x,double y){return {10.+y/6371008.8*180/pi,76.+x/6371008.8*180/pi/std::cos(10*pi/180)};}
static Fix fixAt(double time,double x=0,double y=0,double bearing=0){const auto p=xy(x,y);return {time,p.latitude,p.longitude,3,45,bearing,.5,true,true};}
static std::vector<Coordinate> rightCurve(bool dense=false) {
    std::vector<Coordinate> p;
    for(double y=0;y<90;y+=dense?2:10)p.push_back(xy(0,y));
    p.push_back(xy(0,90));
    const double angle=50*pi/180;
    for(double s=10;s<=150;s+=dense?2:10)p.push_back(xy(s*std::sin(angle),90+s*std::cos(angle)));
    return p;
}
int main() {
    RouteCoach c;const auto p=rightCurve();check(c.load(1,p.data(),p.size(),nullptr,0),"load curve");
    check(c.context(0).routeId==1&&!c.context(0).valid&&c.context(0).match==Match::noFix,"no GPS no context");
    c.fix(fixAt(1));check(c.evaluate(1,false,false)==Prompt::none,"two GPS fixes required");
    c.fix(fixAt(2));auto m=c.context(2);check(m.valid&&m.feature==Feature::rightBend,"50 degree right bend classified");
    check(std::abs(m.distanceM-90)<6,"bend about 90m ahead");
    check(c.evaluate(2,false,false)==Prompt::mapRightBend,"45kmh 90m bend preparation");
    check(c.evaluate(2,true,false)==Prompt::none&&c.evaluate(2,false,true)==Prompt::none,"driver braking/shock suppress map cue");
    check(!std::isfinite(m.approachSpeedLimitKmh),"no invented map speed limit");
    check(c.evaluate(2,false,false)==Prompt::mapRightBend,"suppression does not consume encounter");
    c.accepted(2);check(c.evaluate(2,false,false)==Prompt::none,"accepted feature consumed");
    check(c.context(5).match==Match::stale&&!c.context(5).valid,"stale GPS pauses map context");
    c.invalidateFix();check(!c.context(2).valid,"source switch requires new fixes");
    check(!c.fix(fixAt(1,200,0)),"off route rejected");
    auto f=fixAt(2);f.accuracyM=40;check(!c.fix(f),"poor GPS accuracy rejected");
    c.clear();check(c.context(2).match==Match::noRoute,"clear route");
    const auto dense=rightCurve(true);check(c.load(2,dense.data(),dense.size(),nullptr,0),"dense shape");c.fix(fixAt(1));c.fix(fixAt(2));
    check(c.context(2).feature==Feature::rightBend&&std::abs(c.context(2).distanceM-90)<6,"sampling density stable");
    f=fixAt(3);f.speedValid=false;c.fix(f);check(c.evaluate(3,false,false)==Prompt::none,"unknown speed cannot trigger map cue");
    f=fixAt(4);f.speedAccuracyMps=5;c.fix(f);check(c.evaluate(4,false,false)==Prompt::none,"uncertain speed suppressed");
    check(!c.fix(fixAt(3)),"out of order GPS rejected");
    c.invalidateFix();check(!c.fix(fixAt(5,0,0,180)),"wrong way heading rejected");
    c.invalidateFix();f=fixAt(6);f.bearingValid=false;check(!c.fix(f),"moving without heading withheld");
    c.clear();const Maneuver turn{9,10};check(c.load(3,p.data(),p.size(),&turn,1),"router maneuver accepted");
    c.fix(fixAt(10,0,50));c.fix(fixAt(11,0,50));m=c.context(11);
    check(m.feature==Feature::rightTurn&&std::abs(m.distanceM-40)<1,"turn metadata overrides curve");
    f=fixAt(12,0,45);f.speedKmh=25;c.fix(f);check(c.evaluate(12,false,false)==Prompt::mapRightTurn50,"measured turn distance spoken approximately");
    f=fixAt(13,0,75);f.speedKmh=45;c.fix(f);check(c.evaluate(13,false,false)==Prompt::none,"too late for a full cue");
    c.invalidateFix();f=fixAt(14,0,80);f.speedKmh=8;c.fix(f);f.time=15;c.fix(f);
    check(c.evaluate(15,false,false)==Prompt::mapRightTurnNear,"low-speed learner receives ten-metre cue with accurate GPS and enough lead");
    f.time=16;f.accuracyM=9;c.fix(f);
    check(c.evaluate(16,false,false)==Prompt::mapRightTurn,"uncertain GPS gets generic distance rather than false precision");
    f.time=17;f.speedKmh=0;c.fix(f);check(c.evaluate(17,false,false)==Prompt::none,"stationary driver is not nagged with turn cues");
    check(!mayInterrupt(Prompt::mapRightTurnNear,Prompt::mapLeftBend),"distance cues do not interrupt speech");
    std::vector<Coordinate> straight{xy(0,0),xy(0,100),xy(0,200)};c.load(4,straight.data(),straight.size(),nullptr,0);c.fix(fixAt(1));c.fix(fixAt(2));
    check(c.context(2).feature==Feature::none,"straight shape invents no junction");
    std::vector<Coordinate> repeated{xy(0,0),xy(0,0),xy(0,100),xy(0,100),xy(0,200),xy(0,200)};
    const Maneuver repeatedTurn{3,9};
    check(c.load(43,repeated.data(),repeated.size(),&repeatedTurn,1),"real router repeated vertices accepted");
    c.fix(fixAt(1));c.fix(fixAt(2));
    check(c.context(2).feature==Feature::rightTurn&&std::abs(c.context(2).distanceM-100)<1&&c.context(2).maneuverShapeIndex==3,"duplicate removal preserves original maneuver index and distance");
    const std::vector<Coordinate> collapsed{xy(0,0),xy(0,0),xy(0,0)};
    check(!c.load(44,collapsed.data(),collapsed.size(),nullptr,0),"entirely collapsed route rejected");
    std::vector<Coordinate> parallel{xy(0,0),xy(0,100),xy(0,200),xy(20,200),xy(20,100),xy(20,0)};
    c.load(40,parallel.data(),parallel.size(),nullptr,0);
    f=fixAt(3,10,100);f.accuracyM=12;check(!c.fix(f)&&c.context(3).match==Match::ambiguous,"nearby parallel route sections are ambiguous");
    const Maneuver roundabout{9,26};c.load(41,p.data(),p.size(),&roundabout,1);c.fix(fixAt(4));c.fix(fixAt(5));
    check(c.context(5).feature==Feature::none&&c.context(5).maneuverType==26,"roundabout retained visually without a false bend lesson");
    std::vector<Coordinate> north;
    const double incoming=-20*pi/180,outgoing=30*pi/180;
    for(double s=0;s<=90;s+=10)north.push_back(xy(s*std::sin(incoming),s*std::cos(incoming)));
    for(double s=10;s<=150;s+=10)north.push_back(xy(90*std::sin(incoming)+s*std::sin(outgoing),90*std::cos(incoming)+s*std::cos(outgoing)));
    c.load(42,north.data(),north.size(),nullptr,0);c.fix(fixAt(6,0,0,340));c.fix(fixAt(7,0,0,340));
    check(c.context(7).feature==Feature::rightBend,"north/360 degree wrap preserves bend direction");
    auto bad=p;bad[2].latitude=std::numeric_limits<double>::quiet_NaN();check(!c.load(5,bad.data(),bad.size(),nullptr,0)&&!c.routeId(),"invalid route fails closed");
    c.load(6,p.data(),p.size(),nullptr,0);c.fix(fixAt(100));c.fix(fixAt(101));
    const auto before=allocations.load();const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<10000;++i){(void)c.context(101);(void)c.evaluate(101,false,false);}
    const auto end=std::chrono::steady_clock::now();check(allocations.load()==before,"new map evaluation performs zero heap allocations");
    std::cout<<"map context + policy mean us: "<<std::chrono::duration<double,std::micro>(end-start).count()/10000<<'\n';
    check(mayInterrupt(Prompt::brakeHarder,Prompt::mapRightBend)&&!mayInterrupt(Prompt::mapRightBend,Prompt::brakeHarder),"emergency priority over map speech");
    auto* e=ld_create();check(e!=nullptr,"create engine");check(ld_guidance_begin(e,1,LD_SOURCE_CAMERA,0),"camera source");
    std::vector<double> coords;for(auto a:p){coords.push_back(a.latitude);coords.push_back(a.longitude);}
    check(ld_navigation_route(e,10,coords.data(),p.size(),nullptr,0),"load route C ABI");
    const auto pos=xy(0,0);
    check(ld_navigation_fix(e,1,1,pos.latitude,pos.longitude,3,45,.5,1,0,1),"first live fix");
    check(ld_navigation_fix(e,1,2,pos.latitude,pos.longitude,3,45,.5,1,0,1),"second live fix");
    ld_guidance_request q{};q.source_id=1;q.now=2;q.telemetry.time=2;q.telemetry.speed=0;q.telemetry.speed_valid=1;q.active=q.audio_ready=1;
    ld_guidance_result out{};check(ld_guidance_decide(e,&q,&out)&&out.prompt==48&&out.origin==LD_GUIDANCE_NAVIGATION&&out.eligible,"shared arbiter accepts map cue with no camera frame and actual GPS speed");
    check(ld_guidance_pending(e,out.sequence,out.prompt,2),"pending map cue available before playback");
    ld_navigation_fix(e,1,2.1,pos.latitude,pos.longitude,50,45,.5,1,0,1);
    check(!ld_guidance_pending(e,out.sequence,out.prompt,2.1)&&!ld_guidance_accepted(e,out.sequence,out.prompt,2.1),"new poor GPS fix invalidates pending speech");
    ld_navigation_fix(e,1,2.2,pos.latitude,pos.longitude,3,45,.5,1,0,1);ld_navigation_fix(e,1,3.2,pos.latitude,pos.longitude,3,45,.5,1,0,1);
    q.now=q.telemetry.time=3.2;ld_guidance_decide(e,&q,&out);
    check(ld_guidance_accepted(e,out.sequence,out.prompt,3.2),"acknowledge refreshed map cue");
    check(!ld_audio_allowed(e,47,4)&&ld_audio_allowed(e,42,4),"global cooldown preserved; emergency bypass");
    check(!ld_navigation_route(e,11,nullptr,0,nullptr,0),"malformed replacement route rejected");
    double context[17];ld_navigation_context(e,4,context,17);check(context[0]==0,"malformed replacement cannot retain old route");
    check(ld_guidance_begin(e,2,LD_SOURCE_RECORDING,4),"recording source");
    check(!ld_navigation_fix(e,2,5,pos.latitude,pos.longitude,3,45,.5,1,0,1),"recorded vehicle never inherits phone position");
    check(!ld_navigation_fix(e,1,5,pos.latitude,pos.longitude,3,45,.5,1,0,1),"late old-source fix rejected");
    ld_destroy(e);

    // Structured provider turns must never invent maneuver anchors from a
    // polyline, trust a receipt timestamp as GPS, or recycle a passed step.
    ProviderTurnCoach provider;
    TurnInput input{1,100,1,1,2,TurnState::navigating,4,TurnKind::left,150,14};
    check(provider.update(input),"normalized provider turn accepted");
    check(provider.evaluate(2,false,false)==Prompt::none,"provider receipt cannot substitute for GPS");
    check(provider.fix(fixAt(1)),"provider independent first GPS fix");
    check(provider.evaluate(2,false,false)==Prompt::none,"provider requires two measured GPS fixes");
    check(provider.fix(fixAt(2)),"provider independent second GPS fix");
    check(provider.evaluate(2,false,false)==Prompt::mapLeftTurn,"provider gets early generic left preparation");
    auto providerContext=provider.context(2);
    check(providerContext.valid&&!std::isfinite(providerContext.progressM)&&providerContext.maneuverShapeIndex==-1&&
        providerContext.maneuverType==0&&!std::isfinite(providerContext.approachSpeedLimitKmh),"provider invents no spatial progress, shape index, Valhalla type or speed limit");
    check(provider.evaluate(2,true,false)==Prompt::none&&provider.evaluate(2,false,true)==Prompt::none,"braking and shock suppress provider coaching");
    provider.evaluate(2,false,false);provider.accepted(2);
    provider.unavailable();check(!provider.context(2).valid,"temporary provider loss invalidates context");
    ++input.sequence;check(provider.update(input),"same session recovers after temporary loss");
    check(provider.evaluate(2,false,false)==Prompt::none,"temporary loss cannot repeat an accepted encounter");
    input.receivedAt=12;++input.sequence;provider.fix(fixAt(11));provider.fix(fixAt(12));provider.update(input);
    check(provider.evaluate(12,false,false)==Prompt::none,"accepted encounter remains consumed after cooldown");
    input.step=5;input.kind=TurnKind::right;++input.sequence;
    check(provider.update(input)&&provider.evaluate(12,false,false)==Prompt::mapRightTurn,"advanced step creates a new encounter");
    auto old=input;old.step=4;++old.sequence;check(!provider.update(old),"older step cannot resurrect a passed turn");
    old=input;--old.sequence;check(!provider.update(old),"out-of-order provider sequence rejected");
    old=input;old.routeEpoch=0;++old.sequence;check(!provider.update(old),"invalid route epoch rejected");
    input.state=TurnState::rerouting;++input.sequence;provider.update(input);
    check(provider.evaluate(12,false,false)==Prompt::none&&!provider.context(12).valid,"rerouting immediately pauses turn coaching");
    input.state=TurnState::navigating;input.routeEpoch=2;input.step=0;++input.sequence;
    check(provider.update(input)&&provider.evaluate(12,false,false)==Prompt::mapRightTurn,"new route epoch safely restarts step numbering");
    old=input;old.routeEpoch=1;++old.sequence;check(!provider.update(old),"older route revision rejected despite newer sequence");
    input.kind=TurnKind::roundabout;++input.sequence;provider.update(input);
    check(provider.context(12).feature==Feature::none&&provider.evaluate(12,false,false)==Prompt::none,"unsupported roundabout is not converted to an ordinary turn or bend");
    input.kind=TurnKind::right;input.step=-1;++input.sequence;provider.update(input);
    check(!provider.context(12).valid&&provider.evaluate(12,false,false)==Prompt::none,"nullable step identity abstains");
    input.step=0;++input.sequence;provider.update(input);
    check(provider.evaluate(15.01,false,false)==Prompt::none,"stale feed and GPS cannot speak");
    input.receivedAt=16;++input.sequence;provider.update(input);
    check(provider.evaluate(16,false,false)==Prompt::none,"fresh provider cannot conceal stale measured GPS");
    auto poor=fixAt(16);poor.accuracyM=30;provider.fix(poor);
    check(provider.evaluate(16,false,false)==Prompt::none,"poor independent GPS blocks provider cue");
    provider.fix(fixAt(17));provider.fix(fixAt(18));input.receivedAt=18;++input.sequence;provider.update(input);
    check(provider.evaluate(18,false,false)==Prompt::mapRightTurn,"provider coaching recovers after two good fixes");
    input.remainingSeconds=4;++input.sequence;provider.update(input);
    check(provider.evaluate(18,false,false)==Prompt::none,"late ETA leaves no learner response budget");
    input.remainingSeconds=14;input.remainingM=25;++input.sequence;provider.update(input);
    check(provider.evaluate(18,false,false)==Prompt::none,"provider ETA alone cannot hide insufficient measured-speed lead");
    input.remainingM=45;input.remainingSeconds=15;++input.sequence;
    auto slow=fixAt(19);slow.speedKmh=8;provider.fix(slow);input.receivedAt=19;provider.update(input);
    check(provider.evaluate(19,false,false)==Prompt::mapRightTurn,"even a near provider estimate receives generic wording without unqualified fifty-metre precision");
    provider.clear();++input.sequence;
    check(!provider.active()&&!provider.update(input),"explicit clear retires session against delayed callbacks");
    ++input.session;input.sequence=1;input.routeEpoch=1;
    check(provider.update(input)&&provider.active(),"strictly newer session can start after clear");
    old=input;--old.session;++old.sequence;check(!provider.update(old),"old session cannot replace current route");
    ProviderTurnCoach clocks;
    input.session=200;input.routeEpoch=1;input.sequence=1;input.receivedAt=2;input.remainingM=150;input.remainingSeconds=14;
    clocks.update(input);clocks.fix(fixAt(1));clocks.fix(fixAt(2));clocks.fix(fixAt(5));
    check(!clocks.context(5.01).valid,"fresh measured GPS cannot conceal a stale provider feed");
    input.receivedAt=8;++input.sequence;clocks.update(input);
    check(!clocks.context(8).valid,"fresh provider receipt cannot conceal stale measured GPS");
    clocks.fix(fixAt(8));clocks.fix(fixAt(9));input.receivedAt=9;++input.sequence;clocks.update(input);
    check(clocks.context(9).valid,"independent clocks recover when both fresh");
    input.state=TurnState::stopped;++input.sequence;clocks.update(input);
    check(!clocks.context(9).valid&&clocks.evaluate(9,false,false)==Prompt::none,"stopped state never retains turn guidance");
    input.state=TurnState::arrived;++input.sequence;clocks.update(input);
    check(clocks.context(9).match==Match::arrived&&!clocks.context(9).valid,"arrival retires turn guidance");
    const auto providerBefore=allocations.load();
    for(int i=0;i<10000;++i){(void)provider.context(19);(void)provider.evaluate(19,false,false);}
    check(allocations.load()==providerBefore,"structured provider evaluation performs zero heap allocations");
    e=ld_create();ld_guidance_begin(e,1,LD_SOURCE_CAMERA,0);ld_camera_accepted(e,5,0);
    ld_navigation_turn turnFeed{1,300,1,1,2,LD_TURN_NAVIGATING,1,LD_MANEUVER_RIGHT,150,14};
    check(ld_navigation_turn_update(e,&turnFeed),"structured turn C ABI");
    ld_navigation_fix(e,1,1,pos.latitude,pos.longitude,3,45,.5,1,0,1);
    ld_navigation_fix(e,1,2,pos.latitude,pos.longitude,3,45,.5,1,0,1);
    q.source_id=1;q.now=q.telemetry.time=2;ld_guidance_decide(e,&q,&out);
    check(out.prompt==51&&out.origin==LD_GUIDANCE_NAVIGATION&&out.eligible,"provider enters existing shared arbiter");
    ld_navigation_turn_unavailable(e);
    check(!ld_guidance_pending(e,out.sequence,out.prompt,2.05)&&!ld_guidance_accepted(e,out.sequence,out.prompt,2.05),"feed loss cancels pending provider speech immediately");
    ++turnFeed.sequence;turnFeed.received_at=2.1;check(ld_navigation_turn_update(e,&turnFeed),"provider can recover within same session");
    q.now=q.telemetry.time=2.1;ld_guidance_decide(e,&q,&out);
    check(out.prompt==51&&out.eligible,"temporary loss preserves independent qualified GPS");
    ++turnFeed.step_number;++turnFeed.sequence;turnFeed.received_at=2.2;ld_navigation_turn_update(e,&turnFeed);
    check(!ld_guidance_pending(e,out.sequence,out.prompt,2.2),"step advancement cancels previous pending provider cue");
    turnFeed.estimated_remaining_m=105;++turnFeed.sequence;ld_navigation_turn_update(e,&turnFeed);
    q.now=q.telemetry.time=2.2;ld_guidance_decide(e,&q,&out);
    check(out.eligible&&!ld_guidance_pending(e,out.sequence,out.prompt,2.4),"delivery rechecks learner timing budget before speech starts");
    turnFeed.estimated_remaining_m=150;turnFeed.received_at=2.4;++turnFeed.sequence;ld_navigation_turn_update(e,&turnFeed);
    q.now=q.telemetry.time=2.4;ld_guidance_decide(e,&q,&out);
    check(ld_guidance_accepted(e,out.sequence,out.prompt,2.4),"new provider encounter accepted through shared audio acknowledgement");
    ++turnFeed.session;turnFeed.sequence=1;turnFeed.maneuver=LD_MANEUVER_LEFT;turnFeed.received_at=3;ld_navigation_turn_update(e,&turnFeed);
    q.now=q.telemetry.time=3;ld_guidance_decide(e,&q,&out);
    check(!out.eligible&&!ld_audio_allowed(e,50,3),"new provider session cannot bypass global audio cooldown");
    check(!ld_prompt_may_interrupt(50,42)&&ld_prompt_may_interrupt(42,50),"critical camera speech retains interruption authority over provider turns");
    ld_navigation_clear(e);++turnFeed.sequence;
    check(!ld_navigation_turn_update(e,&turnFeed),"C ABI explicit clear rejects same-session callback");
    ld_destroy(e);
    std::cout<<checks<<" navigation checks passed\n";
}
