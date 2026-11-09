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
//
// Long range (0.8.3): with a high-resolution camera frame the spare rows hold TWO zoomed views,
// 2x and 4x the road band's scale, each centred where the road will be (`aim`, from the route
// or the turn rate), and the road band starts nearer the horizon (sky is not a road user).
// A 1280-wide frame then shows the far view at native resolution: a person 60 m ahead is
// ~23 px tall instead of ~6 px in the road band. `smooth` box-filters downscaled views (a
// 1280-wide frame shrunk 4x by nearest-neighbour sampling aliases thin edges into noise).
struct MosaicPlan {
    float horizon=.5f;       // horizon row, fraction of the upright frame height
    float above=.5f;         // the road band starts this fraction of the frame height above it
    int views=1;             // zoomed views, 0-2 (fewer when there is no room)
    float zoom[2]{2,4};      // each view's scale relative to the road band
    float aim[2]{.5f,.5f};   // each view's centre, fraction of the frame width
    bool smooth{};
};
struct MosaicView { int top{},rows{}; float srcX{},srcY{},scale{}; };
struct Mosaic { bool active{}; int bandBottom{},views{}; MosaicView view[2]{}; float visibleTop{};
    // The first zoom view (0.8.0 field names).
    int stripTop() const noexcept {return view[0].top;}
};
Transform preprocessMosaic(const YuvFrame&,int size,void* tensor,size_t capacity,int dtype,int layout,float quantScale,int zeroPoint,
    Gray* gray,const MosaicPlan& plan,Mosaic& mosaic);
// The 0.8.0 layout: road band from half the height above the horizon, one centred 2x view,
// nearest-neighbour sampling.
Transform preprocessMosaic(const YuvFrame&,int size,void* tensor,size_t capacity,int dtype,int layout,float quantScale,int zeroPoint,
    Gray* gray,float horizon,Mosaic& mosaic);
// Raw YOLO output ([84,N] channel-first or [N,84], model pixels) from a mosaic: zoom-view
// candidates are rewritten into road-band coordinates; any candidate cut by a view's edge (a
// partial object another view covers) or straddling two views has its scores cleared.
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
