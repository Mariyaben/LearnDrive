#pragma once
#include "types.hpp"
#include <cstddef>
#include <cstdint>
namespace ld {
struct Plane { const uint8_t* data{}; size_t size{}; int rowStride{},pixelStride{}; };
struct YuvFrame { int width{},height{},rotation{}; Plane y,u,v; bool fullRange=true; };
struct Transform { int width{},height{},size{}; float scale{},padX{},padY{}; };
struct Gray { int width=80,height=80; std::vector<uint8_t> pixels; };
// Square region of the upright frame (pixels), for the zoomed detector pass.
struct Crop { int x{},y{},side{}; };
// dtype: 0 float32, 1 uint8, 2 int8. layout: 0 NHWC, 1 NCHW. With crop, only that
// region is letterboxed (the returned transform is in crop pixels).
Transform preprocess(const YuvFrame&,int size,void* tensor,size_t capacity,int dtype,int layout,float quantScale,int zeroPoint,Gray* gray=nullptr,const Crop* crop=nullptr);
// The road-ahead zoom: a square of half the long side (at most the short side),
// centred horizontally and on `horizon` (fraction of the height) vertically.
Crop zoomCrop(int width,int height,float horizon);
// Rewrites zoomed YOLO raw output ([84,N] channel-first, crop model pixels) into
// full-frame model pixels. Boxes touching the crop edge are partial objects the
// full frame covers; their scores are cleared.
void zoomToFull(float* values,int candidates,const Transform& full,const Transform& zoom,const Crop& crop);
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
