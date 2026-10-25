#include "learndrive/fusion.hpp"
#include "learndrive/decision.hpp"
#include "learndrive/vision.hpp"
#include "learndrive/latest.hpp"
#include "learndrive/c_api.h"
#include <functional>
#include <iostream>
#include <thread>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <array>
using namespace ld;
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static void near(double a,double b,double tolerance=.01) {check(std::abs(a-b)<tolerance,"value outside tolerance");}
static SensorFusion calibrated() {SensorFusion f;for(int i=0;i<=501;++i)f.imu({i*.01,{0,gravity,0},{},{},true});return f;}
static Telemetry good(double time=10,double speed=60) {Telemetry t;t.time=time;t.speedKmh=speed;t.calibrated=t.speedValid=t.pitchValid=true;return t;}
static Hazard collision(double time=10,double tti=2) {return {time,tti,.9,true};}
static LaneMap curvedRoad(double curve,double yaw=0) {
    LaneMap map;map.width=map.height=80;map.road.assign(6400,.05f);
    for(int y=0;y<80;++y) {
        const double z=(.82-(y+.5)/80)/.32,center=.5+yaw*z+curve*z*z,width=.55-.3*std::clamp(z,0.0,1.0);
        for(int x=0;x<80;++x)if(std::abs((x+.5)/80-center)<width/2)map.road[y*80+x]=.95f;
    }return map;
}
static Decision gradeCue(DrivingCoach& c,double start,double pitch,bool manual=true,bool ready=true) {
    Decision d;for(int i=0;i<=20;++i){auto t=good(start+i*.1);t.pitchDegrees=pitch;d=c.evaluate(t,ready,manual);}return d;
}
static Decision observeAttention(CameraCoach& c,double first,const std::vector<Box>& boxes,
        double speed=30,bool speedValid=true,bool manual=false) {
    Decision d;
    for(int i=0;i<=10;++i)d=c.evaluate(first+i*.125+.02,first+i*.125,boxes,true,false,false,speed,speedValid,manual);
    return d;
}
int main() {
    int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& fn) {try {fn();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("five second stationary calibration",[]{auto f=calibrated();check(f.snapshot(5.01).calibrated,"calibration failed");near(f.snapshot(5.01).pitchDegrees,0);});
    test("normalization generations survive reset and change on reacquisition",[]{
        auto f=calibrated();const auto first=f.calibrationId();check(first>0,"missing normalization identity");
        f.reset();check(f.calibrationId()==first&&!f.snapshot(0).calibrated,"reset fabricated new normalization");
        for(int i=0;i<=501;++i)f.imu({i*.01,{0,gravity,0},{},{},true});
        check(f.calibrationId()>first,"explicit reset reused old normalization identity");
        const auto second=f.calibrationId();f.imu({10,{0,gravity,0},{},{},true});
        check(!f.snapshot(10).calibrated&&f.calibrationId()==second,"sensor gap fabricated successful normalization");
        for(int i=1001;i<=1501;++i)f.imu({i*.01,{0,gravity,0},{},{},true});
        check(f.snapshot(15.01).calibrated&&f.calibrationId()>second,"automatic reacquisition reused confirmed reference");
    });
    test("C ABI exposes normalization identity without changing telemetry layout",[]{
        auto* e=ld_create();check(e&&ld_calibration_id(e)==0,"new engine has a confirmed normalization");
        const double a[3]{0,gravity,0},zero[3]{};
        for(int i=0;i<=501;++i)ld_imu(e,i*.01,a,zero,zero,1);
        const auto id=ld_calibration_id(e);check(id>0&&ld_snapshot(e,5.01).calibrated,"normalization identity not exported");
        ld_reset(e);check(ld_calibration_id(e)==id&&!ld_snapshot(e,0).calibrated,"C ABI reset lost normalization provenance");
        ld_destroy(e);
    });
    test("motion resets calibration",[]{SensorFusion f;for(int i=0;i<600;++i)f.imu({i*.01,{0,gravity,0},{0,.2,0},{},true});check(!f.snapshot(5.99).calibrated,"motion accepted");});
    test("GPS movement prevents stationary calibration",[]{SensorFusion f;f.gps(0,10,.5);for(int i=1;i<600;++i){f.imu({i*.01,{0,gravity,0},{},{},true});if(i%100==0)f.gps(i*.01,10,.5);}check(!f.snapshot(5.99).calibrated,"moving vehicle calibrated");});
    test("suspend requires new calibration",[]{auto f=calibrated();f.imu({10,{0,gravity,0},{},{},true});check(!f.snapshot(10).calibrated,"old mount calibration survived suspension");});
    test("vertical camera rejects ambiguous heading",[]{SensorFusion f;for(int i=0;i<600;++i)f.imu({i*.01,{0,0,gravity},{},{},true});check(!f.snapshot(5.99).calibrated,"vertical camera accepted");});
    test("tilted mount calibrates to zero",[]{SensorFusion f;Vec3 up{0,std::cos(.3)*gravity,std::sin(.3)*gravity};for(int i=0;i<600;++i)f.imu({i*.01,up,{},{},true});near(f.snapshot(5.99).pitchDegrees,0);check(f.snapshot(5.99).calibrated,"tilted mount failed");});
    test("braking has correct forward sign",[]{auto f=calibrated();f.gps(5.01,15,.5);f.imu({5.02,{0,gravity,4},{},{0,0,4},true});auto t=f.snapshot(5.02);check(t.braking&&t.hardBraking,"deceleration missed");check(t.longitudinalAcceleration<0,"wrong sign");});
    test("positive acceleration does not mean braking",[]{auto f=calibrated();f.imu({5.02,{0,gravity,-4},{},{0,0,-4},true});check(!f.snapshot(5.02).braking,"acceleration called braking");});
    test("braking hysteresis releases",[]{auto f=calibrated();f.imu({5.02,{0,gravity,4},{},{0,0,4},true});for(int i=503;i<580;++i)f.imu({i*.01,{0,gravity,0},{},{},true});check(!f.snapshot(5.79).braking&&!f.snapshot(5.79).hardBraking,"braking stuck");});
    test("stale GPS invalidates speed",[]{auto f=calibrated();check(f.gps(5.01,10,.5),"GPS rejected");for(int i=502;i<=850;++i)f.imu({i*.01,{0,gravity,0},{},{},true});check(!f.snapshot(8.5).speedValid,"stale speed valid");});
    test("GPS speed converges and coasts",[]{auto f=calibrated();for(int i=502;i<=800;++i){f.imu({i*.01,{0,gravity,0},{},{},true});if(i%100==0)f.gps(i*.01,20,.5);}near(f.snapshot(8).speedKmh,72,.5);});
    test("out of order GPS and IMU ignored",[]{auto f=calibrated();check(f.gps(5.01,10,.5),"first GPS failed");check(!f.gps(5,50,.5),"old GPS accepted");f.imu({4,{0,gravity,20},{},{0,0,20},true});check(!f.snapshot(5.01).braking,"old IMU changed state");});
    test("nonfinite samples rejected",[]{auto f=calibrated();check(!f.gps(5.02,NAN,.5),"NaN GPS accepted");f.imu({NAN,{0,gravity,0},{},{},true});check(f.snapshot(5.01).pitchValid,"NaN corrupted IMU");});
    test("shock detected",[]{auto f=calibrated();f.imu({5.02,{15,gravity,0},{},{15,0,0},true});check(f.snapshot(5.02).shock,"shock missed");});
    test("pitch tracks gravity after settling",[]{auto f=calibrated();double r=10*pi/180;for(int i=502;i<1000;++i)f.imu({i*.01,{0,gravity*std::cos(r),-gravity*std::sin(r)},{},{},true});near(f.snapshot(9.99).pitchDegrees,10,.1);});
    test("normalized plane removes landscape rotation and mount pitch",[]{
        for(double roll:{-90.,0.,90.,180.})for(double slope:{-12.,12.}) {
            SensorFusion f;const double r=roll*pi/180,p=slope*pi/180;
            const Vec3 level{gravity*std::sin(r),gravity*std::cos(r),0};
            for(int i=0;i<=501;++i)f.imu({i*.01,level,{},{},true});
            const auto normalized=f.normalizedVector(level);
            near(normalized.x,0);near(normalized.y,gravity);near(normalized.z,0);
            for(int i=502;i<=1100;++i)f.imu({i*.01,{level.x*std::cos(p),level.y*std::cos(p),-gravity*std::sin(p)},{},{},true});
            const auto incline=f.inclination(11);check(incline.valid,"landscape slope invalid");
            near(incline.pitchDegrees,slope,.1);near(incline.gradePercent,100*std::tan(p),.2);
        }
        for(double slope:{-7.,7.}) {
            SensorFusion f;const double mount=25*pi/180,p=slope*pi/180;
            for(int i=0;i<=501;++i)f.imu({i*.01,{0,gravity*std::cos(mount),gravity*std::sin(mount)},{},{},true});
            near(f.snapshot(5.01).pitchDegrees,0);
            for(int i=502;i<=1100;++i)f.imu({i*.01,{0,gravity*std::cos(mount-p),gravity*std::sin(mount-p)},{},{},true});
            near(f.inclination(11).pitchDegrees,slope,.1);
        }
    });
    test("normalization removes steady gyro and linear sensor offsets",[]{
        SensorFusion f;const Vec3 offset{.03,0,.12},gyro{.025,0,0};
        for(int i=0;i<=1501;++i)f.imu({i*.01,Vec3{0,gravity,0}+offset,gyro,offset,true});
        const auto t=f.snapshot(15.01);check(t.calibrated&&t.pitchValid,"offset calibration failed");
        near(t.pitchDegrees,0,.01);near(t.speedKmh,0,.01);near(t.longitudinalAcceleration,0,.01);
        check(std::string(f.calibrationReason())=="Phone plane normalized","missing normalized status");
    });
    test("unseparated vehicle acceleration does not create a false hill",[]{
        auto f=calibrated();for(int i=502;i<=1000;++i)f.imu({i*.01,{0,gravity,-2},{},{},false});
        near(f.snapshot(10).pitchDegrees,0,.01);check(!f.snapshot(10).braking,"positive acceleration called braking");
    });
    test("gravity compensation separates a real hill from acceleration",[]{
        auto f=calibrated();const double pitch=8*pi/180;const Vec3 motion{0,0,-2};
        for(int i=502;i<=1100;++i)f.imu({i*.01,Vec3{0,gravity*std::cos(pitch),-gravity*std::sin(pitch)}+motion,{},motion,true});
        near(f.inclination(11).pitchDegrees,8,.1);near(f.snapshot(11).longitudinalAcceleration,2,.01);
    });
    test("gyro rotation follows the normalized mount without integration distortion",[]{
        auto f=calibrated();for(int i=1;i<=10;++i){const double angle=2*pi*i/10;f.imu({5.01+i*.1,{0,gravity*std::cos(angle),-gravity*std::sin(angle)},{2*pi,0,0},{},true});}
        check(f.inclination(6.01).valid,"full rotation did not recover plane");near(f.snapshot(6.01).pitchDegrees,0,.01);
    });
    test("inclination rejects stale shock and excessive tilt readings",[]{
        auto f=calibrated();check(!f.inclination(6).valid,"stale slope accepted");
        f.imu({5.02,{15,gravity,0},{},{15,0,0},true});check(!f.inclination(5.02).valid,"shock slope accepted");
        auto steep=calibrated();const double p=40*pi/180;
        for(int i=502;i<=1100;++i)steep.imu({i*.01,{0,gravity*std::cos(p),-gravity*std::sin(p)},{},{},true});
        check(!steep.inclination(11).valid,"nonvehicle tilt accepted as road grade");
        f.reset();check(!f.inclination(12).valid&&f.normalizedVector({0,gravity,0}).norm()==0,"reset kept normalized plane");
    });
    test("inclination ABI exposes normalized live grade and calibration status",[]{
        auto* e=ld_create();double gyro[3]{},linear[3]{},level[3]{0,gravity,0},out[5]{};
        check(ld_inclination(e,0,out,5)==5&&out[0]==0,"uncalibrated ABI slope valid");
        check(ld_inclination(e,0,out,4)==0&&ld_inclination(nullptr,0,out,5)==0,"short inclination output accepted");
        for(int i=0;i<=501;++i)ld_imu(e,i*.01,level,gyro,linear,1);
        check(std::string(ld_calibration_reason(e))=="Phone plane normalized","ABI plane status missing");
        // Use the middle of a band: filtered angles approach a boundary from below.
        const double p=10*pi/180;double hill[3]{0,gravity*std::cos(p),-gravity*std::sin(p)};
        int prompt=0;for(int i=502;i<=1100;++i){const double time=i*.01;ld_imu(e,time,hill,gyro,linear,1);prompt=ld_driving_decide(e,ld_snapshot(e,time),1,1,1);}
        check(prompt==int(Prompt::steepUp),"normalized phone hill did not reach Camera policy without GPS");
        check(ld_inclination(e,11,out,5)==5&&out[0]==1,"ABI live grade invalid");near(out[1],10,.1);near(out[3],100*std::tan(p),.2);
        check(ld_inclination(e,12,out,5)==5&&out[0]==0,"ABI stale grade displayed");ld_destroy(e);
    });
    test("video speed with phone slope preserves measured normalization and motion",[]{
        auto measured=good(10,0);measured.pitchDegrees=-8;measured.calibrationProgress=.4;measured.calibrated=false;measured.braking=true;measured.hardBraking=true;measured.shock=true;
        const auto t=DecisionEngine::videoSpeedTelemetry(measured,30);
        near(t.speedKmh,30);check(t.speedValid,"phone GPS zero overrode video speed");near(t.pitchDegrees,-8);near(t.calibrationProgress,.4);
        check(!t.calibrated&&t.braking&&t.hardBraking&&t.shock,"phone state replaced by simulation");
        for(double speed:{-1.,161.,double(NAN),infinity})check(!DecisionEngine::videoSpeedTelemetry(measured,speed).speedValid,"invalid video speed accepted");
        near(measured.speedKmh,0);
    });
    test("video speed phone slope C ABI preserves all nonspeed telemetry fields",[]{
        const ld_telemetry measured{10,0,-8,-2,.4,0,1,0,1,1,1};
        const auto t=ld_video_speed_telemetry(measured,20);
        near(t.speed,20);check(t.speed_valid,"ABI video speed missing");
        near(t.time,measured.time);near(t.pitch,measured.pitch);near(t.acceleration,measured.acceleration);near(t.progress,measured.progress);
        check(t.calibrated==measured.calibrated&&t.pitch_valid==measured.pitch_valid&&t.braking==measured.braking&&t.hard_braking==measured.hard_braking&&t.shock==measured.shock,"ABI measured state changed");
        check(!ld_video_speed_telemetry(measured,NAN).speed_valid,"ABI NaN speed accepted");
    });
    test("suspend gap does not integrate acceleration",[]{auto f=calibrated();f.gps(5.01,10,.5);auto before=f.snapshot(5.01).speedKmh;f.imu({50,{0,gravity,-20},{},{0,0,-20},true});near(f.snapshot(50).speedKmh,before);});
    test("adaptive TTI buffer endpoints",[]{near(DecisionEngine::buffer(30),3);near(DecisionEngine::buffer(80),7);near(DecisionEngine::buffer(55),5);near(DecisionEngine::buffer(0),3);});
    test("high speed ease prompt",[]{DecisionEngine e;check(e.evaluate(good(),collision(),true).prompt==Prompt::ease,"ease missed");});
    test("low speed brake prompt",[]{DecisionEngine e;check(e.evaluate(good(10,20),collision(),true).prompt==Prompt::brake,"gentle braking missed");});
    test("critical warning wins over hill",[]{DecisionEngine e;auto t=good(10,20);t.pitchDegrees=10;check(e.evaluate(t,collision(10,.9),true,true).prompt==Prompt::emergency,"priority incorrect");});
    test("all prompts suppressed while braking",[]{DecisionEngine e;auto t=good();t.braking=true;check(e.evaluate(t,collision(10,.5),true).prompt==Prompt::none,"braking not suppressed");});
    test("strict 3.5 second cooldown including emergency",[]{DecisionEngine e;e.accepted(10);check(e.evaluate(good(13.499),collision(13.499,.5),true).prompt==Prompt::none,"early prompt");check(e.evaluate(good(13.5),collision(13.5,.5),true).prompt==Prompt::emergency,"cooldown never released");});
    test("unaccepted decision does not consume cooldown",[]{DecisionEngine e;e.evaluate(good(),collision(),true);check(e.evaluate(good(10.01),collision(10.01),true).prompt==Prompt::ease,"evaluation committed cooldown");});
    test("stale hazard suppressed",[]{DecisionEngine e;check(e.evaluate(good(),collision(9.6),true).prompt==Prompt::none,"stale hazard spoken");});
    test("poor confidence suppressed",[]{DecisionEngine e;auto h=collision();h.confidence=.64;check(e.evaluate(good(),h,true).prompt==Prompt::none,"weak hazard spoken");});
    test("nonfinite confidence suppressed",[]{DecisionEngine e;auto h=collision();h.confidence=infinity;check(e.evaluate(good(),h,true).prompt==Prompt::none,"infinite confidence spoken");});
    test("unready offline audio suppressed",[]{DecisionEngine e;check(e.evaluate(good(),collision(),false).prompt==Prompt::none,"unready audio accepted");});
    test("speed and incline never assert a specific gear shift",[]{DecisionEngine e;auto t=good(10,20);t.pitchDegrees=8;check(e.evaluate(t,{},true,true).prompt==Prompt::none,"legacy blind gear shift survived");});
    test("invalid speed and uncalibrated telemetry suppressed",[]{DecisionEngine e;auto t=good();t.speedValid=false;check(e.evaluate(t,collision(),true).prompt==Prompt::none,"invalid speed used");t=good();t.calibrated=false;check(e.evaluate(t,collision(),true).prompt==Prompt::none,"uncalibrated used");});
    test("latest frame overwrites and releases owner",[]{Latest<std::shared_ptr<int>> q;auto a=std::make_shared<int>(1);std::weak_ptr<int>w=a;q.push(a);a.reset();q.push(std::make_shared<int>(2));check(w.expired(),"stale lease retained");auto value=q.take();check(value&&**value==2&&q.dropped()==1,"queue not latest only");q.close();check(!q.push(std::make_shared<int>(3)),"closed queue accepted frame");});
    test("closing queue unblocks worker",[]{Latest<int> q;bool ended=false;std::thread worker([&]{ended=!q.take();});q.close();worker.join();check(ended,"worker did not stop");});
    test("stride-aware YUV and rotation",[]{uint8_t y[]={20,40,0,0,60,80,0,0},u[]={128},v[]={128};std::vector<float> tensor(12);YuvFrame f{2,2,90,{y,8,4,1},{u,1,1,1},{v,1,1,1}};auto tr=preprocess(f,2,tensor.data(),48,0,0,1,0);near(tensor[0],60/255.);near(tensor[3],20/255.);check(tr.width==2,"rotation shape");});
    test("short plane rejected instead of out of bounds read",[]{uint8_t y[]={20},u[]={128};float tensor[12];bool threw=false;try{preprocess({2,2,0,{y,1,2,1},{u,1,1,1},{u,1,1,1}},2,tensor,48,0,0,1,0);}catch(...){threw=true;}check(threw,"invalid plane accepted");});
    test("INT8 quantization and NCHW layout",[]{uint8_t y[]={255,255,255,255},uv[]={128};int8_t t[12];preprocess({2,2,0,{y,4,2,1},{uv,1,1,1},{uv,1,1,1}},2,t,12,2,1,1.f/255,-128);check(t[0]==127&&t[11]==127,"quantization incorrect");});
    test("YOLO raw decoding and class aware NMS",[]{float v[84*3]{};for(int i=0;i<3;++i){v[i]=160;v[3+i]=160;v[6+i]=100;v[9+i]=100;}v[6*3]=.9;v[6*3+1]=.8;v[4*3+2]=.7;auto boxes=decodeYolo(v,252,84,3,true,false,{640,480,320,.5,0,40});check(boxes.size()==2,"NMS incorrect");near(boxes[0].w,.3125);});
    test("unsupported model contracts rejected",[]{float v[10]{};bool threw=false;try{decodeYolo(v,10,10,1,true,false,{320,320,320,1,0,0});}catch(...){threw=true;}check(threw,"wrong model accepted");});
    test("road/lane segmentation softmax",[]{float road[]={-2,-2,-2,-2,2,2,2,2};float lane[]={2,2,2,2,-2,-2,-2,-2};auto m=decodeLanes(road,lane,2,2,{320,320,320,1,0,0});check(m.inRoad(.5,.5)&&m.lane[0]<.1,"segmentation incorrect");});
    test("TTI needs textured expanding evidence",[]{ApproachTracker t;Gray gray;gray.pixels.assign(6400,128);LaneMap lanes;lanes.width=lanes.height=80;lanes.road.assign(6400,1);for(int i=0;i<10;++i){std::vector<Box> b{{.4f,.3f,.2f+i*.002f,.3f+i*.002f,.9f,2}};auto h=t.update(i*.033,b,gray,lanes);check(!h.inPath,"flat texture invented TTI");}});
    test("textured expansion yields a finite in-path TTI",[]{
        ApproachTracker tracker;LaneMap lanes;lanes.width=lanes.height=80;lanes.road.assign(6400,1);bool detected=false;
        for(int i=0;i<10;++i) {
            double scale=1+i*.07;Gray gray;gray.pixels.resize(6400);
            for(int y=0;y<80;++y)for(int x=0;x<80;++x)gray.pixels[y*80+x]=uint8_t(128+55*std::sin((x-40)/scale*.8)+55*std::cos((y-40)/scale*.9));
            float side=float(.35*scale);std::vector<Box> boxes{{.5f-side/2,.5f-side/2,side,side,.9f,2}};
            auto h=tracker.update(i*.033,boxes,gray,lanes);
            if(h.inPath&&h.confidence>=.65&&std::isfinite(h.tti)&&h.tti>0)detected=true;
        }
        check(detected,"approach evidence never generated TTI");
    });
    test("limited-range YUV maps black and white",[]{uint8_t y[]={16,235,16,235},uv[]={128};float values[12];preprocess({2,2,0,{y,4,2,1},{uv,1,1,1},{uv,1,1,1},false},2,values,48,0,0,1,0);near(values[0],0);near(values[3],1,.001);});
    test("recalibration does not reset cooldown",[]{auto* e=ld_create();ld_telemetry t{10,60,0,0,1,1,1,1,0,0,0};ld_accepted(e,10);ld_reset(e);check(ld_decide(e,t,10,.8,.9,1,1,0)==0,"recalibration bypassed cooldown");ld_destroy(e);});
    test("C ABI errors are contained",[]{auto* e=ld_create();check(e,"engine allocation failed");double h[3];float v[10]{};check(!ld_vision(e,1,v,10,10,1,1,0,v,v,1,1,h),"bad C ABI input succeeded");check(std::strlen(ld_error(e))>0,"missing error");ld_destroy(e);});
    test("C ABI exposes warning suppression reason",[]{
        auto* e=ld_create();ld_telemetry t{10,60,0,0,1,1,1,1,0,0,0};
        check(ld_decide(e,t,10,2,.9,1,0,0)==0,"unready audio prompted");
        check(std::string(ld_decision_reason(e))=="offline audio not ready","missing audio reason");
        check(ld_decide(e,t,10,2,.9,1,1,0)==2,"hazard not actionable");
        check(std::string(ld_decision_reason(e))=="approaching hazard","missing hazard reason");ld_destroy(e);
    });
    test("C ABI returns display boxes within capacity and clears stale results",[]{
        auto* e=ld_create();uint8_t y[4]={128,128,128,128},uv[1]={128};std::vector<float> tensor(3*320*320);
        check(ld_preprocess(e,y,4,2,1,uv,1,1,1,uv,1,1,1,2,2,0,320,tensor.data(),tensor.size()*4,0,1,1,0,1,1),"NCHW preprocessing failed");
        float d[84]{};d[0]=d[1]=160;d[2]=d[3]=100;d[6]=.9f;
        float mask[8]={-2,-2,-2,-2,2,2,2,2};double hazard[3];float packed[7]{};packed[6]=1234;
        check(ld_vision(e,1,d,84,84,1,1,0,mask,mask,2,2,hazard),"vision decode failed");
        check(ld_boxes(e,packed,5)==0,"incomplete box written");
        check(ld_boxes(e,packed,6)==1&&packed[6]==1234,"box buffer overflow");
        near(packed[0],.34375);near(packed[2],.3125);near(packed[4],.9);near(packed[5],2);
        check(!ld_vision(e,2,d,84,10,1,1,0,mask,mask,2,2,hazard),"invalid output accepted");
        check(ld_boxes(e,packed,6)==0,"failed frame retained stale boxes");ld_destroy(e);
    });
    test("optional GPS uses manual speed for actionable advice",[]{
        DecisionEngine e;auto measured=good(10,0);measured.speedValid=false;
        auto selected=DecisionEngine::adviceTelemetry(measured,false,20);
        check(selected.speedValid,"manual speed invalid");near(selected.speedKmh,20);
        check(!measured.speedValid&&measured.speedKmh==0,"measured state mutated");
        check(e.evaluate(selected,collision(),true).prompt==Prompt::brake,"GPS-free advice suppressed");
    });
    test("required GPS blocks advice without a fix",[]{
        DecisionEngine e;auto t=good();t.speedValid=false;
        auto selected=DecisionEngine::adviceTelemetry(t,true,60);
        check(!selected.speedValid&&e.evaluate(selected,collision(),true).prompt==Prompt::none,"GPS requirement ignored");
    });
    test("available GPS including zero wins over fallback speed",[]{
        DecisionEngine e;auto stopped=DecisionEngine::adviceTelemetry(good(10,0),false,60);
        near(stopped.speedKmh,0);check(e.evaluate(stopped,collision(),true).prompt==Prompt::none,"stationary GPS overridden");
        auto moving=DecisionEngine::adviceTelemetry(good(10,60),false,20);
        near(moving.speedKmh,60);check(e.evaluate(moving,collision(),true).prompt==Prompt::ease,"GPS speed not preferred");
    });
    test("invalid manual speeds never enable GPS-free advice",[]{
        auto t=good();t.speedValid=false;
        for(double value:std::array<double,4>{NAN,infinity,-1.,161.})check(!DecisionEngine::adviceTelemetry(t,false,value).speedValid,"invalid fallback accepted");
    });
    test("GPS-free advice preserves calibration braking and shock gates",[]{
        DecisionEngine e;auto t=good();t.speedValid=false;t.calibrated=false;
        check(e.evaluate(DecisionEngine::adviceTelemetry(t,false,60),collision(),true).prompt==Prompt::none,"calibration bypassed");
        t.calibrated=true;t.braking=true;
        check(e.evaluate(DecisionEngine::adviceTelemetry(t,false,60),collision(),true).prompt==Prompt::none,"braking bypassed");
        t.braking=false;t.shock=true;
        check(e.evaluate(DecisionEngine::adviceTelemetry(t,false,60),collision(),true).prompt==Prompt::none,"shock bypassed");
    });
    test("manual speed cannot bypass stale motion sensors",[]{
        DecisionEngine e;auto f=calibrated();
        auto fresh=DecisionEngine::adviceTelemetry(f.snapshot(5.01),false,30);
        check(e.evaluate(fresh,collision(5.01),true).prompt==Prompt::brake,"fresh GPS-free advice suppressed");
        auto stale=DecisionEngine::adviceTelemetry(f.snapshot(5.5),false,30);
        check(stale.calibrated&&!stale.pitchValid,"stale sensor fixture invalid");
        check(e.evaluate(stale,collision(5.5),true).prompt==Prompt::none,"manual speed bypassed stale IMU");
    });
    test("C ABI manual selection preserves sensor fields",[]{
        ld_telemetry measured{10,0,8,-4,.8,1,0,1,1,1,1};auto selected=ld_advice_telemetry(measured,0,20);
        near(selected.speed,20);check(selected.speed_valid,"manual input lost across C ABI");
        near(selected.pitch,8);near(selected.acceleration,-4);near(selected.progress,.8);
        check(selected.calibrated&&selected.pitch_valid&&selected.braking&&selected.hard_braking&&selected.shock,"sensor flags overwritten");
        check(!ld_advice_telemetry(measured,1,20).speed_valid,"C ABI ignored GPS requirement");
    });
    test("slow inference deducts capture age from impact time",[]{
        DecisionEngine e;auto t=good();
        check(e.evaluate(t,collision(9.821,1.3),true).prompt==Prompt::emergency,"179 ms processing discarded or not deducted");
        check(e.evaluate(t,collision(9.7,2),true).prompt==Prompt::ease,"bounded delayed result rejected");
    });
    test("future capture timestamps never trigger warnings",[]{
        DecisionEngine e;check(e.evaluate(good(),collision(10.01,.1),true).prompt==Prompt::none,"future frame accepted");
    });
    test("slower frame intervals retain approach tracks and long gaps reset them",[]{
        ApproachTracker tracker;LaneMap lanes;lanes.width=lanes.height=80;lanes.road.assign(6400,1);bool detected=false;
        for(int i=0;i<=10;++i) {
            double scale=1+i*.07;Gray gray;gray.pixels.resize(6400);
            for(int y=0;y<80;++y)for(int x=0;x<80;++x)gray.pixels[y*80+x]=uint8_t(128+55*std::sin((x-40)/scale*.8)+55*std::cos((y-40)/scale*.9));
            float side=float(.35*scale);std::vector<Box> boxes{{.5f-side/2,.5f-side/2,side,side,.9f,2}};
            auto h=tracker.update(i*.21+(i==10?.15:0),boxes,gray,lanes);
            if(i<10&&h.inPath&&std::isfinite(h.tti))detected=true;
            if(i==10)check(!h.inPath,"old track survived a 360 ms gap");
        }
        check(detected,"210 ms frames never produced approach evidence");
    });
    test("camera speaks before frames calibration or GPS arrive",[]{
        auto* e=ld_create();ld_camera_started(e,10);
        check(!ld_snapshot(e,10).calibrated&&!ld_snapshot(e,10).speed_valid,"fresh engine already calibrated");
        check(ld_camera_decide(e,10,0,0,0)==0,"unready audio accepted");
        check(ld_camera_decide(e,10,1,1,1)==int(Prompt::camera),"startup waited for sensor gates");
        ld_camera_accepted(e,int(Prompt::camera),10);
        check(ld_camera_decide(e,10.01,1,0,0)==0,"startup repeated");ld_destroy(e);
    });
    test("sustained actor-specific attention does not invent TTI",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box person{.6f,.4f,.08f,.25f,.7f,0};
        check(c.evaluate(10.12,10.1,{person},true,false,false).prompt==Prompt::none,"single detection spoke");
        auto d=c.evaluate(10.245,10.225,{person},true,false,false);
        check(d.prompt==Prompt::pedestrianSpace&&d.actorCategory==0&&d.actorId>0&&d.evidenceKind==1,"pedestrian attention missing");
        CameraCoach vehicle;vehicle.start(10);vehicle.accepted(Prompt::camera,10);
        check(observeAttention(vehicle,10.1,{{.4f,.4f,.15f,.2f,.8f,2}}).prompt==Prompt::roadUserQuiet,"sustained vehicle ignored");
    });
    test("one large box cannot become an obstacle instruction",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box car{.3f,.4f,.4f,.5f,.9f,2};
        check(c.evaluate(10.2,10.1,{car},true,false,false).prompt==Prompt::none,"first large box requested action");
        for(int i=0;i<20;++i)check(c.evaluate(10.2+i*.001,10.1,{car},true,false,false).prompt==Prompt::none,"one frame matured at rule frequency");
        auto d=observeAttention(c,10.225,{car});
        check(d.prompt==Prompt::roadUserQuiet&&d.evidenceKind==1,"sustained real car lost its gap lesson");
    });
    test("side traffic is quiet and a small relevant vehicle gets only a gap lesson",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        check(observeAttention(c,10.1,{{.77f,.2f,.2f,.75f,.9f,2}}).prompt==Prompt::none,"side traffic narrated");
        auto d=observeAttention(c,12,{{.47f,.4f,.08f,.2f,.9f,2}});
        check(d.prompt==Prompt::roadUserQuiet&&d.evidenceKind==1,"small sustained central vehicle misclassified");
    });
    test("sustained modest central vehicle gives space guidance without a close claim",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        auto d=observeAttention(c,10.1,{{.375f,.42f,.25f,.2f,.8f,2}});
        check(d.prompt==Prompt::roadUserQuiet&&std::strstr(promptText(d.prompt),"Obstacle")==nullptr,"occupancy claimed an obstacle");
    });
    test("critical camera warning requires reliable optical evidence",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.3f,.4f,.4f,.5f,.9f,2};b.tti=.8;b.confidence=.64;
        check(c.evaluate(10.2,10.1,{b},true,false,false).prompt==Prompt::none,"weak optical motion requested action");
        b.confidence=.9;auto d=c.evaluate(10.3,10.225,{b},true,false,false);
        check(d.prompt==Prompt::emergency&&d.evidenceKind==3,"reliable critical approach ignored");
    });
    test("optical approach and critical risk can escalate a routine attention cue",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::roadUserQuiet,10);Box b{.3f,.4f,.4f,.5f,.9f,2};b.tti=2;b.confidence=.9;b.leadTime=10.01;
        auto d=c.evaluate(10.1,10.01,{b},true,false,false,30,true);
        check(d.prompt==Prompt::keepSafeGap&&d.evidenceKind==2,"optical approach waited behind courtesy spacing");
        c.accepted(d.prompt,10.1);b.tti=.8;
        check(c.evaluate(10.2,10.11,{b},true,false,false).prompt==Prompt::emergency,"critical approach suppressed");
    });
    test("attention retries unaccepted speech and quiets unchanged traffic",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.4f,.4f,.15f,.2f,.9f,2};
        auto d=observeAttention(c,10.1,{b});check(d.prompt==Prompt::roadUserQuiet,"gap lesson missing");
        auto retry=c.evaluate(11.5,11.475,{b},true,false,false);
        check(retry.prompt==d.prompt&&retry.actorId==d.actorId,"unaccepted attention was consumed or identity changed");
        c.accepted(retry.prompt,11.5);
        for(int i=0;i<100;++i){double t=11.6+i*.125;check(c.evaluate(t+.02,t,{b},true,false,false).prompt==Prompt::none,"unchanged traffic repeated");}
    });
    test("camera rejects expired and future scene evidence",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        std::vector<Box> boxes{{.3f,.4f,.4f,.5f,.9f,2}};
        check(c.evaluate(10.5,10.1,boxes,true,false,false).prompt==Prompt::none,"old obstacle spoken");
        check(c.evaluate(10.5,10.6,boxes,true,false,false).prompt==Prompt::none,"future obstacle spoken");
        check(c.evaluate(10.2,9.99,boxes,true,false,false).prompt==Prompt::none,"previous camera session reused");
    });
    test("camera action cues respect braking and shock",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        std::vector<Box> boxes{{.3f,.4f,.4f,.5f,.9f,2}};
        check(c.evaluate(10.2,10.01,boxes,true,true,false).prompt==Prompt::none,"already braking ignored");
        check(c.evaluate(10.2,10.01,boxes,true,false,true).prompt==Prompt::none,"phone shock ignored");
    });
    test("routine camera speech leaves tactical collision timing available",[]{
        auto* e=ld_create();ld_camera_started(e,10);ld_camera_accepted(e,int(Prompt::traffic),10);
        ld_telemetry t{10.01,60,0,0,1,1,1,1,0,0,0};
        check(ld_decide(e,t,10.01,.8,.9,1,1,0)==int(Prompt::emergency),"routine speech blocked tactical emergency");ld_destroy(e);
    });
    test("C ABI sustained attention survives occluded road masks without inventing risk",[]{
        auto* e=ld_create();ld_camera_started(e,10);ld_camera_accepted(e,int(Prompt::camera),10);
        uint8_t y[4]={128,128,128,128},uv[1]={128};std::vector<float> tensor(3*320*320);
        check(ld_preprocess(e,y,4,2,1,uv,1,1,1,uv,1,1,1,2,2,0,320,tensor.data(),tensor.size()*4,0,1,1,0,1,1),"preprocess failed");
        float boxes[84]{};boxes[0]=160;boxes[1]=210;boxes[2]=120;boxes[3]=180;boxes[6]=.8f;float road[2]={2,-2};double hazard[3];int prompt=0;
        for(int i=0;i<=10;++i){double t=10.01+i*.125;check(ld_vision(e,t,boxes,84,84,1,1,0,road,road,1,1,hazard),"vision failed");prompt=ld_camera_decide(e,t+.02,1,0,0);}
        check(!std::isfinite(hazard[0])&&!hazard[2]&&prompt==int(Prompt::roadUserQuiet),"no-calibration gap lesson lost or risk invented");
        check(!ld_vision(e,11.3,boxes,84,10,1,1,0,road,road,1,1,hazard),"bad frame accepted");
        check(ld_camera_decide(e,11.32,1,0,0)==0,"failed frame reused an actor");ld_destroy(e);
    });
    test("invalid road user detections never trigger camera instructions",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        for(Box b:std::vector<Box>{{.3f,.4f,.4f,.5f,.39f,2},{.3f,.4f,.4f,.5f,.9f,56},{.3f,.4f,.4f,.5f,NAN,2},{.3f,.4f,-.4f,.5f,.9f,2}})
            check(c.evaluate(10.2,10.01,{b},true,false,false).prompt==Prompt::none,"invalid detection spoken");
    });
    test("recorded road and guardrail hallucinations cannot give attention or obstacle cues",[]{
        for(Box b:std::vector<Box>{{.222215f,.610187f,.774561f,.377501f,.687502f,2},{.000456f,.617154f,.970425f,.371571f,.487783f,2}}) {
            CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
            check(observeAttention(c,10.1,{b}).prompt==Prompt::none,"known road hallucination still spoke");
        }
    });
    test("opposing far-left vehicle does not become generic traffic advice",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        check(observeAttention(c,10.1,{{0,.5f,.065f,.09f,.8f,2}}).prompt==Prompt::none,"across-divider actor narrated");
    });
    test("a relevant two-wheeler receives early specific attention",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.4f,.5f,.1f,.25f,.7f,3};
        c.evaluate(10.12,10.1,{b},true,false,false);
        auto d=c.evaluate(10.245,10.225,{b},true,false,false);
        check(d.prompt==Prompt::riderSpace&&d.actorCategory==3&&d.evidenceKind==1,"rider attention missed or became a collision claim");
    });
    test("crossing vulnerable actor can receive attention before its center enters the corridor",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.19f,.5f,.08f,.3f,.75f,0};
        c.evaluate(10.12,10.1,{b},true,false,false);b.x=.225f;
        auto d=c.evaluate(10.245,10.225,{b},true,false,false);
        check(d.prompt==Prompt::pedestrianSpace,"inward crossing pedestrian waited for center overlap");
    });
    test("class flicker and missing observations cannot mature a vehicle",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.4f,.5f,.15f,.2f,.8f,2};
        c.evaluate(10.12,10.1,{b},true,false,false);
        check(c.evaluate(11,10.98,{b},true,false,false).prompt==Prompt::none,"long gap fabricated continuity");
        b.category=3;check(c.evaluate(11.1,11.08,{b},true,false,false).prompt==Prompt::none,"class switch inherited vehicle history");
    });
    test("attention identity and capture box remain stable and source-local",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.4f,.5f,.15f,.2f,.8f,2};auto d=observeAttention(c,10.1,{b});
        auto next=c.evaluate(11.5,11.475,{b},true,false,false);
        check(next.actorId==d.actorId&&next.actorId>0&&next.actorW==b.w&&next.actorCategory==2,"actor evidence changed without a new actor");
        c.start(12);c.accepted(Prompt::camera,12);
        check(c.evaluate(12.12,12.1,{b},true,false,false).prompt==Prompt::none,"old source matured new actor");
    });
    test("one image cannot assert slowing from object size at any manual speed",[]{
        for(double speed:{3.,30.,80.}) {CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
            auto d=observeAttention(c,10.1,{{.3f,.4f,.4f,.5f,.9f,2}},speed,true,true);
            check(d.prompt==Prompt::roadUserQuiet&&d.evidenceKind==1,"speed setting invented obstacle or gear authority");}
    });
    test("short learner attention clips have equal noninterrupting priority",[]{
        for(auto a:{Prompt::pedestrianSpace,Prompt::riderSpace,Prompt::vehicleSpace,Prompt::roadUserQuiet})for(auto b:{Prompt::pedestrianSpace,Prompt::riderSpace,Prompt::vehicleSpace,Prompt::roadUserQuiet}) {
            check(!mayInterrupt(a,b),"ordinary attention chopped another lesson");
            check(!mayInterrupt(a,Prompt::keepSafeGap),"courtesy cue displaced an approach action");
        }
        check(mayInterrupt(Prompt::emergency,Prompt::pedestrianSpace),"critical action could not interrupt attention");
    });
    test("coherently moving rider body and motorcycle produce a two-wheeler lesson",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        std::vector<Box> boxes{{.4f,.3f,.12f,.35f,.85f,0},{.4f,.48f,.13f,.25f,.75f,3}};
        c.evaluate(10.12,10.1,boxes,true,false,false);for(auto& b:boxes)b.x+=.02f;
        auto d=c.evaluate(10.245,10.225,boxes,true,false,false);
        check(d.prompt==Prompt::riderSpace&&d.actorCategory==3,"rider body announced as a pedestrian");
        check(std::strstr(promptText(Prompt::pedestrianSpace),"Pedestrian")==nullptr,"ambiguous person label asserted walking");
    });
    test("a newly entering vehicle is offered attention without waiting for the routine gap",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::riderSpace,10);Box b{.69f,.5f,.14f,.2f,.8f,2};
        c.evaluate(14.02,14,{b},true,false,false);b.x=.62f;c.evaluate(14.145,14.125,{b},true,false,false);
        b.x=.58f;auto d=c.evaluate(14.27,14.25,{b},true,false,false);
        check(d.prompt==Prompt::roadUserQuiet&&d.actorCategory==2&&d.evidenceKind==1,"new entering vehicle lost behind routine spacing");
        check(d.reason=="sustained actor entering forward attention zone","entry evidence missing");
    });
    test("proven moving rider association survives a brief weak-bike observation",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        std::vector<Box> boxes{{.4f,.3f,.12f,.35f,.85f,0},{.4f,.48f,.13f,.25f,.75f,3}};
        c.evaluate(10.12,10.1,boxes,true,false,false);for(auto& b:boxes)b.x+=.02f;
        auto rider=c.evaluate(10.245,10.225,boxes,true,false,false);check(rider.prompt==Prompt::riderSpace,"moving rider not established");c.accepted(rider.prompt,10.245);
        boxes[1].score=.45f;
        check(c.evaluate(10.37,10.35,boxes,true,false,false).prompt==Prompt::none,"proven rider repeated as a person during brief class dropout");
        CameraCoach first;first.start(10);first.accepted(Prompt::camera,10);
        check(observeAttention(first,10.1,boxes).prompt==Prompt::pedestrianSpace,"weak bike hypothesis suppressed first person attention");
    });
    test("a stationary or independently moving pedestrian overlapping a bike remains visible",[]{
        for(bool personMoving:{false,true}) {
            CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
            std::vector<Box> boxes{{.4f,.3f,.12f,.35f,.85f,0},{.4f,.48f,.13f,.25f,.75f,3}};
            c.evaluate(10.12,10.1,boxes,true,false,false);if(personMoving)boxes[0].x+=.02f;
            auto d=c.evaluate(10.245,10.225,boxes,true,false,false);
            check(d.prompt==Prompt::pedestrianSpace,"overlap alone hid a pedestrian behind a bike");
        }
    });
    test("an implausible tiny bike cannot suppress a real person",[]{
        CameraCoach c;c.start(0);c.accepted(Prompt::camera,0);
        const std::vector<Box> boxes{{.4f,.35f,.1f,.4f,.9f,0},{.44f,.6f,.001f,.001f,.9f,3}};
        check(observeAttention(c,.1,boxes).prompt==Prompt::pedestrianSpace,"tiny invalid bike hid a person");
    });
    test("road curvature separates left right and straight perspective",[]{
        auto left=roadShape(curvedRoad(-.22)),right=roadShape(curvedRoad(.22));
        check(left.valid&&left.bend==-1,"left bend missed");check(right.valid&&right.bend==1,"right bend missed");
        for(double yaw:{-.15,0.,.15}) {auto shape=roadShape(curvedRoad(0,yaw));check(shape.valid&&shape.bend==0,"straight perspective mistaken for a bend");}
    });
    test("cropped near road edges use a farther bounded corridor",[]{
        auto map=curvedRoad(.22);for(int y=60;y<80;++y)for(int x=0;x<80;++x)map.road[y*80+x]=.95f;
        auto shape=roadShape(map);check(shape.valid&&shape.bend==1,"cropped foreground disabled all bend detection");
    });
    test("ambiguous missing clipped and weak road masks do not invent bends",[]{
        LaneMap empty;check(!roadShape(empty).valid,"missing mask accepted");
        auto map=curvedRoad(.22);map.road.assign(6400,.5f);check(!roadShape(map).valid,"weak road accepted");
        map.road.assign(6400,1);check(!roadShape(map).valid,"unbounded full mask accepted");
        map=curvedRoad(.22);for(int y=38;y<70;++y)for(int x=2;x<9;++x)map.road[y*80+x]=.95f;
        check(!roadShape(map).valid,"split corridors accepted");
        map=curvedRoad(.22);map.road[40*80+40]=NAN;check(!roadShape(map).valid,"nonfinite mask accepted");
    });
    test("bend guidance needs consecutive frames but no GPS calibration or TTI",[]{
        DrivingCoach c;Telemetry t;t.time=10;c.road(10,{true,-1,-.1});
        check(c.evaluate(t,true,false).prompt==Prompt::none,"one-frame bend spoken");
        for(double time:{10.2,10.4,10.6})c.road(time,{true,-1,-.1});t.time=10.7;
        check(c.evaluate(t,true,false).prompt==Prompt::bendLeftQuiet,"bend unnecessarily telemetry gated");
        c.accepted(Prompt::bendLeft,10.7);t.time=20;c.road(20,{true,-1,-.1});
        check(c.evaluate(t,true,false).prompt==Prompt::none,"same bend repeated");
    });
    test("repeated evaluation cannot turn one image into a sustained bend",[]{
        DrivingCoach c;c.road(10,{true,1,.1});for(int i=0;i<10;++i){auto t=good(10+i*.02);check(c.evaluate(t,true,false).prompt==Prompt::none,"one image counted repeatedly");}
    });
    test("stale future and failed road evidence cannot issue bend advice",[]{
        DrivingCoach c;for(int i=0;i<5;++i)c.road(10+i*.15,{true,1,.1});
        check(c.evaluate(good(10.5),true,false).prompt==Prompt::none,"future evidence used");
        check(c.evaluate(good(11),true,false).prompt==Prompt::none,"expired bend used");
        c.road(11.1,{});check(c.evaluate(good(11.2),true,false).prompt==Prompt::none,"failed mask reused old bend");
    });
    test("bend re-arms only after a confirmed straight road",[]{
        DrivingCoach c;for(int i=0;i<5;++i)c.road(10+i*.15,{true,-1,-.1});
        c.accepted(Prompt::bendLeft,10.7);
        for(int i=0;i<=24;++i)c.road(11+i*.1,{true,0,0});
        for(int i=0;i<5;++i)c.road(17+i*.15,{true,-1,-.1});
        check(c.evaluate(good(17.7),true,false).prompt==Prompt::bendLeft,"new bend suppressed after straight road");
    });
    test("uphill and downhill choose conditional manual or general guidance",[]{
        for(bool manual:{false,true})for(double pitch:{-6.,6.}) {
            DrivingCoach c;auto d=gradeCue(c,10,pitch,manual);
            auto expected=pitch>0?(manual?Prompt::hill:Prompt::uphill):(manual?Prompt::downhillGear:Prompt::downhill);
            check(d.prompt==expected,"slope guidance wrong");
        }
        check(std::strstr(promptText(Prompt::hill),"if needed")!=nullptr,"unconditional downshift claimed");
    });
    test("brief tilt weak grades and stale uncalibrated pitch cannot give slope actions",[]{
        DrivingCoach c;auto t=good(10);t.pitchDegrees=8;check(c.evaluate(t,true,true).prompt==Prompt::none,"instant tilt triggered slope");
        t.time=10.2;t.pitchDegrees=0;c.evaluate(t,true,true);t.time=10.4;t.pitchDegrees=8;
        check(c.evaluate(t,true,true).prompt==Prompt::none,"transient tilt accumulated");
        for(int i=0;i<30;++i){t.time=11+i*.1;t.calibrated=false;check(slopePromptBand(c.evaluate(t,true,true).prompt)==0,"uncalibrated slope action");}
        t.calibrated=true;t.pitchValid=false;for(int i=0;i<30;++i){t.time=15+i*.1;check(slopePromptBand(c.evaluate(t,true,true).prompt)==0,"stale pitch slope action");}
        DrivingCoach gentle;check(gradeCue(gentle,20,1.9,true).prompt==Prompt::none,"gentle grade warned");
    });
    test("slope cue is once per episode and normal ground re-arms it",[]{
        DrivingCoach c;check(gradeCue(c,10,6).prompt==Prompt::hill,"hill missing");c.accepted(Prompt::hill,12);
        check(gradeCue(c,15,6).prompt==Prompt::none,"unchanged hill repeated");
        for(int i=0;i<25;++i){auto t=good(18+i*.1);t.pitchDegrees=0;c.evaluate(t,true,true);}
        check(gradeCue(c,22,6).prompt==Prompt::hill,"next hill suppressed");
    });
    test("slope hysteresis and missing inputs do not re-arm the same hill",[]{
        DrivingCoach c;gradeCue(c,10,6);c.accepted(Prompt::hill,12);
        check(gradeCue(c,15,4).prompt==Prompt::none,"grade threshold chatter");
        auto t=good(18);t.pitchValid=false;c.evaluate(t,true,true);
        check(gradeCue(c,20,6).prompt==Prompt::none,"sensor dropout caused repetition");
    });
    test("disabled slope guidance ignores grade without disabling camera bends",[]{
        DrivingCoach c;for(int i=0;i<30;++i){auto t=good(10+i*.1);t.pitchDegrees=9;check(c.evaluate(t,true,true,false).prompt==Prompt::none,"disabled slope played");}
        for(int i=0;i<5;++i)c.road(14+i*.15,{true,1,.1});
        check(c.evaluate(good(14.7),true,true,false).prompt==Prompt::bendRightGear,"slope toggle disabled bend/gear guidance");
    });
    test("busy audio retries guidance and collision priority stays higher",[]{
        DrivingCoach c;check(gradeCue(c,10,6,true,false).prompt==Prompt::none,"unready voice issued cue");
        auto t=good(12.1);t.pitchDegrees=6;check(c.evaluate(t,true,true).prompt==Prompt::hill,"pending cue lost");
        check(priority(Prompt::emergency)>priority(Prompt::close)&&priority(Prompt::close)>priority(Prompt::bendLeft)&&priority(Prompt::bendLeft)>priority(Prompt::traffic),"guidance priority wrong");
        t.braking=true;check(c.evaluate(t,true,true).prompt==Prompt::none,"braking ignored");
        t.braking=false;t.shock=true;check(c.evaluate(t,true,true).prompt==Prompt::none,"shock ignored");
    });
    test("necessary gear changes require fresh real gear RPM load and configured bands",[]{
        DrivingCoach noData;check(gradeCue(noData,10,0).prompt==Prompt::none,"gear inferred from speed alone");
        for(bool up:{false,true}) {
            DrivingCoach c;Decision d;
            for(int i=0;i<15;++i){double time=10+i*.1;c.engine({time,up?3500.:900.,1200,3000,3,6,true});d=c.evaluate(good(time),true,true);}
            check(d.prompt==(up?Prompt::shiftUp:Prompt::shiftDown),"necessary shift missed");
            c.accepted(d.prompt,11.4);
            for(int i=0;i<80;++i){double time=11.5+i*.1;c.engine({time,up?3500.:900.,1200,3000,3,6,true});check(c.evaluate(good(time),true,true).prompt==Prompt::none,"same gear shift repeated");}
        }
    });
    test("automatic stale neutral correct RPM and no-load engine never prompt shifts",[]{
        for(int kind=0;kind<7;++kind) {
            DrivingCoach c;
            for(int i=0;i<20;++i){double time=10+i*.1;EngineSample s{time,3500,1200,3000,3,6,true};
                if(kind==1)s.time=5;if(kind==2)s.gear=0;if(kind==3)s.rpm=2000;if(kind==4)s.underLoad=false;if(kind==5)s.rpm=NAN;if(kind==6)s.upperRpm=1000;
                c.engine(s);check(c.evaluate(good(time),true,kind!=0).prompt==Prompt::none,"invalid/unnecessary shift spoken");}
        }
    });
    test("first and top gears prohibit impossible shifts",[]{
        for(bool up:{false,true}) {DrivingCoach c;for(int i=0;i<20;++i){double time=10+i*.1;c.engine({time,up?3500.:900.,1200,3000,up?6:1,6,true});check(c.evaluate(good(time),true,true).prompt==Prompt::none,"shift outside gear range");}}
    });
    test("do not upshift during descent or a visible bend",[]{
        for(bool bend:{false,true}) {DrivingCoach c;for(int i=0;i<20;++i){double time=10+i*.1;auto t=good(time);t.pitchDegrees=bend?0:-8;
            if(bend)c.road(time,{true,-1,-.1});c.engine({time,3500,1200,3000,3,6,true});check(c.evaluate(t,true,true).prompt!=Prompt::shiftUp,"upshift defeated engine braking or bend preparation");}}
    });
    test("C ABI slope episode and RPM inputs round-trip",[]{
        auto e=ld_create();ld_telemetry t{10,0,6,0,1,1,0,1,0,0,0};int p=0;
        for(int i=0;i<=20;++i){t.time=10+i*.1;p=ld_driving_decide(e,t,1,1,1);}check(p==int(Prompt::hill),"slope ABI missed GPS-free cue");
        ld_driving_accepted(e,p,t.time);t.time=12.1;check(ld_driving_decide(e,t,1,1,1)==0,"ABI repeated accepted cue");
        ld_driving_reset(e);t.pitch=0;
        for(int i=0;i<=15;++i){t.time=20+i*.1;ld_engine_sample(e,t.time,3,6,3500,1200,3000,1);p=ld_driving_decide(e,t,1,1,1);}check(p==int(Prompt::shiftUp),"RPM ABI missed shift");ld_destroy(e);
    });
    test("camera road logits flow through native decode into bend guidance",[]{
        for(int direction:{-1,1}) {
            auto e=ld_create();ld_camera_started(e,10);
            uint8_t y[16],uv[4];std::fill_n(y,16,128);std::fill_n(uv,4,128);std::vector<float> tensor(320*320*3);
            check(ld_preprocess(e,y,16,4,1,uv,4,2,1,uv,4,2,1,4,4,0,320,tensor.data(),tensor.size()*4,0,1,1,0,1,1),"camera preprocess failed");
            auto map=curvedRoad(direction*.22);std::vector<float> road(12800,0),lane(12800,0);float detections[84]{};double hazard[3];
            for(int i=0;i<6400;++i)road[6400+i]=std::log(map.road[i]/(1-map.road[i]));
            for(double time:{10.1,10.3,10.5,10.7})check(ld_vision(e,time,detections,84,84,1,1,0,road.data(),lane.data(),80,80,hazard),"camera mask failed");
            ld_telemetry t{};t.time=10.8;
            check(ld_driving_decide(e,t,1,0,1)==int(direction<0?Prompt::bendLeftQuiet:Prompt::bendRightQuiet),"camera ABI never produced bend cue");
            double shape[13];check(ld_road_shape(e,shape,13)==13&&shape[1]==1&&shape[2]==direction,"road overlay ABI lost the detected bend");
            for(int i=3;i<13;++i)check(shape[i]>=0&&shape[i]<=1,"overlay points outside image");
            check(!ld_vision(e,10.81,detections,84,10,1,1,0,road.data(),lane.data(),80,80,hazard),"invalid detector contract accepted");
            t.time=10.82;check(ld_driving_decide(e,t,1,0,1)==0,"failed camera frame reused old road geometry");ld_destroy(e);
        }
    });
    test("shift cue re-arms after correct RPM and a changed gear",[]{
        DrivingCoach c;Decision d;
        for(int i=0;i<15;++i){double time=10+i*.1;c.engine({time,3500,1200,3000,3,6,true});d=c.evaluate(good(time),true,true);}c.accepted(d.prompt,11.4);
        for(int i=0;i<25;++i){double time=12+i*.1;c.engine({time,2000,1200,3000,3,6,true});check(c.evaluate(good(time),true,true).prompt==Prompt::none,"shift at correct RPM");}
        for(int i=0;i<15;++i){double time=18+i*.1;c.engine({time,3500,1200,3000,3,6,true});d=c.evaluate(good(time),true,true);}check(d.prompt==Prompt::shiftUp,"RPM recovery did not re-arm");c.accepted(d.prompt,19.4);
        for(int i=0;i<15;++i){double time=26+i*.1;c.engine({time,3500,1200,3000,4,6,true});d=c.evaluate(good(time),true,true);}check(d.prompt==Prompt::shiftUp,"changed gear did not allow necessary next shift");
    });
    test("stationary attention is quiet and missing speed never adds slowing",[]{
        Box b{.3f,.4f,.4f,.5f,.9f,2};
        for(double speed:{0.,3.,10.,30.}) {CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);auto d=observeAttention(c,10.1,{b},speed);
            check(d.prompt==(speed==0?Prompt::none:Prompt::roadUserQuiet),"wrong stationary or moving attention");
            check(std::strstr(promptText(d.prompt),"Slow down")==nullptr,"box-only cue requested slowing");}
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
        check(observeAttention(c,10.1,{b},30,false).prompt==Prompt::roadUserQuiet,"unknown speed assumed stationary or fast");
    });
    test("camera-only attention never appends unsupported gear instructions",[]{
        for(bool manual:{false,true}) {CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);
            auto d=observeAttention(c,10.1,{{.3f,.4f,.4f,.5f,.9f,2}},30,true,manual);
            check(d.prompt==Prompt::roadUserQuiet&&std::strstr(promptText(d.prompt),"gear")==nullptr,"camera invented gear need");}
    });
    test("unchanged vehicles speak once and verified approach still escalates",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.3f,.4f,.4f,.5f,.9f,2};auto d=observeAttention(c,10.1,{b});c.accepted(d.prompt,11.4);
        for(int i=0;i<80;++i){double t=11.5+i*.125;check(c.evaluate(t+.02,t,{b},true,false,false,30,true).prompt==Prompt::none,"static vehicle repeated");}
        b.confidence=.9;b.tti=2;b.leadTime=21.95;check(c.evaluate(22,21.95,{b},true,false,false,30,true).prompt==Prompt::keepSafeGap,"new approach ignored");
        c.accepted(Prompt::keepSafeGap,22);b.tti=.7;check(c.evaluate(22.1,22.075,{b},true,false,false,3,true).prompt==Prompt::emergency,"critical risk suppressed at crawl speed");
    });
    test("confirmed clear frames rearm attention but missing frames do not",[]{
        CameraCoach c;c.start(10);c.accepted(Prompt::camera,10);Box b{.3f,.4f,.4f,.5f,.9f,2};auto d=observeAttention(c,10.1,{b});c.accepted(d.prompt,11.4);
        check(observeAttention(c,20,{b}).prompt==Prompt::none,"missing frames rearmed same traffic");
        for(int i=0;i<20;++i){double t=22+i*.125;c.evaluate(t+.02,t,{},true,false,false,30,true);}
        check(observeAttention(c,25,{b}).prompt==Prompt::roadUserQuiet,"confirmed empty interval did not rearm");
    });
    test("already slow tactical advice skips gentle braking but retains imminent risk",[]{
        DecisionEngine e;check(e.evaluate(good(10,7),collision(10,2),true).prompt==Prompt::none,"crawl speed got unnecessary slowing");
        check(e.evaluate(good(10,3),collision(10,.7),true).prompt==Prompt::emergency,"imminent risk ignored because already slow");
        CameraCoach c;c.start(10);Box b{.3f,.4f,.4f,.5f,.9f,2};b.confidence=.9;b.tti=.7;
        check(c.evaluate(10.2,10.1,{b},true,false,false,0,true,false).prompt==Prompt::nearbyCollision,"stopped vehicle told to brake again");
    });
    test("bend and gear guidance can interrupt image cautions while real collision advice wins",[]{
        for(Prompt p:{Prompt::bendLeftGear,Prompt::bendRightQuiet,Prompt::hill,Prompt::shiftDown}) {
            check(mayInterrupt(p,Prompt::imageClose),"image-only speech starved driving advice");
            check(!mayInterrupt(p,Prompt::brake)&&!mayInterrupt(p,Prompt::emergency),"routine instruction interrupted a collision action");
        }
        check(mayInterrupt(Prompt::emergency,Prompt::bendLeftGear),"collision could not interrupt bend");
        check(!mayInterrupt(Prompt::imageClose,Prompt::bendLeftGear),"static caution re-interrupted new road guidance");
    });
    test("bend voice is speed-aware and includes manual gear preparation",[]{
        for(bool manual:{false,true})for(double speed:{3.,30.}) {DrivingCoach c;for(int i=0;i<5;++i)c.road(10+i*.15,{true,1,.1});
            auto d=c.evaluate(good(10.7,speed),true,manual);auto expected=manual?Prompt::bendRightGear:(speed<=10?Prompt::bendRightQuiet:Prompt::bendRight);
            check(d.prompt==expected,"wrong bend/gear wording");c.accepted(d.prompt,10.7);
            check(c.evaluate(good(10.8,speed),true,manual).prompt==Prompt::none,"alternate bend clip repeated same event");}
    });
    test("video test speed ignores stationary phone GPS and phone pitch",[]{
        auto measured=good(10,0);measured.pitchDegrees=-12;measured.braking=measured.hardBraking=true;
        auto selected=DecisionEngine::videoTelemetry(measured,7,8);near(selected.speedKmh,7);near(selected.pitchDegrees,8);
        check(selected.speedValid&&selected.pitchValid&&!selected.braking&&!selected.hardBraking,"phone state leaked into video test");
        near(measured.speedKmh,0);near(measured.pitchDegrees,-12);
        measured.shock=true;check(DecisionEngine::videoTelemetry(measured,7,0).shock,"shock protection lost");
        check(!DecisionEngine::videoTelemetry(measured,NAN,0).speedValid&&!DecisionEngine::videoTelemetry(measured,7,21).pitchValid,"invalid video settings accepted");
    });
    test("small central road-mask holes do not hide visible bends",[]{
        auto map=curvedRoad(.22);
        for(int y=35;y<70;++y){double z=(.82-(y+.5)/80)/.32;int center=int((.5+.22*z*z)*80);for(int x=std::max(0,center-2);x<std::min(80,center+2);++x)map.road[y*80+x]=.05f;}
        auto shape=roadShape(map);check(shape.valid&&shape.bend==1,"small car-shaped gap hid bend");
    });
    test("paired lane markings detect curvature with cropped road edges",[]{
        for(double curve:{-.22,0.,.22}) {LaneMap map;map.width=map.height=80;map.road.assign(6400,.95f);map.lane.assign(6400,.05f);
            for(int y=0;y<80;++y){double z=(.82-(y+.5)/80)/.32,center=.5+curve*z*z,width=.45-.25*std::clamp(z,0.,1.);
                for(int x=0;x<80;++x)if(std::abs((x+.5)/80-(center-width/2))<.014||std::abs((x+.5)/80-(center+width/2))<.014)map.lane[y*80+x]=.95f;}
            auto shape=roadShape(map);check(shape.valid&&shape.source==2&&shape.points.size()==10,"paired lanes never established road shape");
            check(shape.bend==(curve<0?-1:curve>0?1:0),"lane curve direction incorrect");
            map.lane.assign(6400,.05f);check(!roadShape(map).valid,"missing markings invented a bend");}
    });
    test("video telemetry and actor-specific attention round-trip through C ABI",[]{
        auto* e=ld_create();ld_camera_started(e,10);ld_camera_accepted(e,int(Prompt::camera),10);uint8_t y[4]={128,128,128,128},uv[1]={128};std::vector<float> tensor(320*320*3);
        check(ld_preprocess(e,y,4,2,1,uv,1,1,1,uv,1,1,1,2,2,0,320,tensor.data(),tensor.size()*4,0,1,1,0,1,1),"preprocess failed");
        float boxes[84]{};boxes[0]=160;boxes[1]=210;boxes[2]=120;boxes[3]=180;boxes[6]=.8f;float road[2]={2,-2};double hazard[3];int prompt=0;
        for(int i=0;i<=10;++i){double t=10.1+i*.125;check(ld_vision(e,t,boxes,84,84,1,1,0,road,road,1,1,hazard),"vision failed");ld_telemetry measured{};measured.time=t+.02;
            auto selected=ld_video_telemetry(measured,3,0);check(selected.speed_valid&&selected.speed==3,"video speed lost");prompt=ld_camera_decide_telemetry(e,selected,1,1);}
        check(prompt==int(Prompt::roadUserQuiet),"camera attention lost through ABI");check(ld_prompt_count()==55,"prompt count mismatch");ld_destroy(e);
    });
    test("slope bands agree through the native ABI at each boundary",[]{
        const std::array<std::pair<double,int>,10> boundaries{{{0,0},{1.999,0},{2,1},{4.999,1},{5,2},{7.999,2},{8,3},{11.999,3},{12,4},{30,4}}};
        for(auto [angle,band]:boundaries)for(int direction:{-1,1}) {
            check(slopeBand(angle*direction)==band*direction,"wrong slope band boundary");
            check(ld_slope_band(angle*direction)==band*direction,"display and speech band disagree");
        }
        for(double angle:{30.001,-30.001,double(NAN),infinity})check(ld_slope_band(angle)==0,"unsupported slope classified");
    });
    test("all sustained bands produce short manual and automatic slope cues",[]{
        const double angles[]{3,6,10,15,-3,-6,-10,-15};
        const Prompt manual[]{Prompt::gentleUp,Prompt::hill,Prompt::steepUp,Prompt::verySteepUp,Prompt::gentleDown,Prompt::downhillGear,Prompt::steepDownGear,Prompt::verySteepDown};
        const Prompt automatic[]{Prompt::gentleUp,Prompt::uphill,Prompt::steepUp,Prompt::verySteepUp,Prompt::gentleDown,Prompt::downhill,Prompt::steepDown,Prompt::verySteepDown};
        for(int i=0;i<8;++i)for(bool isManual:{false,true}) {
            DrivingCoach c;const auto p=gradeCue(c,10,angles[i],isManual).prompt;
            check(p==(isManual?manual[i]:automatic[i]),"wrong instruction for sustained band");
            check(std::strlen(promptText(p))<=40,"slope cue is not short");
            if(!isManual)check(std::strstr(promptText(p),"gear")==nullptr,"automatic mode requested a gear");
        }
    });
    test("a slope escalation speaks while returning to an announced band stays quiet",[]{
        DrivingCoach c;auto p=gradeCue(c,10,3).prompt;check(p==Prompt::gentleUp,"gentle cue missing");c.accepted(p,12);
        p=gradeCue(c,18,6).prompt;check(p==Prompt::hill,"increased slope not announced");c.accepted(p,20);
        check(gradeCue(c,26,3).prompt==Prompt::none,"lesser slope repeated");
        check(gradeCue(c,34,6).prompt==Prompt::none,"announced band repeated");
        p=gradeCue(c,42,10).prompt;check(p==Prompt::steepUp,"steeper band missing");c.accepted(p,44);
        p=gradeCue(c,50,-6).prompt;check(p==Prompt::downhillGear,"slope direction change missing");c.accepted(p,52);
        for(int i=0;i<=24;++i){auto t=good(58+i*.1);c.evaluate(t,true,true);}
        check(gradeCue(c,66,-6).prompt==Prompt::downhillGear,"confirmed level did not re-arm new descent");
    });
    test("band boundary noise cannot announce a grade that has already ended",[]{
        DrivingCoach c;gradeCue(c,10,6,true,false);
        for(int i=0;i<20;++i){auto t=good(12.1+i*.1);t.pitchDegrees=4.8;check(c.evaluate(t,true,true).prompt==Prompt::none,"retained upper band spoke below its entry angle");}
        auto t=good(14.1);t.pitchDegrees=5.1;check(c.evaluate(t,true,true).prompt==Prompt::hill,"hysteresis lost valid pending band");
        DrivingCoach transient;for(int i=0;i<30;++i){auto noise=good(20+i*.1);noise.pitchDegrees=i%2?5.1:4.9;check(transient.evaluate(noise,true,true).prompt==Prompt::none,"unstable boundary produced speech");}
    });
    test("unavailable slope is reported once after loss and not before initial calibration",[]{
        DrivingCoach startup;for(int i=0;i<30;++i){Telemetry t;t.time=10+i*.1;check(startup.evaluate(t,true,true).prompt==Prompt::none,"initial normalization produced unavailable speech");}
        DrivingCoach c;auto valid=good(10);c.evaluate(valid,true,true);Decision d;
        for(int i=0;i<=20;++i){auto t=good(10.1+i*.1);t.pitchValid=false;d=c.evaluate(t,true,true);}
        check(d.prompt==Prompt::slopeUnavailable,"lost slope not reported");c.accepted(d.prompt,12.1);
        for(int i=0;i<30;++i){auto t=good(13+i*.1);t.pitchValid=false;check(c.evaluate(t,true,true).prompt==Prompt::none,"unavailable repeated");}
        valid.time=20;c.evaluate(valid,true,true);
        for(int i=0;i<=20;++i){auto t=good(20.1+i*.1);t.pitchDegrees=31;d=c.evaluate(t,true,true);}
        check(d.prompt==Prompt::slopeUnavailable,"new unsupported reading not reported");
    });
    test("invalid slope reports still respect audio readiness braking shock and mute",[]{
        for(int kind=0;kind<4;++kind) {DrivingCoach c;for(int i=0;i<30;++i){auto t=good(10+i*.1);t.pitchDegrees=NAN;t.braking=kind==1;t.shock=kind==2;
            check(c.evaluate(t,kind!=0,true,kind!=3).prompt==Prompt::none,"unavailable cue ignored suppression");}}
    });
    test("steady low-speed slope response stays quiet without claiming a safe speed",[]{
        for(int kind=0;kind<7;++kind) {DrivingCoach c;Decision d;
            for(int i=0;i<=20;++i){auto t=good(10+i*.1,5);t.pitchDegrees=-10;
                if(kind!=1)c.road(t.time,{true,0,0});
                if(kind==2)t.longitudinalAcceleration=.3;if(kind==3)t.speedValid=false;if(kind==4)t.speedKmh=0;
                if(kind==5)t.longitudinalAcceleration=NAN;if(kind==6)t.speedKmh=25;
                d=c.evaluate(t,true,true,true);}
            const auto expected=kind==0||kind==1||kind==4?Prompt::none:Prompt::steepDownGear;
            check(d.prompt==expected,"slope response either claimed safe speed or ignored changed/unknown motion");
        }
    });
    test("quiet measured response preserves grade guidance when speed or acceleration increases",[]{
        for(bool accelerating:{false,true}) {DrivingCoach c;Decision d;
            for(int i=0;i<=20;++i){auto t=good(10+i*.1,5);t.pitchDegrees=-10;c.road(t.time,{true,0,0});d=c.evaluate(t,true,true,true);}
            check(d.prompt==Prompt::none,"steady descent produced unnecessary speech");
            auto t=good(18,accelerating?5:25);t.pitchDegrees=-10;if(accelerating)t.longitudinalAcceleration=.3;c.road(18,{true,0,0});
            check(c.evaluate(t,true,true,true).prompt==Prompt::steepDownGear,"quiet response silenced changed motion");
        }
    });
    test("quiet slope response does not suppress a new actor warning",[]{
        DrivingCoach c;CameraCoach camera;camera.start(10);camera.accepted(Prompt::camera,10);Decision d;
        for(int i=0;i<=20;++i){auto t=good(10+i*.1,5);t.pitchDegrees=-10;c.road(t.time,{true,0,0});d=c.evaluate(t,true,true,true);}
        check(d.prompt==Prompt::none,"steady response was not quiet");auto t=good(18,5);t.pitchDegrees=-10;c.road(18,{true,0,0});
        check(c.evaluate(t,true,true,true).prompt==Prompt::none,"steady response produced additional slope speech");
        const Box b{.3f,.4f,.4f,.5f,.9f,2};
        check(observeAttention(camera,18,{b},5,true,true).prompt==Prompt::roadUserQuiet,"obstacle warning lost at controlled speed");
    });
    test("camera ABI never turns an empty scene into a maintain-speed assurance",[]{
        auto* e=ld_create();ld_camera_started(e,10);uint8_t y[16],uv[4];std::fill_n(y,16,128);std::fill_n(uv,4,128);std::vector<float> tensor(320*320*3);
        check(ld_preprocess(e,y,16,4,1,uv,4,2,1,uv,4,2,1,4,4,0,320,tensor.data(),tensor.size()*4,0,1,1,0,1,1),"preprocess failed");
        const auto map=curvedRoad(0);std::vector<float> road(12800,0),lane(12800,0);float detections[84]{};double hazard[3];
        for(int i=0;i<6400;++i)road[6400+i]=std::log(map.road[i]/(1-map.road[i]));
        ld_telemetry t{10,5,-10,0,1,1,1,1,0,0,0};int p=0;
        for(int i=0;i<=20;++i){t.time=10+i*.1;check(ld_vision(e,t.time,detections,84,84,1,1,0,road.data(),lane.data(),80,80,hazard),"clear scene decode failed");p=ld_driving_decide(e,t,1,1,1);}
        check(p==0,"fresh empty scene produced a speed assurance");
        detections[0]=160;detections[1]=210;detections[2]=120;detections[3]=180;detections[6]=.8f;t.time=18;
        check(ld_vision(e,t.time,detections,84,84,1,1,0,road.data(),lane.data(),80,80,hazard),"obstacle decode failed");
        check(ld_driving_decide(e,t,1,1,1)==0,"new actor caused unnecessary slope speech during a steady response");
        ld_camera_accepted(e,int(Prompt::camera),10);
        int attention=0;for(int i=0;i<=10;++i){t.time=18+i*.125;check(ld_vision(e,t.time,detections,84,84,1,1,0,road.data(),lane.data(),80,80,hazard),"actor decode failed");attention=ld_camera_decide_telemetry(e,t,1,1);}
        check(attention==int(Prompt::roadUserQuiet),"sustained actor did not replace clear-scene guidance");ld_destroy(e);
    });
    test("retired maintain-speed prompt cannot acknowledge an unspoken slope lesson",[]{
        DrivingCoach c;check(gradeCue(c,10,-10).prompt==Prompt::steepDownGear,"slope lesson missing");
        c.accepted(Prompt::maintainSpeed,12);
        auto t=good(12.1);t.pitchDegrees=-10;
        check(c.evaluate(t,true,true).prompt==Prompt::steepDownGear,"unsupported speed assurance acknowledged a slope lesson");
    });
    test("new short grade cues preserve collision speech priority",[]{
        for(int i=27;i<=33;++i){auto p=Prompt(i);check(mayInterrupt(p,Prompt::imageClose),"short grade cue starved by image caution");
            check(!mayInterrupt(p,Prompt::brake)&&!mayInterrupt(p,Prompt::emergency),"short grade cue interrupted collision action");}
        for(auto p:{Prompt::maintainSpeed,Prompt::slopeUnavailable})check(!mayInterrupt(p,Prompt::imageClose)&&!mayInterrupt(p,Prompt::emergency),"routine slope message displaced caution");
    });
    test("brief detector loss retains a spoken physical actor without ghost relevance",[]{
        CameraCoach c;c.start(0);c.accepted(Prompt::camera,0);Box rider{.4f,.4f,.14f,.3f,.9f,3};
        auto d=observeAttention(c,.1,{rider});check(d.prompt==Prompt::riderSpace,"initial rider missing");
        c.accepted(d.prompt,1.4);const int actor=d.actorId;
        check(c.evaluate(1.52,1.5,{},true,false,false,30,true).prompt==Prompt::none,"missing rider retained relevance");
        for(int i=0;i<=70;++i) {
            const double t=1.625+i*.125;auto current=c.evaluate(t+.02,t,{rider},true,false,false,30,true);
            check(current.prompt==Prompt::none,"spoken rider repeated after brief detector gap");
        }
        // A pending actor may be reassociated, but cannot speak while absent.
        CameraCoach pending;pending.start(0);pending.accepted(Prompt::camera,0);
        d=observeAttention(pending,.1,{rider});const int pendingActor=d.actorId;
        pending.evaluate(1.52,1.5,{},true,false,false,30,true);
        check(pending.evaluate(1.645,1.625,{rider},true,false,false,30,true).prompt==Prompt::none,"dropout counted as sustained evidence");
        auto resumed=pending.evaluate(1.77,1.75,{rider},true,false,false,30,true);
        check(resumed.prompt==Prompt::riderSpace&&resumed.actorId==pendingActor&&actor>0,"physical actor identity lost across short detector gap");
    });
    test("optical expansion without a supported lead cannot claim a following gap",[]{
        for(bool known:{false,true}) {
            CameraCoach c;c.start(0);c.accepted(Prompt::camera,0);
            Box passing{.4f,.4f,.2f,.3f,.9f,2};passing.tti=2;passing.confidence=.9;
            auto d=c.evaluate(1.02,1,{passing},true,false,false,30,known);
            check(d.prompt==Prompt::roadUserQuiet&&d.evidenceKind==2,"unverified optical expansion claimed a following relationship");
            c.accepted(d.prompt,1.02);
            check(c.evaluate(12.02,12,{passing},true,false,false,30,known).prompt==Prompt::none,"neutral optical attention repeated without escalation");
        }
    });
    test("gap wording requires fresh independent lead support and known motion",[]{
        for(int scenario=0;scenario<4;++scenario) {
            CameraCoach c;c.start(0);c.accepted(Prompt::camera,0);
            Box lead{.4f,.4f,.2f,.3f,.9f,2};lead.tti=2;lead.confidence=.9;lead.leadTime=scenario==1?.5:1;
            auto d=c.evaluate(1.02,1,{lead},true,false,false,scenario==3?0:30,scenario!=2);
            check(d.prompt==(scenario==0?Prompt::keepSafeGap:scenario==3?Prompt::none:Prompt::roadUserQuiet),"gap wording ignored lead age or measured motion");
        }
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
