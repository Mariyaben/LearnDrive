#include "learndrive/c_api.h"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using Engine=std::unique_ptr<ld_engine,decltype(&ld_destroy)>;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static ld_guidance_request request(double now,uint64_t source=1) {
    ld_guidance_request q{};q.source_id=source;q.now=now;
    q.telemetry={now,30,0,0,1,1,1,1,0,0,0};q.active=q.audio_ready=q.manual=1;
    q.hazard_time=now;q.tti=10;q.confidence=.9;q.in_path=1;return q;
}
static ld_guidance_result decide(ld_engine* e,ld_guidance_request q) {
    ld_guidance_result r{};check(ld_guidance_decide(e,&q,&r),"guidance call failed");return r;
}
struct Frames {
    std::vector<uint8_t> y=std::vector<uint8_t>(64*48,80),uv=std::vector<uint8_t>(32*24,128);
    std::vector<float> input=std::vector<float>(3*320*320);
    std::array<float,84> boxes{};
    void publish(ld_engine* e,uint64_t source,double time) {
        check(ld_preprocess(e,y.data(),y.size(),64,1,uv.data(),uv.size(),32,1,uv.data(),uv.size(),32,1,
            64,48,0,320,input.data(),input.size()*4,0,1,1,0,0,1),"prepare frame");
        double hazard[3];check(ld_spatial_semantics_source(e,source,time,boxes.data(),boxes.size(),1,1,hazard),"publish semantic frame");
    }
};
static void encounterRegression(double entersAt=4,bool speedValid=true,double speed=30,bool switchSource=false,bool overlapDuplicate=false,bool multiple=false) {
    Engine e(ld_create(),ld_destroy);Frames f;
    check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"encounter source begin");
    ld_camera_accepted(e.get(),5,0);
    int count=0,firstActor=0,secondActor=0,latestActor=0;double firstTime=-1,secondTime=-1,latestTime=-1;uint64_t source=1;
    for(int frame=0;frame<=(multiple?88:64);++frame) {
        const double t=frame*.125;
        if(switchSource&&frame==24){source=2;check(ld_guidance_begin(e.get(),source,LD_SOURCE_CAMERA,t),"encounter source switch");ld_camera_accepted(e.get(),5,t);}
        const bool second=t>=entersAt&&(multiple||t<entersAt+3),third=multiple&&t>=7&&t<10;
        const int n=1+int(second)+int(third);std::vector<float> labels(84*n);
        auto person=[&](int i,double x) {
            const double width=i&&overlapDuplicate?.24:.08;
            labels[i]=float((x+width/2)*320);labels[n+i]=float(.5*240+40);
            labels[2*n+i]=float(width*320);labels[3*n+i]=.4f*240;labels[4*n+i]=.95f;
        };
        person(0,.42);if(second)person(1,std::min(.3,.18+(t-entersAt)*.12));
        if(third)person(2,std::max(.6,.72-(t-7)*.12));
        check(ld_preprocess(e.get(),f.y.data(),f.y.size(),64,1,f.uv.data(),f.uv.size(),32,1,f.uv.data(),f.uv.size(),32,1,
            64,48,0,320,f.input.data(),f.input.size()*4,0,1,1,0,0,1),"encounter frame prepare");
        double hazard[3];check(ld_spatial_semantics_source(e.get(),source,t,labels.data(),labels.size(),n,1,hazard),"encounter labels");
        auto q=request(t+.02,source);q.telemetry.speed_valid=speedValid;q.telemetry.speed=speed;
        auto r=decide(e.get(),q);
        if(r.eligible) {
            check(r.prompt==44,"encounter introduced unrelated instruction");
            if(count) {
                const std::string reason=ld_camera_reason(e.get());
                check(reason.find("; reference="+std::to_string(latestActor))!=std::string::npos&&
                    reason.find("; source=1; new_entry=1")!=std::string::npos,"entry diagnostic lost accepted actor provenance");
                check(t+.02-latestTime>=3.5,"distinct entry bypassed global ordinary spacing");
            }
            latestActor=int(r.actor_id);latestTime=t+.02;
            if(count++==0){firstActor=int(r.actor_id);firstTime=t+.02;}
            else {secondActor=int(r.actor_id);secondTime=t+.02;}
            check(ld_guidance_pending(e.get(),r.sequence,r.prompt,t+.02),"encounter acknowledgement gate disagrees with selection");
            check(ld_guidance_accepted(e.get(),r.sequence,r.prompt,t+.02),"encounter accept rejected");
        }
    }
    if((speedValid&&speed==0)||switchSource||entersAt<1||overlapDuplicate) {
        check(count==((speedValid&&speed==0)?0:1),"stationary, disappeared or source-switched encounter bypassed suppression");
    } else if(multiple) {
        check(count==3&&latestTime<10,"third distinct entry referenced an older spoken actor or missed its useful window");
    } else {
        check(count==2&&firstActor>0&&secondActor!=firstActor,"distinct fresh crossing was lost behind same-phrase duplicate lock");
        check(secondTime-firstTime>=3.5&&secondTime-firstTime<10&&secondTime<entersAt+3,"crossing advice arrived outside useful fresh encounter");
    }
}
static void pendingFreshnessRegression() {
    Engine e(ld_create(),ld_destroy);Frames f;
    check(ld_guidance_begin(e.get(),1,LD_SOURCE_CAMERA,0),"fresh pending source");ld_camera_accepted(e.get(),5,0);
    f.boxes[0]=160;f.boxes[1]=160;f.boxes[2]=25.6f;f.boxes[3]=96;f.boxes[4]=.95f;
    f.publish(e.get(),1,1);decide(e.get(),request(1.02));f.publish(e.get(),1,1.125);
    auto r=decide(e.get(),request(1.145));check(r.eligible&&r.actor_id,"fresh attention missing");
    check(ld_guidance_pending(e.get(),r.sequence,r.prompt,1.465),"fresh capture prematurely expired");
    check(!ld_guidance_pending(e.get(),r.sequence,r.prompt,1.48),"acknowledgement extended capture freshness beyond350ms");
    f.boxes.fill(0);f.publish(e.get(),1,1.15);
    check(!ld_guidance_pending(e.get(),r.sequence,r.prompt,1.16),"pending actor survived a newer observation without relevance revalidation");
}
int main(){try {
    encounterRegression();encounterRegression(2);encounterRegression(.5);
    encounterRegression(4,false,0);encounterRegression(4,true,0);encounterRegression(4,true,30,true);
    encounterRegression(4,true,30,false,true);
    encounterRegression(4,true,30,false,false,true);
    pendingFreshnessRegression();
    Engine camera(ld_create(),ld_destroy),recording(ld_create(),ld_destroy);Frames frames;
    check(camera&&recording,"engine allocation");
    check(ld_guidance_begin(camera.get(),1,LD_SOURCE_CAMERA,0),"camera begin");
    check(ld_guidance_begin(recording.get(),1,LD_SOURCE_RECORDING,0),"recording begin");
    int accepted=0;
    // Two independent engines receive exactly the same frames and speech
    // acceptance. Source orchestration cannot change selection or cooldowns.
    for(int tick=0;tick<1600;++tick) {
        const double time=tick*.01;
        if(tick%25==0){
            frames.boxes.fill(0);
            if(tick>=400&&tick<800) { // A large central car, then an empty interval.
                frames.boxes[0]=160;frames.boxes[1]=210;frames.boxes[2]=90;frames.boxes[3]=100;frames.boxes[6]=.95f;
            } else if(tick>=900) { // A smaller roadside pedestrian, without invented TTI.
                frames.boxes[0]=110;frames.boxes[1]=180;frames.boxes[2]=30;frames.boxes[3]=50;frames.boxes[4]=.95f;
            }
            frames.publish(camera.get(),1,time);frames.publish(recording.get(),1,time);
        }
        const auto q=request(time);const auto a=decide(camera.get(),q),b=decide(recording.get(),q);
        check(a.prompt==b.prompt&&a.origin==b.origin&&a.eligible==b.eligible&&a.blocked==b.blocked&&
            a.suppress==b.suppress&&a.semantic_available==b.semantic_available,"camera/recording guidance diverged");
        if(a.eligible) {
            ++accepted;
            check(ld_guidance_accepted(camera.get(),a.sequence,a.prompt,time),"camera accept");
            check(ld_guidance_accepted(recording.get(),b.sequence,b.prompt,time),"recording accept");
            check(!ld_guidance_accepted(camera.get(),a.sequence,a.prompt,time),"duplicate acceptance");
        }
    }
    check(accepted>=3,"conformance did not exercise startup, obstacle and pedestrian speech");
    frames.boxes.fill(0);
    // Starting a new source invalidates pending decisions, geometry and frames,
    // without clearing the app-wide duplicate or global audio cooldown.
    Engine switched(ld_create(),ld_destroy);check(ld_guidance_begin(switched.get(),1,LD_SOURCE_CAMERA,0),"source one");
    frames.publish(switched.get(),1,1);auto pending=decide(switched.get(),request(1));check(pending.eligible,"startup candidate missing");
    check(!ld_guidance_accepted(switched.get(),pending.sequence,pending.prompt,.99),"backward acknowledgement accepted");
    check(!ld_guidance_accepted(switched.get(),pending.sequence,pending.prompt,1.4),"expired acknowledgement accepted");
    check(ld_guidance_accepted(switched.get(),pending.sequence,pending.prompt,1),"valid acknowledgement rejected");
    const double geometry[9]{60,60,32,24,64,48,0,1,0};
    check(ld_spatial_geometry(switched.get(),geometry),"camera geometry");std::array<float,6472> raw{};
    check(ld_spatial_publish_source(switched.get(),1,1,raw.data(),raw.size()),"spatial frame rejected");
    check(ld_guidance_begin(switched.get(),2,LD_SOURCE_RECORDING,1.1),"source two");
    check(!ld_guidance_begin(switched.get(),1,LD_SOURCE_CAMERA,1.2),"old source ID accepted");
    check(!ld_spatial_publish_source(switched.get(),1,1.2,raw.data(),raw.size()),"old model result contaminated new source");
    check(!ld_spatial_publish_source(switched.get(),2,1.2,raw.data(),raw.size()),"old camera calibration leaked into recording");
    double hazard[3];check(!ld_spatial_semantics_source(switched.get(),1,1.2,frames.boxes.data(),84,1,1,hazard),"old labels contaminated new source");
    check(!ld_guidance_accepted(switched.get(),pending.sequence,pending.prompt,1.2),"old-source speech acknowledged");
    check(!ld_audio_allowed(switched.get(),7,1.2),"source switch cleared global cooldown");
    frames.publish(switched.get(),2,1.2);
    auto current=decide(switched.get(),request(1.2,2));check(!current.eligible&&current.blocked==LD_GUIDANCE_COOLDOWN,"cooldown not enforced by coordinator");
    check(!current.spatial_available&&current.semantic_available,"missing recording geometry reported as available");
    auto old=decide(switched.get(),request(1.3,1));check(old.blocked==LD_GUIDANCE_INVALID&&!old.eligible,"stale-source decision accepted");
    auto inactive=request(1.3,2);inactive.active=0;check(decide(switched.get(),inactive).blocked==LD_GUIDANCE_INACTIVE,"inactive source spoke");
    auto backwards=request(1.25,2);check(decide(switched.get(),backwards).blocked==LD_GUIDANCE_INVALID,"backward decision clock accepted");
    // Losing motion/optics clears metric evidence immediately, while the fresh
    // semantic boxes and app-wide audio gates survive. Old work cannot undo it.
    Engine readiness(ld_create(),ld_destroy);
    check(ld_guidance_begin(readiness.get(),1,LD_SOURCE_CAMERA,0),"readiness source");
    frames.boxes.fill(0);frames.boxes[0]=160;frames.boxes[1]=210;frames.boxes[2]=90;frames.boxes[3]=100;frames.boxes[6]=.95f;
    check(ld_spatial_geometry(readiness.get(),geometry),"readiness geometry");
    frames.publish(readiness.get(),1,1);
    check(ld_spatial_publish_source(readiness.get(),1,1,raw.data(),raw.size()),"readiness initial spatial output");
    auto aligned=decide(readiness.get(),request(1));
    check(aligned.spatial_available&&aligned.semantic_available&&aligned.eligible,"initial readiness missing");
    check(!ld_spatial_unavailable_source(readiness.get(),2,1.01),"old source invalidated current evidence");
    check(!ld_spatial_unavailable_source(readiness.get(),1,.9),"older loss invalidated newer evidence");
    check(!ld_spatial_unavailable_source(readiness.get(),1,NAN),"invalid loss timestamp accepted");
    check(ld_spatial_unavailable_source(readiness.get(),1,1),"same-frame motion loss rejected");
    check(ld_spatial_unavailable_source(readiness.get(),1,1.01),"motion loss rejected");
    check(!ld_guidance_accepted(readiness.get(),aligned.sequence,aligned.prompt,1.01),"pre-loss decision acknowledged");
    auto limited=decide(readiness.get(),request(1.02));
    check(!limited.spatial_available&&limited.semantic_available,"motion loss silenced semantic detections or retained spatial output");
    check(limited.camera!=0,"basic camera coaching disappeared on motion loss");
    std::array<float,6> displayed{};check(ld_boxes(readiness.get(),displayed.data(),displayed.size())==1,"motion loss hid semantic box");
    check(!ld_spatial_publish_source(readiness.get(),1,1.02,raw.data(),raw.size()),"model output accepted without new geometry");
    check(ld_spatial_geometry(readiness.get(),geometry),"pose recovery geometry");
    check(!ld_spatial_publish_source(readiness.get(),1,1,raw.data(),raw.size()),"pre-loss model result survived recovery");
    check(ld_spatial_publish_source(readiness.get(),1,1.03,raw.data(),raw.size()),"fresh recovered model result rejected");
    auto recovered=decide(readiness.get(),request(1.04));check(recovered.spatial_available,"pose did not recover");
    ld_audio_accepted(readiness.get(),8,1.04);
    check(ld_spatial_unavailable_source(readiness.get(),1,1.05),"second motion loss");
    check(!ld_audio_allowed(readiness.get(),7,1.06),"motion loss erased global speech cooldown");
    check(ld_spatial_unavailable_source(readiness.get(),1,1.07),"repeated unavailable frame rejected");
    frames.publish(readiness.get(),1,5.1);
    auto semanticOnly=decide(readiness.get(),request(5.1));check(semanticOnly.eligible,"semantic-only candidate missing");
    check(ld_spatial_unavailable_source(readiness.get(),1,5.11),"repeated loss rejected");
    check(ld_guidance_accepted(readiness.get(),semanticOnly.sequence,semanticOnly.prompt,5.12),"repeated loss cancelled semantic-only speech");
    check(ld_guidance_begin(readiness.get(),2,LD_SOURCE_SCREEN,5.2),"screen switch after motion loss");
    check(!ld_spatial_unavailable_source(readiness.get(),1,5.3),"old unavailable frame contaminated new source");
    frames.boxes.fill(0);
    // Emergency phrases require a deliberate research flag. The same urgent
    // simulation candidate must remain visible in the trace while disabled.
    Engine simulation(ld_create(),ld_destroy);check(ld_guidance_begin(simulation.get(),1,LD_SOURCE_SIMULATION,0),"simulation begin");
    auto urgent=request(10);urgent.tti=.5;
    auto blocked=decide(simulation.get(),urgent);
    check(blocked.prompt==4&&!blocked.eligible&&blocked.blocked==LD_GUIDANCE_RESEARCH_ONLY,"emergency default gate failed");
    urgent.allow_emergency_commands=1;auto enabled=decide(simulation.get(),urgent);
    check(enabled.prompt==4&&enabled.eligible,"research emergency not enabled");
    check(!ld_guidance_accepted(simulation.get(),blocked.sequence,4,10),"superseded decision accepted");
    check(ld_guidance_accepted(simulation.get(),enabled.sequence,4,10),"research acknowledgement failed");
    // A recorded source must not react to the reviewing phone's pitch or shock.
    Engine clean(ld_create(),ld_destroy),phone(ld_create(),ld_destroy);
    check(ld_guidance_begin(clean.get(),1,LD_SOURCE_RECORDING,0)&&ld_guidance_begin(phone.get(),1,LD_SOURCE_RECORDING,0),"recording provenance begin");
    for(int tick=0;tick<500;++tick) {
        const double time=tick*.01;if(tick%25==0){frames.publish(clean.get(),1,time);frames.publish(phone.get(),1,time);}
        auto q=request(time);q.slope_enabled=1;auto dirty=q;
        dirty.telemetry.pitch=15;dirty.telemetry.acceleration=-8;dirty.telemetry.braking=dirty.telemetry.hard_braking=dirty.telemetry.shock=1;
        auto a=decide(clean.get(),q),b=decide(phone.get(),dirty);
        check(a.prompt==b.prompt&&a.eligible==b.eligible&&a.suppress==b.suppress,"phone motion changed recorded guidance");
    }
    // Route preparation and live camera detections coexist in the same engine.
    // Route preparation outranks routine vehicle presence; both use one cooldown.
    Engine combined(ld_create(),ld_destroy);
    check(ld_guidance_begin(combined.get(),1,LD_SOURCE_CAMERA,0),"combined camera begin");
    ld_camera_started(combined.get(),0);ld_camera_accepted(combined.get(),5,0);
    const double radians=std::acos(-1.)/180,earth=6371008.8;
    const double route[]={10,76,10+90/earth/radians,76,
        10+(90+150*std::cos(50*radians))/earth/radians,76+150*std::sin(50*radians)/earth/radians/std::cos(10*radians)};
    check(ld_navigation_route(combined.get(),99,route,3,nullptr,0),"combined route load");
    check(ld_navigation_fix(combined.get(),1,10,10,76,3,45,.5,1,0,1),"combined first fix");
    check(ld_navigation_fix(combined.get(),1,11,10,76,3,45,.5,1,0,1),"combined stable fix");
    frames.boxes.fill(0);frames.boxes[0]=160;frames.boxes[1]=210;frames.boxes[2]=90;frames.boxes[3]=100;frames.boxes[6]=.95f;
    ld_guidance_result fused{};
    for(int i=0;i<=10;++i){const double t=11+i*.125;frames.publish(combined.get(),1,t);fused=decide(combined.get(),request(t));}
    double macro[17];ld_navigation_context(combined.get(),12.25,macro,17);
    check(macro[0]==99&&macro[10]==1,"camera publication erased route context");
    check(fused.camera==18&&fused.prompt==48&&fused.origin==LD_GUIDANCE_NAVIGATION&&fused.eligible,"route preparation and camera presence did not share priority selection");
    check(ld_guidance_accepted(combined.get(),fused.sequence,fused.prompt,12.25),"combined shared acceptance");
    frames.publish(combined.get(),1,12.35);
    const auto quiet=decide(combined.get(),request(12.35));
    check(!quiet.eligible&&!ld_audio_allowed(combined.get(),46,12.35),"camera bypassed map speech cooldown");
    Engine providerCombined(ld_create(),ld_destroy);
    check(ld_guidance_begin(providerCombined.get(),1,LD_SOURCE_CAMERA,0),"provider/camera source begin");
    ld_camera_accepted(providerCombined.get(),5,0);
    ld_navigation_turn turn{1,1000,1,1,11,LD_TURN_NAVIGATING,4,LD_MANEUVER_RIGHT,150,14};
    check(ld_navigation_turn_update(providerCombined.get(),&turn),"provider/camera normalized feed");
    check(ld_navigation_fix(providerCombined.get(),1,10,10,76,3,45,.5,1,0,1)&&
        ld_navigation_fix(providerCombined.get(),1,11,10,76,3,45,.5,1,0,1),"provider/camera measured GPS");
    for(int i=0;i<=10;++i){const double t=11+i*.125;frames.publish(providerCombined.get(),1,t);fused=decide(providerCombined.get(),request(t));}
    check(fused.camera==18&&fused.prompt==51&&fused.origin==LD_GUIDANCE_NAVIGATION&&fused.eligible,
        "provider turn and camera presence must share existing priority selection");
    check(ld_guidance_accepted(providerCombined.get(),fused.sequence,fused.prompt,12.25),"provider uses shared acceptance");
    frames.publish(providerCombined.get(),1,12.35);
    const auto providerQuiet=decide(providerCombined.get(),request(12.35));
    check(!providerQuiet.eligible&&!ld_audio_allowed(providerCombined.get(),46,12.35),"camera cannot bypass provider speech cooldown");
    std::cout<<"Shared source conformance, stale-frame isolation, acknowledgements, recording provenance, research gates and map/camera arbitration passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
