#include "learndrive/perception/supercombo.hpp"
#include <onnxruntime_cxx_api.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>
#include <stdexcept>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sched.h>
#include <fstream>
#endif

namespace ld::perception {
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
bool bigCores() {
#if defined(__ANDROID__)
    std::vector<std::pair<long,int>> cores;
    cpu_set_t allowed;CPU_ZERO(&allowed);
    if(sched_getaffinity(0,sizeof(allowed),&allowed)!=0)return false;
    for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&allowed)) {
        long hz=0;std::ifstream f("/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/cpuinfo_max_freq");
        f>>hz;if(hz>0)cores.emplace_back(hz,i);
    }
    if(cores.size()<2)return false;
    std::sort(cores.rbegin(),cores.rend());cpu_set_t selected;CPU_ZERO(&selected);
    CPU_SET(cores[0].second,&selected);CPU_SET(cores[1].second,&selected);
    return sched_setaffinity(0,sizeof(selected),&selected)==0;
#else
    return false;
#endif
}
}
struct SupercomboRuntime::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"LearnDriveSupercombo"};
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    std::array<float,supercomboInputSize> images{};
    std::array<float,8> desire{};
    std::array<float,2> traffic{0,1}; // India: left-hand traffic, explicit platform setting below.
    std::array<float,512> state{};
    std::array<float,supercomboOutputSize> output{};
    std::vector<Ort::Value> inputs;
    Ort::Value result{nullptr};
    ProviderReport report;
    double lastTime=-infinity;
    const char* names[4]{"input_imgs","desire","traffic_convention","initial_state"};
    const char* outputName="outputs";
    Impl(const std::string& path,const SpatialRuntimeOptions& options) {
        report.bigCoreAffinity=bigCores();
        auto tensor=[&](float* ptr,size_t n,std::initializer_list<int64_t> dims) {
            return Ort::Value::CreateTensor<float>(memory,ptr,n,dims.begin(),dims.size());
        };
        inputs.emplace_back(tensor(images.data(),images.size(),{1,12,128,256}));
        inputs.emplace_back(tensor(desire.data(),8,{1,8}));
        inputs.emplace_back(tensor(traffic.data(),2,{1,2}));
        inputs.emplace_back(tensor(state.data(),512,{1,512}));
        result=tensor(output.data(),output.size(),{1,6472});
        const auto start=Clock::now();
        auto create=[&](const char* provider) {
            Ort::SessionOptions so;so.SetIntraOpNumThreads(2);so.SetInterOpNumThreads(1);
            so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            so.AddConfigEntry("session.intra_op.allow_spinning","0");
            so.AddConfigEntry("session.inter_op.allow_spinning","0");
            if(std::strcmp(provider,"XNNPACK")==0)
                so.AppendExecutionProvider("XNNPACK",{{"intra_op_num_threads","2"}});
            if(std::strcmp(provider,"QNN")==0) {
                so.AddConfigEntry("session.disable_cpu_ep_fallback","1");
                so.AppendExecutionProvider("QNN",{{"backend_path",options.qnnBackendPath},{"htp_performance_mode","balanced"}});
            }
            return std::make_unique<Ort::Session>(env,std::filesystem::path(path).c_str(),so);
        };
        try {session=create("XNNPACK");report.selected="XNNPACK";}
        catch(const std::exception& e){report.fallbackReason=std::string("XNNPACK: ")+e.what();session=create("CPU");}
        Ort::AllocatorWithDefaultOptions allocator;
        const std::array<std::vector<int64_t>,4> shapes{{{1,12,128,256},{1,8},{1,2},{1,512}}};
        if(session->GetInputCount()!=4||session->GetOutputCount()!=1)throw std::runtime_error("wrong Supercombo graph version");
        for(size_t i=0;i<4;++i) {
            auto type=session->GetInputTypeInfo(i);auto info=type.GetTensorTypeAndShapeInfo();
            if(session->GetInputNameAllocated(i,allocator).get()!=std::string(names[i])||
               info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||info.GetShape()!=shapes[i])
                throw std::runtime_error("Supercombo input contract mismatch");
        }
        auto outputType=session->GetOutputTypeInfo(0);auto outputInfo=outputType.GetTensorTypeAndShapeInfo();
        if(outputInfo.GetShape()!=std::vector<int64_t>{1,6472}||outputInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT||
           std::string(session->GetOutputNameAllocated(0,allocator).get())!="outputs")
            throw std::runtime_error("Supercombo output contract mismatch");
        report.initializationMs=ms(start);
        std::fill(images.begin(),images.end(),128.f);
        auto benchmark=[&](Ort::Session& s) {
            s.Run(Ort::RunOptions{nullptr},names,inputs.data(),4,&outputName,&result,1);
            auto begin=Clock::now();
            for(int i=0;i<3;++i)s.Run(Ort::RunOptions{nullptr},names,inputs.data(),4,&outputName,&result,1);
            return ms(begin)/3;
        };
        report.baselineMs=benchmark(*session);
        bool qnnPresent=false;
#if defined(__ANDROID__)
        if(options.profileQnn&&!options.qnnBackendPath.empty()) {
            void* lib=dlopen(options.qnnBackendPath.c_str(),RTLD_NOW|RTLD_LOCAL);
            qnnPresent=lib!=nullptr;if(lib)dlclose(lib);
        }
#endif
        if(qnnPresent) {
            report.qnnProbed=true;
            const auto reference=output;
            try {
                auto qnn=create("QNN");report.qnnMs=benchmark(*qnn);
                bool equivalent=true;
                for(size_t i=0;i<output.size();++i)
                    equivalent&=std::isfinite(output[i])&&std::abs(output[i]-reference[i])<=1e-3f*(1+std::abs(reference[i]));
                if(equivalent&&report.qnnMs<report.baselineMs*.9){session=std::move(qnn);report.selected="QNN HTP";}
                else report.fallbackReason+="; QNN rejected: numerical/latency gate";
            }catch(const std::exception& e){report.fallbackReason+=std::string("; QNN: ")+e.what();}
        }
        std::fill(images.begin(),images.end(),0);output.fill(0);
    }
};
SupercomboRuntime::SupercomboRuntime(const std::string& p,SpatialRuntimeOptions o):impl_(std::make_unique<Impl>(p,o)){}
SupercomboRuntime::~SupercomboRuntime()=default;
void SupercomboRuntime::reset() noexcept {impl_->images.fill(0);impl_->state.fill(0);impl_->lastTime=-infinity;}
SpatialOutput SupercomboRuntime::run(const YuvFrame& frame,const Matrix3& warp,double time) {
    if(!std::isfinite(time)||time<0||time<=impl_->lastTime)throw std::invalid_argument("Supercombo capture clock moved backwards");
    if(time-impl_->lastTime>.25)reset();
    std::memmove(impl_->images.data(),impl_->images.data()+supercomboFrameSize,supercomboFrameSize*sizeof(float));
    packSupercombo(frame,warp,impl_->images.data()+supercomboFrameSize,supercomboFrameSize);
    if(!std::isfinite(impl_->lastTime))std::copy_n(impl_->images.data()+supercomboFrameSize,supercomboFrameSize,impl_->images.data());
    try {
        impl_->session->Run(Ort::RunOptions{nullptr},impl_->names,impl_->inputs.data(),4,&impl_->outputName,&impl_->result,1);
        auto decoded=decodeSupercombo(impl_->output.data(),impl_->output.size());
        std::copy_n(impl_->output.data()+supercomboStateOffset,512,impl_->state.data());
        impl_->lastTime=time;
        return decoded;
    }catch(...){reset();throw;}
}
const ProviderReport& SupercomboRuntime::report() const noexcept {return impl_->report;}
const float* SupercomboRuntime::packedInput() const noexcept {return impl_->images.data();}
const float* SupercomboRuntime::rawOutput() const noexcept {return impl_->output.data();}
} // namespace ld::perception
