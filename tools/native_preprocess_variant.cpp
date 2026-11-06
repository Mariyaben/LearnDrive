// Compile this translation unit and the two pure preprocessing implementations
// twice. -Dld=ld_candidate keeps the candidate's C++ types and symbols distinct;
// only primitive buffers and scalar arguments cross this C ABI.
#include "learndrive/vision.hpp"
#include "learndrive/perception/supercombo.hpp"
#include <algorithm>
#include <cstring>

#ifdef PROBE_CANDIDATE
#define PROBE_EXPORT(name) ld_candidate_probe_##name
#else
#define PROBE_EXPORT(name) ld_probe_##name
#endif

#define FRAME_ARGUMENTS \
    const uint8_t* y, size_t yn, int yr, int yp, \
    const uint8_t* u, size_t un, int ur, int up, \
    const uint8_t* v, size_t vn, int vr, int vp, \
    int width, int height, int rotation, int full

namespace {
ld::YuvFrame frame(FRAME_ARGUMENTS) {
    return {width,height,rotation,{y,yn,yr,yp},{u,un,ur,up},{v,vn,vr,vp},full!=0};
}
}

extern "C" int PROBE_EXPORT(preprocess)(FRAME_ARGUMENTS,
        float* tensor, size_t tensorCount, uint8_t* luminance, size_t luminanceCount,
        double* transform, size_t transformCount) noexcept {
    try {
        if (!tensor || tensorCount != 3*320*320 || !luminance || luminanceCount != 80*80 ||
                !transform || transformCount != 6) return 0;
        // Reuse the gray allocation, as ld_engine does in the actual camera path.
        // Copying its small public output is identical in both variants and is
        // included in the reported wrapper timings.
        thread_local ld::Gray gray;
        const auto tr=ld::preprocess(frame(y,yn,yr,yp,u,un,ur,up,v,vn,vr,vp,
                width,height,rotation,full),320,tensor,tensorCount*sizeof(float),0,1,1.f,0,&gray);
        if(gray.pixels.size()!=luminanceCount) return 0;
        std::memcpy(luminance,gray.pixels.data(),luminanceCount);
        transform[0]=tr.width;transform[1]=tr.height;transform[2]=tr.size;
        transform[3]=tr.scale;transform[4]=tr.padX;transform[5]=tr.padY;
        return 1;
    } catch (...) { return 0; }
}

extern "C" int PROBE_EXPORT(pack)(FRAME_ARGUMENTS,
        const double* warp, size_t warpCount, float* packed, size_t packedCount) noexcept {
    try {
        if(!warp || warpCount!=9 || !packed || packedCount!=ld::perception::supercomboFrameSize) return 0;
        ld::perception::Matrix3 matrix{};
        std::copy_n(warp,9,matrix.begin());
        ld::perception::packSupercombo(frame(y,yn,yr,yp,u,un,ur,up,v,vn,vr,vp,
                width,height,rotation,full),matrix,packed,packedCount);
        return 1;
    } catch (...) { return 0; }
}
