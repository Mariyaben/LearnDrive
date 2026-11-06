#include <jni.h>
#include "learndrive/perception/supercombo.hpp"
#include <cstring>
#include <sstream>
using namespace ld;
using namespace ld::perception;
#define SPATIAL(name) Java_ai_learndrive_mobile_SpatialVision_##name
static void fail(JNIEnv* e,const char* message){auto c=e->FindClass("java/lang/IllegalStateException");if(c)e->ThrowNew(c,message);}
extern "C" {
JNIEXPORT jlong JNICALL SPATIAL(create)(JNIEnv* e,jobject,jstring path,jstring qnn) {
    const char* p=e->GetStringUTFChars(path,nullptr);if(!p)return 0;
    const char* q=qnn?e->GetStringUTFChars(qnn,nullptr):nullptr;
    jlong handle=0;
    try{handle=reinterpret_cast<jlong>(new SupercomboRuntime(p,{true,q?q:""}));}
    catch(const std::exception& ex){fail(e,ex.what());}
    e->ReleaseStringUTFChars(path,p);if(q)e->ReleaseStringUTFChars(qnn,q);return handle;
}
JNIEXPORT void JNICALL SPATIAL(destroy)(JNIEnv*,jobject,jlong handle){delete reinterpret_cast<SupercomboRuntime*>(handle);}
JNIEXPORT void JNICALL SPATIAL(reset)(JNIEnv*,jobject,jlong handle){if(handle)reinterpret_cast<SupercomboRuntime*>(handle)->reset();}
JNIEXPORT jstring JNICALL SPATIAL(provider)(JNIEnv* e,jobject,jlong h){
    if(!h)return e->NewStringUTF("unavailable");
    const auto& r=reinterpret_cast<SupercomboRuntime*>(h)->report();std::ostringstream s;
    s<<r.selected<<" · warm inference "<<r.baselineMs<<" ms · big-core affinity "<<r.bigCoreAffinity;
    if(!r.fallbackReason.empty())s<<" · "<<r.fallbackReason;
    return e->NewStringUTF(s.str().c_str());
}
JNIEXPORT jboolean JNICALL SPATIAL(process)(JNIEnv* e,jobject,jlong handle,
    jobject y,jint yr,jint yp,jobject u,jint ur,jint up,jobject v,jint vr,jint vp,
    jint width,jint height,jint rotation,jboolean full,jdouble time,jdoubleArray geometry,jobject output) {
    if(!handle||!geometry||e->GetArrayLength(geometry)!=9)return false;
    double g[9];e->GetDoubleArrayRegion(geometry,0,9,g);
    auto plane=[&](jobject b,int row,int step){const auto n=e->GetDirectBufferCapacity(b);
        return Plane{static_cast<uint8_t*>(e->GetDirectBufferAddress(b)),n>0?size_t(n):0,row,step};};
    auto* out=e->GetDirectBufferAddress(output);
    if(!out||e->GetDirectBufferCapacity(output)<jlong(supercomboOutputSize*sizeof(float)))return false;
    try {
        CameraIntrinsics c{g[0],g[1],g[2],g[3],int(g[4]),int(g[5])};
        const auto warp=supercomboWarp(c,{g[6],g[7],g[8]});
        auto* runtime=reinterpret_cast<SupercomboRuntime*>(handle);
        runtime->run({width,height,rotation,plane(y,yr,yp),plane(u,ur,up),plane(v,vr,vp),bool(full)},warp,time);
        std::memcpy(out,runtime->rawOutput(),supercomboOutputSize*sizeof(float));return true;
    }catch(const std::exception& ex){fail(e,ex.what());return false;}
}
}
