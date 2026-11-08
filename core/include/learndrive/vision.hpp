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
// The detector image as two views, for the cost of one inference. Letterboxing a 4:3 or 16:9
// frame into a square wastes a quarter to almost half of it on grey, and the rows well above
// the horizon (sky, tree canopy, building tops) never hold a road user. The mosaic keeps:
//   * the road band: the full frame width at the usual scale, from 50% of the frame height
//     above the horizon down to the bottom (the returned transform maps it, padY <= 0);
//   * the zoom band, in the rest of the square: 2x the road band's scale over the middle of
//     the frame around the horizon, where road users far ahead are only a few pixels tall.
// Zoom-band detections are mapped back into road-band coordinates (mosaicToFull) before any
// decoding, so distances from box geometry stay in true frame pixels (and are sharper).
// Without room for a useful zoom band (< 48 rows) this is the plain letterbox (active=false).
struct Mosaic { bool active{}; int bandBottom{},stripTop{}; float srcX{},srcY{},stripScale{},visibleTop{}; };
Transform preprocessMosaic(const YuvFrame&,int size,void* tensor,size_t capacity,int dtype,int layout,float quantScale,int zeroPoint,
    Gray* gray,float horizon,Mosaic& mosaic);
// Raw YOLO output ([84,N] channel-first or [N,84], model pixels) from a mosaic: zoom-band
// candidates are rewritten into road-band coordinates; any candidate cut by a band edge (a
// partial object the other view covers) or straddling the two bands has its scores cleared.
void mosaicToFull(float* values,int candidates,bool channelFirst,const Transform&,const Mosaic&);
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
