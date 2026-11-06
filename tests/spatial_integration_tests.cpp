#include "learndrive/perception/supercombo.hpp"
#include "learndrive/ContextualPolicyEngine.hpp"
#include "learndrive/c_api.h"
#include "learndrive/fusion.hpp"
#include "learndrive/decision.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <cstdlib>
#include <new>
#include <chrono>
static std::atomic<bool> counting{false};static std::atomic<size_t> allocations{0};
void* operator new(std::size_t n){if(counting)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace ld;using namespace ld::perception;using namespace ld::cognition;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){
    try {
        CameraIntrinsics k{910,910,256,47.6,512,256};
        SensorFusion fusion;
        check(fusion.cameraUp(0).norm()==0,"startup invented an upright pose");
        for(int i=0;i<20;++i)fusion.imu({i*.01,{0,gravity,0},{},{},true});
        check(fusion.cameraUp(.19).norm()==0,"gravity warmup bypassed");
        fusion.imu({.20,{0,gravity,0},{},{},true});
        check(fusion.cameraUp(.20).norm()>.9&&!fusion.snapshot(.20).calibrated,
            "automatic horizon readiness incorrectly requires level-ground normalization");
        fusion.reset();
        for(int i=0;i<=600;++i)fusion.imu({i*.01,{0,gravity,0},{},{},true});
        check(fusion.cameraUp(5.98).norm()>.9,"capture-time IMU pose was discarded when newer sensor samples existed");
        check(fusion.cameraUp(4.9).norm()==0&&fusion.cameraUp(6.3).norm()==0,"expired camera pose accepted");
        const Matrix3 identity{1,0,0,0,1,0,0,0,1};
        const auto warp=supercomboWarp(k,{0,gravity,0});
        for(size_t i=0;i<9;++i)check(std::abs(warp[i]-identity[i])<1e-8,"canonical warp is not identity");
        const auto h=calibrateHorizon({2,8,3},k.fx,k.fy,k.cx,k.cy);
        for(const auto bad:std::array<Vec3,5>{{{0,0,0},{0,0,1},{0,0,-1},{.1,0,1},{NAN,1,0}}})
            check(!calibrateHorizon(bad,k.fx,k.fy,k.cx,k.cy).valid,"missing/near-vertical gravity accepted");
        check(!calibrateHorizon({0,gravity,0},0,k.fy,k.cx,k.cy).valid,"missing optics accepted");
        for(const auto up:std::array<Vec3,4>{{{1,0,0},{-1,0,0},{0,-1,0},{.5,.5,.7071067811865476}}})
            check(calibrateHorizon(up,k.fx,k.fy,k.cx,k.cy).valid,"usable landscape/tilted mount rejected");
        bool rejected=false;try{supercomboWarp(k,{0,0,1});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"degenerate mount reached model warp");
        DrivingCoach terrain;RoadShape bend;bend.valid=true;bend.bend=1;
        terrain.road(1,bend);terrain.road(1.2,bend);terrain.road(1.5,bend);
        Telemetry mountTelemetry;mountTelemetry.time=1.5;mountTelemetry.speedKmh=30;mountTelemetry.speedValid=true;
        check(terrain.evaluate(mountTelemetry,true,false,false).prompt==Prompt::bendRight,"bend fixture not active");
        terrain.roadUnavailable(1.5);
        check(!terrain.shape().valid&&terrain.evaluate(mountTelemetry,true,false,false).prompt==Prompt::none,
            "same-frame pose loss retained road shape or pending bend");
        const auto inv=inverse(h.horizonHomography),product=multiply(h.horizonHomography,inv);
        for(size_t i=0;i<9;++i)check(std::abs(product[i]-identity[i])<1e-8,"tilted homography inversion");
        const Matrix3 intrinsic{910,0,256,0,910,47.6,0,0,1};
        const auto rotation=multiply(multiply(inverse(intrinsic),h.horizonHomography),intrinsic);
        const Vec3 cameraUp{2,-8,-3};
        const double levelX=rotation[0]*cameraUp.x+rotation[1]*cameraUp.y+rotation[2]*cameraUp.z;
        const double levelZ=rotation[6]*cameraUp.x+rotation[7]*cameraUp.y+rotation[8]*cameraUp.z;
        check(std::abs(levelX)<1e-6&&std::abs(levelZ)<1e-6,"homography did not align tilted gravity");
        double x=0,y=0;check(projectVehicle(k,{10,1,2},x,y)&&std::abs(x-347)<1e-8&&std::abs(y-229.6)<1e-8,"vehicle/view axis convention");
        std::vector<uint8_t> luma(512*256),uv(256*128,128);for(size_t i=0;i<luma.size();++i)luma[i]=uint8_t(i%251);
        std::vector<float> packed(supercomboFrameSize);
        packSupercombo({512,256,0,{luma.data(),luma.size(),512,1},{uv.data(),uv.size(),256,1},{uv.data(),uv.size(),256,1},false},identity,packed.data(),packed.size());
        check(packed[0]==luma[0]&&packed[32768]==luma[512]&&packed[65536]==luma[1]&&packed[98304]==luma[513],"upstream YUV channel order");
        std::array<float,6472> raw{};raw[990]=5;raw[5857]=10;
        for(int j=0;j<33;++j){raw[j*15]=float(j*4);raw[4955+66+j*2]=-1.8f;raw[4955+132+j*2]=1.8f;}
        raw[5755]=20;raw[5755+1]=.2f;raw[5755+2]=5;raw[5755+3]=-1;raw[5755+48]=5;
        auto decoded=decodeSupercombo(raw.data(),raw.size());check(decoded.valid&&decoded.leads[0].distanceM==20,"Supercombo lead decoder offset");
        Telemetry t;t.time=10;t.speedKmh=54;t.speedValid=true;
        auto scene=spatialScene(decoded,t,10,1);check(scene.count==1&&std::abs(scene.hazards[0].ttiSeconds-2)<1e-5,"absolute lead speed interpreted as relative speed");
        auto merging=decoded;merging.leads[0].lateralM=3;merging.leads[0].lateralVelocityMps=-1;
        check(spatialScene(merging,t,10,2).hazards[0].inTrajectory,"lateral encroachment ignored");
        ReflexEngine reflex;scene.hazards[0].ttiSeconds=2.5;t.longitudinalAcceleration=-4;scene.telemetry=t;
        check(reflex.evaluate(scene,10).suppressDistanceAlerts&&reflex.evaluate(scene,10).command==ReflexCommand::none,"braking suppression");
        scene.hazards[0].ttiSeconds=1;check(reflex.evaluate(scene,10).command==ReflexCommand::BRAKE_HARDER,"harder override");
        scene.telemetry.shock=true;
        check(reflex.evaluate(scene,10).command==ReflexCommand::none,"phone shock bypassed action suppression");
        scene.telemetry.shock=false;
        scene.telemetry.longitudinalAcceleration=0;scene.hazards[0].ttiSeconds=.05;
        check(reflex.evaluate(scene,10.1).command!=ReflexCommand::none,"capture-age subtraction discarded an already imminent collision");
        scene.telemetry.time=9;scene.telemetry.longitudinalAcceleration=-4;
        check(!reflex.evaluate(scene,10).suppressDistanceAlerts,"stale IMU suppression");
        scene.captureTime=10;scene.telemetry=t;scene.telemetry.longitudinalAcceleration=0;scene.count=maxHazards;
        for(size_t i=0;i<scene.count;++i)scene.hazards[i]={i+1,i%2?HazardType::bus:HazardType::pothole,Lane::ego,40,5,.9,true,false,1,0};
        ContextualPolicyEngine policy;
        volatile int consumed=0;
        counting=true;const auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<100000;++i){consumed+=int(reflex.evaluate(scene,10).command)+int(policy.evaluate(scene,10).action);}
        const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/100000;
        counting=false;check(allocations==0,"reflex/tactical execution allocated heap memory");
        auto* engine=ld_create();check(engine,"engine allocation");
        check(ld_audio_allowed(engine,36,10),"first prompt rejected");ld_audio_accepted(engine,36,10);
        check(!ld_audio_allowed(engine,37,13.49)&&ld_audio_allowed(engine,37,13.5),"global 3.5s cooldown");
        check(!ld_audio_allowed(engine,36,19.99)&&ld_audio_allowed(engine,36,20),"10s duplicate lock");
        check(ld_audio_allowed(engine,42,10.01),"emergency blocked by tactical cooldown");ld_destroy(engine);
        auto* corroborated=ld_create();check(corroborated,"corroboration engine allocation");
        const double geometry[9]{910,910,256,47.6,512,256,0,1,0};
        check(ld_spatial_geometry(corroborated,geometry),"corroboration geometry");
        raw[5755]=10;raw[5755+2]=0;
        check(ld_spatial_publish(corroborated,10,raw.data(),raw.size()),"publish diagnostic lead");
        ld_telemetry replay{10,54,0,0,0,0,1,0,0,0,0};int suppress=0;
        check(ld_dual_policy_decide(corroborated,replay,10,&suppress)==0,"uncorroborated neural lead created emergency speech");
        std::vector<float> rgb(3*320*320);
        check(ld_preprocess(corroborated,luma.data(),luma.size(),512,1,uv.data(),uv.size(),256,1,
            uv.data(),uv.size(),256,1,512,256,0,320,rgb.data(),rgb.size()*sizeof(float),0,1,1,0,0,1),"semantic preparation");
        float detection[84]{};detection[0]=274.2f*.625f;detection[1]=140*.625f+80;
        detection[2]=80*.625f;detection[3]=80*.625f;detection[6]=.95f;double hazard[3]{};
        check(ld_spatial_semantics(corroborated,10,detection,84,1,1,hazard),"corroborating semantic box");
        check(ld_spatial_publish(corroborated,10,raw.data(),raw.size()),"merge corroborating lead");
        check(ld_dual_policy_decide(corroborated,replay,10.01,&suppress)==0,"a matching box without closing evidence created emergency speech");
        ld_destroy(corroborated);
        std::cout<<"spatial contracts, warp, zero allocations, native audio gates passed; combined host mean "<<us<<" us\n";
        return 0;
    }catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}
}
