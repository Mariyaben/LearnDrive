#pragma once
#include "types.hpp"
#include <cstddef>
#include <cstdint>
namespace ld {
struct Plane { const uint8_t* data{}; size_t size{}; int rowStride{},pixelStride{}; };
struct YuvFrame { int width{},height{},rotation{}; Plane y,u,v; bool fullRange=true; };
struct Transform { int width{},height{},size{}; float scale{},padX{},padY{}; };
struct Gray { int width=80,height=80; std::vector<uint8_t> pixels; };
// dtype: 0 float32, 1 uint8, 2 int8. layout: 0 NHWC, 1 NCHW.
Transform preprocess(const YuvFrame&,int size,void* tensor,size_t capacity,int dtype,int layout,float quantScale,int zeroPoint,Gray* gray=nullptr);
std::vector<Box> decodeYolo(const float* values,size_t count,int channels,int candidates,bool channelFirst,bool normalized,const Transform&);
LaneMap decodeLanes(const float* road,const float* lane,int width,int height,const Transform&);
class ApproachTracker {
public:
    Hazard update(double time,std::vector<Box>& boxes,const Gray& gray,const LaneMap& lanes);
    void reset();
private:
    struct Track { Box box; int hits{}; double rate{}; };
    std::vector<Track> tracks_;
    Gray previous_;
    double last_=-1;
    int nextId_=1;
};
float iou(const Box&,const Box&);
}
