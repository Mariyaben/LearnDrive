// Same-process O0/O2 comparison of app-owned pure preprocessing. No camera,
// permissions, JNI, ORT, navigation, image files or network access is involved.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define FRAME_ARGUMENTS \
    const uint8_t*, size_t, int, int, const uint8_t*, size_t, int, int, \
    const uint8_t*, size_t, int, int, int, int, int, int
#define DECLARE_VARIANT(prefix) \
    extern "C" int prefix##_preprocess(FRAME_ARGUMENTS,float*,size_t,uint8_t*,size_t,double*,size_t) noexcept; \
    extern "C" int prefix##_pack(FRAME_ARGUMENTS,const double*,size_t,float*,size_t) noexcept;
DECLARE_VARIANT(ld_probe)
DECLARE_VARIANT(ld_candidate_probe)
using Preprocess=int(*)(FRAME_ARGUMENTS,float*,size_t,uint8_t*,size_t,double*,size_t) noexcept;
using Pack=int(*)(FRAME_ARGUMENTS,const double*,size_t,float*,size_t) noexcept;
using Clock=std::chrono::steady_clock;
constexpr size_t detectorCount=3*320*320,grayCount=80*80,packCount=6*128*256;
constexpr double wallBudgetMs=8000;

struct Plane {
    std::vector<uint8_t> bytes;
    size_t offset{};
    int row{},step{};
    const uint8_t* data() const {return bytes.data()+offset;}
    size_t size() const {return bytes.size()-offset;}
};
struct Frame {
    std::string name;
    int width{},height{},rotation{},full{};
    Plane y,u,v;
};
struct Warp {std::string name;std::array<double,9> values;};
struct Difference {
    uint64_t compared{},different{},nonfinite{};
    double maxAbs{};
    void floats(const float* a,const float* b,size_t count) {
        compared+=count;
        for(size_t i=0;i<count;++i) {
            uint32_t av{},bv{};std::memcpy(&av,a+i,4);std::memcpy(&bv,b+i,4);
            different+=av!=bv;
            if(!std::isfinite(a[i]) || !std::isfinite(b[i])) ++nonfinite;
            else maxAbs=std::max(maxAbs,std::abs(double(a[i])-double(b[i])));
        }
    }
    void doubles(const double* a,const double* b,size_t count) {
        compared+=count;
        for(size_t i=0;i<count;++i) {
            uint64_t av{},bv{};std::memcpy(&av,a+i,8);std::memcpy(&bv,b+i,8);
            different+=av!=bv;
            if(!std::isfinite(a[i]) || !std::isfinite(b[i])) ++nonfinite;
            else maxAbs=std::max(maxAbs,std::abs(a[i]-b[i]));
        }
    }
    void bytes(const uint8_t* a,const uint8_t* b,size_t count) {
        compared+=count;
        for(size_t i=0;i<count;++i) {
            different+=a[i]!=b[i];maxAbs=std::max(maxAbs,std::abs(double(a[i])-double(b[i])));
        }
    }
    bool exact() const {return different==0 && nonfinite==0;}
    std::string json() const {
        std::ostringstream s;s<<std::setprecision(17)<<"{\"compared\":"<<compared
            <<",\"bit_differences\":"<<different<<",\"nonfinite\":"<<nonfinite
            <<",\"bit_exact\":"<<(exact()?"true":"false")<<",\"max_abs\":"<<maxAbs<<"}";
        return s.str();
    }
};
struct Buffers {
    std::vector<float> detector=std::vector<float>(detectorCount),packed=std::vector<float>(packCount);
    std::array<uint8_t,grayCount> gray{};
    std::array<double,6> transform{};
};

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
bool withinBudget(Clock::time_point start) {return elapsed(start)<wallBudgetMs;}
Plane plane(int width,int height,int step,int padding,int left,int top,int seed,bool full,bool chroma) {
    Plane out;out.step=step;out.row=width*step+padding;
    out.bytes.assign(size_t(out.row)*height+32,0x7d);
    uint32_t random=uint32_t(seed);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        random=random*1664525u+1013904223u;
        const unsigned value=(random>>24)+unsigned(x*17+y*11);
        out.bytes[size_t(y)*out.row+size_t(x)*step]=uint8_t(full?value%256:16+value%(chroma?225:220));
    }
    out.offset=size_t(top)*out.row+size_t(left)*step;
    return out;
}
Frame fixture(int rotation,int full,int profile) {
    // Even and odd sizes, compact/padded rows, luma pixel strides 1/2,
    // planar/interleaved-style chroma strides and even/odd crop offsets.
    constexpr int widths[]{512,641,640,319},heights[]{256,479,480,241};
    constexpr int lefts[]{0,4,3,6},tops[]{0,6,5,4};
    const int width=widths[profile],height=heights[profile],left=lefts[profile],top=tops[profile];
    const int backingWidth=width+left+8,backingHeight=height+top+8;
    Frame f;f.name="rot"+std::to_string(rotation)+(full?"_full_":"_limited_")+std::to_string(width)+"x"+std::to_string(height);
    f.width=width;f.height=height;f.rotation=rotation;f.full=full;
    f.y=plane(backingWidth,backingHeight,profile==2?2:1,profile*7,left,top,101+profile,full,false);
    f.u=plane((backingWidth+1)/2,(backingHeight+1)/2,profile%2?2:1,11+profile*3,left/2,top/2,211+profile,full,true);
    f.v=plane((backingWidth+1)/2,(backingHeight+1)/2,profile%2?2:1,17+profile*5,left/2,top/2,307+profile,full,true);
    return f;
}
std::vector<Warp> warps(const Frame& f) {
    const bool swapped=f.rotation==90||f.rotation==270;
    const double width=swapped?f.height:f.width,height=swapped?f.width:f.height;
    const double sx=width/512.,sy=height/256.;
    return {{"identity",{1,0,0,0,1,0,0,0,1}},
        {"tilted_affine",{sx*.985,-sx*.035,width*.025,sy*.025,sy*.99,height*.01,0,0,1}},
        {"projective",{sx*.94,sx*.02,width*.03,-sy*.015,sy*.98,height*.02,.00018,-.00012,1}},
        {"borders",{sx*1.4,0,-width*.18,0,sy*1.4,-height*.12,0,0,1}},
        {"zero_denominator",{sx,0,0,0,sy,0,0,0,0}}};
}
#define FRAME_VALUES(f) \
    (f).y.data(),(f).y.size(),(f).y.row,(f).y.step, \
    (f).u.data(),(f).u.size(),(f).u.row,(f).u.step, \
    (f).v.data(),(f).v.size(),(f).v.row,(f).v.step, \
    (f).width,(f).height,(f).rotation,(f).full
bool prepare(Preprocess function,const Frame& f,Buffers& out) {
    return function(FRAME_VALUES(f),out.detector.data(),out.detector.size(),out.gray.data(),out.gray.size(),out.transform.data(),out.transform.size())!=0;
}
bool pack(Pack function,const Frame& f,const Warp& warp,Buffers& out) {
    return function(FRAME_VALUES(f),warp.values.data(),warp.values.size(),out.packed.data(),out.packed.size())!=0;
}
bool canonicalReference(const Frame& f,const Buffers& out) {
    // Independent scalar reference for every output in the canonical identity
    // fixture; the remaining cases establish parity, not independent correctness.
    if(f.rotation!=0||f.width!=512||f.height!=256) return false;
    constexpr size_t area=128*256;
    for(int y=0;y<128;++y) for(int x=0;x<256;++x) {
        const size_t i=size_t(y)*256+x;
        for(int channel=0;channel<6;++channel) {
            const bool chroma=channel>=4;
            const Plane& p=channel==4?f.u:channel==5?f.v:f.y;
            const int px=chroma?x:2*x+(channel==2||channel==3);
            const int py=chroma?y:2*y+(channel==1||channel==3);
            const float value=p.data()[size_t(py)*p.row+size_t(px)*p.step];
            const float expected=f.full?(chroma?128+(value-128)*(224.f/255):16+value*(219.f/255)):value;
            if(out.packed[size_t(channel)*area+i]!=expected) return false;
        }
    }
    return true;
}
std::string samplesJson(const std::vector<double>& values) {
    std::ostringstream s;s<<std::setprecision(17)<<"[";
    for(size_t i=0;i<values.size();++i){if(i)s<<",";s<<values[i];}s<<"]";return s.str();
}
double median(std::vector<double> values) {
    if(values.empty())return 0;std::sort(values.begin(),values.end());
    const size_t n=values.size();return n%2?values[n/2]:(values[n/2-1]+values[n/2])/2;
}
std::string quote(const std::string& value) {
    std::ostringstream s;s<<'"';for(unsigned char c:value) {
        if(c=='"'||c=='\\')s<<'\\'<<c;
        else if(c<32)s<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c)<<std::dec;
        else s<<c;
    }s<<'"';return s.str();
}

int main(int argc,char** argv) {
    const auto started=Clock::now();
    Difference detectorDifference,grayDifference,transformDifference,packDifference;
    size_t detectorCases=0,packCases=0,canonicalChecks=0;
    bool complete=true,wrappersSucceeded=true,canonicalPassed=true;
    std::string error;
    std::vector<std::string> cases;
    std::vector<double> detectorO0,detectorO2,packO0,packO2;
    size_t timingBlocks=0;
    try {
        Buffers baseline,candidate;
        std::vector<Frame> fixtures;
        for(int full=0;full<=1;++full)for(int r=0;r<4;++r)fixtures.push_back(fixture(r*90,full,r));
        for(const auto& f:fixtures) {
            if(!withinBudget(started)){complete=false;break;}
            if(!prepare(ld_probe_preprocess,f,baseline)||!prepare(ld_candidate_probe_preprocess,f,candidate)) {
                wrappersSucceeded=false;error="preprocessing wrapper rejected fixture";break;
            }
            ++detectorCases;
            detectorDifference.floats(baseline.detector.data(),candidate.detector.data(),detectorCount);
            grayDifference.bytes(baseline.gray.data(),candidate.gray.data(),grayCount);
            transformDifference.doubles(baseline.transform.data(),candidate.transform.data(),6);
            for(const auto& warp:warps(f)) {
                if(!withinBudget(started)){complete=false;break;}
                if(!pack(ld_probe_pack,f,warp,baseline)||!pack(ld_candidate_probe_pack,f,warp,candidate)) {
                    wrappersSucceeded=false;error="packing wrapper rejected fixture";break;
                }
                ++packCases;Difference local;
                local.floats(baseline.packed.data(),candidate.packed.data(),packCount);
                packDifference.floats(baseline.packed.data(),candidate.packed.data(),packCount);
                cases.push_back("{\"fixture\":"+quote(f.name)+",\"warp\":"+quote(warp.name)+",\"difference\":"+local.json()+"}");
                if(warp.name=="identity"&&f.rotation==0) {
                    ++canonicalChecks;
                    canonicalPassed=canonicalReference(f,baseline)&&canonicalReference(f,candidate)&&canonicalPassed;
                }
            }
            if(!complete||!wrappersSucceeded)break;
        }
        // Fixed work in ABBA order, after parity has warmed both implementations.
        // Each segment is one call. Includes wrapper checks and the gray copy;
        // excludes model inference and cannot establish app heat/power savings.
        if(complete&&wrappersSucceeded) {
            const auto& f=fixtures[5];const auto warp=warps(f)[2];
            constexpr int order[]{0,1,1,0};
            for(size_t block=0;block<4;++block) {
                bool blockComplete=true;
                for(int variant:order) {
                    if(!withinBudget(started)){complete=false;blockComplete=false;break;}
                    Buffers& out=variant?candidate:baseline;
                    auto begin=Clock::now();
                    if(!prepare(variant?ld_candidate_probe_preprocess:ld_probe_preprocess,f,out))throw std::runtime_error("timed preprocessing failed");
                    (variant?detectorO2:detectorO0).push_back(elapsed(begin));
                    begin=Clock::now();
                    if(!pack(variant?ld_candidate_probe_pack:ld_probe_pack,f,warp,out))throw std::runtime_error("timed packing failed");
                    (variant?packO2:packO0).push_back(elapsed(begin));
                }
                if(!blockComplete)break;
                ++timingBlocks;
            }
        }
    }catch(const std::exception& e){complete=false;error=e.what();}
    catch(...){complete=false;error="unknown probe failure";}
    const bool parityComplete=detectorCases==8&&packCases==40&&canonicalChecks==2;
    const bool numericalQualified=parityComplete&&wrappersSucceeded&&canonicalPassed&&detectorDifference.exact()&&
        grayDifference.exact()&&transformDifference.exact()&&packDifference.exact();
    const bool timingComplete=timingBlocks==4;
    complete=complete&&parityComplete&&timingComplete;
    std::ostringstream report;report<<std::setprecision(17)
        <<"{\"schema\":1,\"probe\":\"same_process_native_preprocessing_O0_vs_O2\""
        <<",\"complete\":"<<(complete?"true":"false")
        <<",\"numerical_qualified\":"<<(numericalQualified?"true":"false")
        <<",\"exact_fixture_parity_only\":true,\"wrappers_succeeded\":"<<(wrappersSucceeded?"true":"false")
        <<",\"wall_ms\":"<<elapsed(started)<<",\"admission_budget_ms\":"<<wallBudgetMs
        <<",\"budget_note\":\"Stops admitting cases at budget; a running function call is not interrupted\""
        <<",\"detector_cases\":"<<detectorCases<<",\"pack_cases\":"<<packCases
        <<",\"canonical_reference_checks\":"<<canonicalChecks<<",\"canonical_reference_passed\":"<<(canonicalPassed?"true":"false")
        <<",\"detector_nchw\":"<<detectorDifference.json()<<",\"gray\":"<<grayDifference.json()
        <<",\"transform\":"<<transformDifference.json()<<",\"supercombo_packed\":"<<packDifference.json()
        <<",\"timing\":{\"order\":\"ABBA\",\"completed_blocks\":"<<timingBlocks
        <<",\"fixture\":\"rot90_full_641x479\",\"warp\":\"projective\""
        <<",\"scope\":\"Pure functions plus identical wrapper checks and gray output copy; no ORT/JNI/camera. Not a heat or power measurement.\""
        <<",\"detector_O0_ms\":"<<samplesJson(detectorO0)<<",\"detector_O2_ms\":"<<samplesJson(detectorO2)
        <<",\"pack_O0_ms\":"<<samplesJson(packO0)<<",\"pack_O2_ms\":"<<samplesJson(packO2)
        <<",\"detector_O0_median_ms\":"<<median(detectorO0)<<",\"detector_O2_median_ms\":"<<median(detectorO2)
        <<",\"pack_O0_median_ms\":"<<median(packO0)<<",\"pack_O2_median_ms\":"<<median(packO2)<<"}"
        <<",\"pack_case_results\":[";
    for(size_t i=0;i<cases.size();++i){if(i)report<<",";report<<cases[i];}
    report<<"],\"error\":"<<(error.empty()?"null":quote(error))<<"}";
    const auto json=report.str();
    bool saved=true;
    if(argc>1){std::ofstream output(argv[1]);output<<json<<'\n';saved=bool(output);}
    std::cout<<json<<'\n';
    if(!saved)return 4;
    if(!complete||!wrappersSucceeded)return 3;
    return numericalQualified?0:2;
}
