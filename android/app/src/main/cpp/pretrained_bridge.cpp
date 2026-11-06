#include <jni.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#ifdef LEARNDRIVE_WITH_LLAMA
#include "llama.h"
#include "ggml-backend.h"
#endif
#define REASON(name) Java_ai_learndrive_mobile_PretrainedNative_##name
namespace {
struct State {
    std::atomic<bool> cancelled{false};
    std::chrono::steady_clock::time_point deadline{};
    int kind{};
    std::string error;
#ifdef LEARNDRIVE_WITH_LLAMA
    llama_model* model{};
    llama_context* context{};
    ~State(){if(context)llama_free(context);if(model)llama_model_free(model);}
#endif
};
State* state(jlong h){return reinterpret_cast<State*>(h);}
std::string utf(JNIEnv* env,jstring s){if(!s)return {};const char* p=env->GetStringUTFChars(s,nullptr);if(!p)return {};std::string r(p);env->ReleaseStringUTFChars(s,p);return r;}
#ifdef LEARNDRIVE_WITH_LLAMA
bool abortInference(void* p){auto& s=*static_cast<State*>(p);return s.cancelled.load()||std::chrono::steady_clock::now()>=s.deadline;}
bool loadProgress(float,void* p){return !static_cast<State*>(p)->cancelled.load();}
#endif
}
extern "C" {
JNIEXPORT jboolean JNICALL REASON(available)(JNIEnv*,jobject) {
#ifdef LEARNDRIVE_WITH_LLAMA
    return true;
#else
    return false;
#endif
}
JNIEXPORT jlong JNICALL REASON(create)(JNIEnv*,jobject){try{return reinterpret_cast<jlong>(new State);}catch(...){return 0;}}
JNIEXPORT void JNICALL REASON(cancel)(JNIEnv*,jobject,jlong h){if(h)state(h)->cancelled=true;}
JNIEXPORT void JNICALL REASON(close)(JNIEnv*,jobject,jlong h){delete state(h);}
JNIEXPORT jstring JNICALL REASON(error)(JNIEnv* e,jobject,jlong h){return e->NewStringUTF(h?state(h)->error.c_str():"Model unavailable");}
JNIEXPORT jboolean JNICALL REASON(load)(JNIEnv* e,jobject,jlong h,jstring path,jint kind) {
    if(!h)return false;auto& s=*state(h);s.kind=kind;
#ifdef LEARNDRIVE_WITH_LLAMA
    try {
        static std::once_flag initialized;
        std::call_once(initialized,[]{llama_backend_init();ggml_backend_load("libggml-cpu-android_armv8.0_1.so");});
        auto mp=llama_model_default_params();mp.n_gpu_layers=0;mp.load_mode=LLAMA_LOAD_MODE_MMAP;mp.progress_callback=loadProgress;mp.progress_callback_user_data=&s;
        s.model=llama_model_load_from_file(utf(e,path).c_str(),mp);if(!s.model){s.error="Pinned model could not load";return false;}
        auto cp=llama_context_default_params();cp.n_ctx=4096;cp.n_batch=512;cp.n_ubatch=128;cp.n_threads=2;cp.n_threads_batch=2;
        cp.abort_callback=abortInference;cp.abort_callback_data=&s;s.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        s.context=llama_init_from_model(s.model,cp);if(!s.context){s.error="Model context allocation failed";return false;}
        return !s.cancelled.load();
    }catch(...){s.error="Local model initialization failed";return false;}
#else
    s.error="No compatible provisioned native runtime for this ABI";return false;
#endif
}
JNIEXPORT jstring JNICALL REASON(choose)(JNIEnv* e,jobject,jlong h,jstring summary,jstring options,jint timeoutMs,jstring system) {
    if(!h)return e->NewStringUTF("");auto& s=*state(h);s.error.clear();
#ifdef LEARNDRIVE_WITH_LLAMA
    try {
        if(!s.context)return e->NewStringUTF("");
        s.cancelled=false; // Worker owns requests; source/revision checked again after return.
        const auto input=utf(e,summary),allowed=utf(e,options),systemPrompt=utf(e,system);
        if(input.size()>20000||systemPrompt.size()>2000||allowed.empty()||allowed.size()>10){s.error="Bounded model input rejected";return e->NewStringUTF("");}
        std::string grammar="root ::= ";
        for(size_t i=0;i<allowed.size();++i){if(std::string("SDABCEFGHI").find(allowed[i])==std::string::npos)return e->NewStringUTF("");if(i)grammar+=" | ";grammar+='"';grammar+=allowed[i];grammar+='"';}
        std::string prompt="<|im_start|>system\n"+systemPrompt+"<|im_end|>\n<|im_start|>user\n"+input+"<|im_end|>\n<|im_start|>assistant\n";
        if(s.kind==0)prompt+="<think>\n\n</think>\n\n";
        const auto* vocab=llama_model_get_vocab(s.model);
        std::vector<llama_token> tokens(4096);
        int n=llama_tokenize(vocab,prompt.data(),int(prompt.size()),tokens.data(),int(tokens.size()),true,true);
        if(n<=0||n>3800){s.error="Model context budget exceeded";return e->NewStringUTF("");}
        tokens.resize(size_t(n));llama_memory_clear(llama_get_memory(s.context),true);
        s.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::max(100,std::min(int(timeoutMs),10000)));
        for(int offset=0;offset<n;offset+=512){if(llama_decode(s.context,llama_batch_get_one(tokens.data()+offset,std::min(512,n-offset)))!=0){s.error="Inference aborted or failed";return e->NewStringUTF("");}}
        auto* sampler=llama_sampler_chain_init(llama_sampler_chain_default_params());
        auto* constrained=llama_sampler_init_grammar(vocab,grammar.c_str(),"root");
        if(!constrained){llama_sampler_free(sampler);s.error="Grammar rejected";return e->NewStringUTF("");}
        llama_sampler_chain_add(sampler,constrained);llama_sampler_chain_add(sampler,llama_sampler_init_greedy());
        std::string result;
        for(int i=0;i<4;++i) {
            auto token=llama_sampler_sample(sampler,s.context,-1);if(llama_vocab_is_eog(vocab,token))break;
            char buffer[64];int size=llama_token_to_piece(vocab,token,buffer,64,0,true);
            if(size>0&&size<=64)result.append(buffer,size);
            if(result.size()==1&&allowed.find(result[0])!=std::string::npos)break;
            if(llama_decode(s.context,llama_batch_get_one(&token,1))!=0)break;
        }
        llama_sampler_free(sampler);
        if(s.cancelled.load()||std::chrono::steady_clock::now()>=s.deadline||result.size()!=1||allowed.find(result[0])==std::string::npos){s.error="Expired or invalid local proposal";return e->NewStringUTF("");}
        return e->NewStringUTF(result.c_str());
    }catch(...){s.error="Local model inference failed";return e->NewStringUTF("");}
#else
    s.error="Runtime unavailable";return e->NewStringUTF("");
#endif
}
}
