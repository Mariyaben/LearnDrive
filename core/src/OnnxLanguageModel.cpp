#include "learndrive/OnnxLanguageModel.hpp"
#ifdef LEARNDRIVE_WITH_ORT
#include <onnxruntime_cxx_api.h>
#ifdef __ANDROID__
#include <nnapi_provider_factory.h>
#endif
#include <filesystem>
#include <numeric>
#include <stdexcept>

namespace ld::cognition {
namespace {
// SetTerminate interrupts an in-flight Run as well as the token loop. Some EP
// kernels cannot be interrupted until they return; late results are discarded.
class RunWatchdog {
public:
    RunWatchdog(Ort::RunOptions& options, const Cancellation& cancel)
        : thread_([this, &options, &cancel] {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!done_) {
                if (cancel.cancelled()) { try { options.SetTerminate(); } catch (...) {} return; }
                wake_.wait_for(lock, std::chrono::milliseconds(2), [&] { return done_; });
            }
        }) {}
    ~RunWatchdog() {
        { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
        wake_.notify_one(); thread_.join();
    }
private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_{};
    std::thread thread_;
};
bool isFloat(ONNXTensorElementDataType t) { return t == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || t == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16; }
bool isInteger(ONNXTensorElementDataType t) { return t == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 || t == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64; }
}
struct OnnxLanguageModel::Impl {
    struct Input {
        std::string name;
        ONNXTensorElementDataType type;
        std::vector<int64_t> shape;
        int cacheOutput = -1;
    };
    OnnxModelOptions config;
    std::unique_ptr<Tokenizer> tokenizer;
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "learndrive-cognition"};
    Ort::Session session{nullptr};
    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<Input> inputs;
    std::vector<std::string> outputNames;
    std::vector<const char*> inputPointers, outputPointers;
    size_t logitsIndex{};
    std::string selected = "CPU", fallback;

    Ort::SessionOptions options(ExecutionProvider provider) {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(config.intraOpThreads);
        opts.SetInterOpNumThreads(1);
        opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.AddConfigEntry("session.intra_op.allow_spinning", "0");
        opts.AddConfigEntry("session.inter_op.allow_spinning", "0");
        // Token lengths vary each decode step; do not retain a prefill memory pattern.
        opts.DisableMemPattern();
        if (provider == ExecutionProvider::coreML) {
            const auto providers = Ort::GetAvailableProviders();
            if (std::find(providers.begin(), providers.end(), "CoreMLExecutionProvider") == providers.end())
                throw std::runtime_error("ORT binary has no CoreML execution provider");
            std::unordered_map<std::string, std::string> settings{
                {"ModelFormat", "MLProgram"}, {"MLComputeUnits", "ALL"},
                {"RequireStaticInputShapes", "0"}, {"EnableOnSubgraphs", "0"}};
            if (!config.coreMLCacheDirectory.empty()) settings["ModelCacheDirectory"] = config.coreMLCacheDirectory;
            opts.AppendExecutionProvider("CoreML", settings);
            selected = "CoreML + CPU fallback for unsupported operators";
        } else if (provider == ExecutionProvider::nnapi) {
#ifdef __ANDROID__
            // Optional legacy Android delegate; INT4 decoder ops often remain on
            // ORT CPU. LiteRT is a different runtime, not an ORT provider.
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Nnapi(opts, NNAPI_FLAG_CPU_DISABLED));
            selected = "NNAPI + CPU fallback for unsupported operators";
#else
            throw std::runtime_error("NNAPI is only available in Android ORT builds");
#endif
        }
        return opts;
    }
    Impl(OnnxModelOptions c, std::unique_ptr<Tokenizer> t) : config(std::move(c)), tokenizer(std::move(t)) {
        if (!tokenizer || config.modelPath.empty() || config.contextLength < 2 || config.contextLength > 32768 ||
            config.maxInputTokens == 0 || config.maxInputTokens >= config.contextLength || config.maxNewTokens == 0 ||
            config.maxNewTokens > config.contextLength - config.maxInputTokens || config.intraOpThreads < 1 ||
            config.intraOpThreads > 4 || config.eosTokens.empty()) throw std::invalid_argument("invalid ONNX decoder configuration");
        const std::filesystem::path path = std::filesystem::u8path(config.modelPath);
        try {
            auto opts = options(config.provider);
            session = Ort::Session(environment, path.c_str(), opts);
        } catch (const std::exception& ex) {
            if (!config.allowCpuFallback || config.provider == ExecutionProvider::cpu) throw;
            fallback = ex.what(); selected = "CPU";
            auto opts = options(ExecutionProvider::cpu);
            session = Ort::Session(environment, path.c_str(), opts);
        }
        bool logits = false, ids = false, mask = false;
        for (size_t i = 0; i < session.GetOutputCount(); ++i) {
            outputNames.emplace_back(session.GetOutputNameAllocated(i, allocator).get());
            if (outputNames.back() == "logits") {
                logits = true; logitsIndex = i;
                const auto type = session.GetOutputTypeInfo(i);
                const auto info = type.GetTensorTypeAndShapeInfo();
                if (!isFloat(info.GetElementType()) || info.GetShape().size() != 3)
                    throw std::runtime_error("expected float/float16 logits [1,sequence,vocabulary]");
            }
        }
        size_t cacheBytesPerToken = 0, caches = 0;
        for (size_t i = 0; i < session.GetInputCount(); ++i) {
            const auto type = session.GetInputTypeInfo(i);
            const auto info = type.GetTensorTypeAndShapeInfo();
            Input input{session.GetInputNameAllocated(i, allocator).get(), info.GetElementType(), info.GetShape(), -1};
            if (input.name == "input_ids" || input.name == "attention_mask" || input.name == "position_ids") {
                if (!isInteger(input.type) || input.shape.size() != 2 || (input.shape[0] != 1 && input.shape[0] != -1))
                    throw std::runtime_error("expected int32/int64 token input [1,sequence]");
                ids |= input.name == "input_ids"; mask |= input.name == "attention_mask";
            } else if (input.name.rfind("past_key_values.", 0) == 0) {
                if (!isFloat(input.type) || input.shape.size() != 4 || (input.shape[0] != 1 && input.shape[0] != -1) ||
                    input.shape[1] <= 0 || input.shape[1] > 256 || input.shape[3] <= 0 || input.shape[3] > 512 || input.shape[2] >= 0)
                    throw std::runtime_error("expected dynamic KV cache [1,heads,past_length,head_dim]");
                std::string present = "present." + input.name.substr(16);
                auto found = std::find(outputNames.begin(), outputNames.end(), present);
                if (found == outputNames.end()) throw std::runtime_error("decoder is missing present KV output: " + present);
                input.cacheOutput = static_cast<int>(found - outputNames.begin());
                const auto outputType = session.GetOutputTypeInfo(input.cacheOutput);
                const auto outputInfo = outputType.GetTensorTypeAndShapeInfo();
                if (outputInfo.GetElementType() != input.type || outputInfo.GetShape().size() != 4)
                    throw std::runtime_error("KV input/output types do not match");
                cacheBytesPerToken += size_t(input.shape[1] * input.shape[3]) * (input.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ? 4 : 2);
                ++caches;
            } else throw std::runtime_error("unsupported decoder input: " + input.name);
            inputs.push_back(std::move(input));
        }
        if (!ids || !mask || !logits || caches == 0 || caches > 256 || caches % 2 || outputNames.size() != caches + 1)
            throw std::runtime_error("expected causal decoder with input_ids, attention_mask, logits and key/value caches");
        // Both past and present buffers may coexist during Run.
        if (cacheBytesPerToken > config.maxCacheBytes / 2 / (config.maxInputTokens + config.maxNewTokens))
            throw std::runtime_error("decoder KV buffers exceed configured memory budget");
        for (const auto& input : inputs) inputPointers.push_back(input.name.c_str());
        for (const auto& name : outputNames) outputPointers.push_back(name.c_str());
    }
    Ort::Value integerTensor(const Input& input, const std::vector<int64_t>& values) {
        const int64_t shape[]{1, static_cast<int64_t>(values.size())};
        auto value = Ort::Value::CreateTensor(allocator, shape, 2, input.type);
        if (input.type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
            std::copy(values.begin(), values.end(), value.GetTensorMutableData<int64_t>());
        else {
            auto* data = value.GetTensorMutableData<int32_t>();
            std::transform(values.begin(), values.end(), data, [](int64_t v) { return static_cast<int32_t>(v); });
        }
        return value;
    }
    std::string generate(const std::string& system, const std::string& scene, const Cancellation& cancel) {
        if (cancel.cancelled()) return {};
        const std::string prompt = config.chatTemplate == ChatTemplate::phi3
            ? "<|system|>\n" + system + "<|end|>\n<|user|>\n" + scene + "<|end|>\n<|assistant|>\n"
            : "<|im_start|>system\n" + system + "<|im_end|>\n<|im_start|>user\n" + scene + "<|im_end|>\n<|im_start|>assistant\n";
        const auto encoded = tokenizer->encode(prompt);
        if (encoded.empty() || encoded.size() > config.maxInputTokens ||
            std::any_of(encoded.begin(), encoded.end(), [](int32_t id) { return id < 0; }))
            throw std::runtime_error("scene prompt exceeds input budget or tokenizer returned invalid IDs");
        std::vector<int64_t> tokens(encoded.begin(), encoded.end());
        std::vector<int32_t> generated;
        std::vector<Ort::Value> cache;
        cache.reserve(inputs.size());
        for (const auto& input : inputs) {
            if (input.cacheOutput < 0) cache.emplace_back(nullptr);
            else {
                const int64_t shape[]{1, input.shape[1], 0, input.shape[3]};
                cache.push_back(Ort::Value::CreateTensor(allocator, shape, 4, input.type));
            }
        }
        Ort::RunOptions run;
        RunWatchdog watchdog(run, cancel);
        size_t pastLength = 0;
        for (size_t step = 0; step < config.maxNewTokens && !cancel.cancelled(); ++step) {
            std::vector<Ort::Value> values;
            values.reserve(inputs.size());
            std::vector<int64_t> mask(pastLength + tokens.size(), 1), positions(tokens.size());
            std::iota(positions.begin(), positions.end(), static_cast<int64_t>(pastLength));
            for (size_t i = 0; i < inputs.size(); ++i) {
                const auto& input = inputs[i];
                if (input.cacheOutput >= 0) values.push_back(std::move(cache[i]));
                else values.push_back(integerTensor(input, input.name == "input_ids" ? tokens : input.name == "position_ids" ? positions : mask));
            }
            auto outputs = session.Run(run, inputPointers.data(), values.data(), values.size(), outputPointers.data(), outputPointers.size());
            if (cancel.cancelled()) return {};
            const auto info = outputs[logitsIndex].GetTensorTypeAndShapeInfo();
            const auto shape = info.GetShape();
            if (shape.size() != 3 || shape[0] != 1 || shape[1] < 1 || shape[2] < 1 || shape[2] > 1000000)
                throw std::runtime_error("invalid runtime logits dimensions");
            const size_t vocab = static_cast<size_t>(shape[2]), offset = (static_cast<size_t>(shape[1]) - 1) * vocab;
            const bool fp32 = info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
            const float* logits = fp32 ? outputs[logitsIndex].GetTensorData<float>() : nullptr;
            const Ort::Float16_t* half = fp32 ? nullptr : outputs[logitsIndex].GetTensorData<Ort::Float16_t>();
            float best = -std::numeric_limits<float>::infinity();
            int32_t next = -1;
            for (size_t i = 0; i < vocab; ++i) {
                const float score = fp32 ? logits[offset + i] : half[offset + i].ToFloat();
                if (std::isfinite(score) && score > best) { best = score; next = static_cast<int32_t>(i); }
            }
            if (next < 0) throw std::runtime_error("decoder returned no finite logits");
            if (std::find(config.eosTokens.begin(), config.eosTokens.end(), next) != config.eosTokens.end())
                return tokenizer->decode(generated);
            generated.push_back(next);
            const auto text = tokenizer->decode(generated);
            if (text.size() > 256) return {}; // Bounded output, never truncate into a valid command.
            ContextualDecision complete;
            if (parseResponse(text, complete)) return text;
            pastLength += tokens.size();
            for (size_t i = 0; i < inputs.size(); ++i) if (inputs[i].cacheOutput >= 0) {
                auto& present = outputs[inputs[i].cacheOutput];
                const auto dims = present.GetTensorTypeAndShapeInfo().GetShape();
                if (dims != std::vector<int64_t>{1, inputs[i].shape[1], static_cast<int64_t>(pastLength), inputs[i].shape[3]})
                    throw std::runtime_error("runtime KV cache shape mismatch");
                cache[i] = std::move(present);
            }
            tokens.assign(1, next);
        }
        return {}; // Cancellation or truncated generation is never actionable.
    }
};
OnnxLanguageModel::OnnxLanguageModel(OnnxModelOptions options, std::unique_ptr<Tokenizer> tokenizer)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(tokenizer))) {}
OnnxLanguageModel::~OnnxLanguageModel() = default;
std::string OnnxLanguageModel::generate(const std::string& system, const std::string& scene, const Cancellation& cancel) {
    return impl_->generate(system, scene, cancel);
}
const std::string& OnnxLanguageModel::selectedProvider() const noexcept { return impl_->selected; }
const std::string& OnnxLanguageModel::fallbackReason() const noexcept { return impl_->fallback; }
} // namespace ld::cognition
#endif // LEARNDRIVE_WITH_ORT
