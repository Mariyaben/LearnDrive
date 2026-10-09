#import "LDCore.h"
#include "learndrive/c_api.h"
#include <mutex>
@implementation LDCore {
    ld_engine* _engine;
    std::mutex _mutex;
}
- (instancetype)init {if((self=[super init])){_engine=ld_create();if(!_engine)return nil;}return self;}
- (void)dealloc {ld_destroy(_engine);}
- (void)reset {std::lock_guard<std::mutex> lock(_mutex);ld_reset(_engine);}
- (void)imu:(double)t acceleration:(NSArray<NSNumber*>*)a gyro:(NSArray<NSNumber*>*)g linear:(NSArray<NSNumber*>*)l {
    if(a.count!=3||g.count!=3||l.count!=3)return;
    double av[3],gv[3],lv[3];for(int i=0;i<3;++i){av[i]=a[i].doubleValue;gv[i]=g[i].doubleValue;lv[i]=l[i].doubleValue;}
    std::lock_guard<std::mutex> lock(_mutex);ld_imu(_engine,t,av,gv,lv,1);
}
- (void)gps:(double)t speed:(double)s accuracy:(double)a {std::lock_guard<std::mutex> lock(_mutex);ld_gps(_engine,t,s,a);}
- (NSArray<NSNumber*>*)snapshot:(double)t {
    std::lock_guard<std::mutex> lock(_mutex);auto s=ld_snapshot(_engine,t);
    return @[@(s.time),@(s.speed),@(s.pitch),@(s.acceleration),@(s.progress),@(s.calibrated),@(s.speed_valid),@(s.pitch_valid),@(s.braking),@(s.hard_braking),@(s.shock)];
}
- (NSArray<NSNumber*>*)adviceTelemetry:(NSArray<NSNumber*>*)v requireGps:(BOOL)required manualSpeed:(double)speed {
    if(v.count!=11)return v;
    ld_telemetry measured{v[0].doubleValue,v[1].doubleValue,v[2].doubleValue,v[3].doubleValue,v[4].doubleValue,v[5].intValue,v[6].intValue,v[7].intValue,v[8].intValue,v[9].intValue,v[10].intValue};
    auto selected=ld_advice_telemetry(measured,required,speed);
    NSMutableArray<NSNumber*>* out=[v mutableCopy];out[1]=@(selected.speed);out[6]=@(selected.speed_valid);return out;
}
- (int)decide:(NSArray<NSNumber*>*)v hazardTime:(double)ht tti:(double)tti confidence:(double)c path:(BOOL)p ready:(BOOL)r gear:(BOOL)g {
    if(v.count!=11)return 0;
    ld_telemetry t{v[0].doubleValue,v[1].doubleValue,v[2].doubleValue,v[3].doubleValue,v[4].doubleValue,v[5].intValue,v[6].intValue,v[7].intValue,v[8].intValue,v[9].intValue,v[10].intValue};
    std::lock_guard<std::mutex> lock(_mutex);return ld_decide(_engine,t,ht,tti,c,p,r,g);
}
- (void)accepted:(double)t {std::lock_guard<std::mutex> lock(_mutex);ld_accepted(_engine,t);}
- (void)drivingReset {std::lock_guard<std::mutex> lock(_mutex);ld_driving_reset(_engine);}
- (int)drivingAdvice:(NSArray<NSNumber*>*)v ready:(BOOL)r manual:(BOOL)m slope:(BOOL)s {
    if(v.count!=11)return 0;
    ld_telemetry t{v[0].doubleValue,v[1].doubleValue,v[2].doubleValue,v[3].doubleValue,v[4].doubleValue,v[5].intValue,v[6].intValue,v[7].intValue,v[8].intValue,v[9].intValue,v[10].intValue};
    std::lock_guard<std::mutex> lock(_mutex);return ld_driving_decide(_engine,t,r,m,s);
}
- (void)drivingAccepted:(int)p time:(double)t {std::lock_guard<std::mutex> lock(_mutex);ld_driving_accepted(_engine,p,t);}
- (int)promptPriority:(int)p {return ld_prompt_priority(p);}
- (int)promptCount {return ld_prompt_count();}
- (BOOL)promptMayInterrupt:(int)n current:(int)c {return ld_prompt_may_interrupt(n,c);}
- (NSString*)promptText:(int)p {return [NSString stringWithUTF8String:ld_prompt_text(p)];}
- (NSString*)error {std::lock_guard<std::mutex> lock(_mutex);return [NSString stringWithUTF8String:ld_error(_engine)];}
- (BOOL)prepare:(CVPixelBufferRef)f tensor:(void*)tensor bytes:(NSUInteger)bytes gray:(BOOL)gray {
    if(CVPixelBufferGetPixelFormatType(f)!=kCVPixelFormatType_420YpCbCr8BiPlanarFullRange||CVPixelBufferGetPlaneCount(f)!=2)return NO;
    if(CVPixelBufferLockBaseAddress(f,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess)return NO;
    std::lock_guard<std::mutex> lock(_mutex);
    auto y=static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(f,0));
    auto uv=static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(f,1));
    int yr=int(CVPixelBufferGetBytesPerRowOfPlane(f,0)),ur=int(CVPixelBufferGetBytesPerRowOfPlane(f,1));
    size_t yn=yr*CVPixelBufferGetHeightOfPlane(f,0),un=ur*CVPixelBufferGetHeightOfPlane(f,1);
    BOOL success=ld_preprocess(_engine,y,yn,yr,1,uv,un,ur,2,uv+1,un-1,ur,2,int(CVPixelBufferGetWidth(f)),int(CVPixelBufferGetHeight(f)),0,320,tensor,bytes,0,1,1,0,1,gray);
    CVPixelBufferUnlockBaseAddress(f,kCVPixelBufferLock_ReadOnly);return success;
}
- (NSArray<NSNumber*>*)vision:(double)t detections:(const float*)d count:(NSUInteger)n candidates:(int)c channelFirst:(BOOL)cf road:(const float*)road lane:(const float*)lane width:(int)w height:(int)h {
    std::lock_guard<std::mutex> lock(_mutex);double out[3];
    if(!ld_vision(_engine,t,d,n,84,c,cf,0,road,lane,w,h,out))return nil;
    return @[@(out[0]),@(out[1]),@(out[2])];
}
@end
