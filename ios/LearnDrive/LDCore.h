#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
NS_ASSUME_NONNULL_BEGIN
@interface LDCore : NSObject
- (void)reset;
- (void)imu:(double)time acceleration:(NSArray<NSNumber*>*)acc gyro:(NSArray<NSNumber*>*)gyro linear:(NSArray<NSNumber*>*)linear;
- (void)gps:(double)time speed:(double)speed accuracy:(double)accuracy;
- (NSArray<NSNumber*>*)snapshot:(double)time;
- (NSArray<NSNumber*>*)adviceTelemetry:(NSArray<NSNumber*>*)telemetry requireGps:(BOOL)requireGps manualSpeed:(double)manualSpeed;
- (int)decide:(NSArray<NSNumber*>*)telemetry hazardTime:(double)hazardTime tti:(double)tti confidence:(double)confidence path:(BOOL)path ready:(BOOL)ready gear:(BOOL)gear;
- (void)accepted:(double)time;
- (void)drivingReset;
- (int)drivingAdvice:(NSArray<NSNumber*>*)telemetry ready:(BOOL)ready manual:(BOOL)manual slope:(BOOL)slope;
- (void)drivingAccepted:(int)prompt time:(double)time;
- (int)promptPriority:(int)prompt;
- (int)promptCount;
- (BOOL)promptMayInterrupt:(int)next current:(int)current;
- (NSString*)promptText:(int)prompt;
- (BOOL)prepare:(CVPixelBufferRef)frame tensor:(void*)tensor bytes:(NSUInteger)bytes gray:(BOOL)gray;
- (nullable NSArray<NSNumber*>*)vision:(double)time detections:(const float*)detections count:(NSUInteger)count candidates:(int)candidates channelFirst:(BOOL)channelFirst road:(const float*)road lane:(const float*)lane width:(int)width height:(int)height;
- (NSString*)error;
@end
NS_ASSUME_NONNULL_END
