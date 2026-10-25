#pragma once
#include "ContextualCognitionEngine.hpp"
#include <vector>

namespace ld::cognition {
class Tokenizer {
public:
    virtual ~Tokenizer() = default;
    virtual std::vector<int32_t> encode(const std::string& text) = 0;
    virtual std::string decode(const std::vector<int32_t>& tokens) = 0;
};
enum class ExecutionProvider { cpu, coreML, nnapi };
enum class ChatTemplate { phi3, chatML };
struct OnnxModelOptions {
    std::string modelPath;
    ExecutionProvider provider = ExecutionProvider::cpu;
    bool allowCpuFallback = false;
    std::string coreMLCacheDirectory; // Version/hash-specific, writable directory.
    ChatTemplate chatTemplate = ChatTemplate::phi3;
    std::vector<int32_t> eosTokens{32000, 32001, 32007};
    size_t contextLength = 4096;
    size_t maxInputTokens = 512;
    size_t maxNewTokens = 96;
    size_t maxCacheBytes = 1024ULL * 1024 * 1024;
    int intraOpThreads = 1; // Leave compute capacity for vision/reflex/audio.
};
// ORT >= 1.22 C++ causal decoder with greedy autoregressive generation and KV
// cache. A compatible graph and its exact tokenizer are required, not just a
// filename. Construction must happen in ContextualCognitionEngine's factory.
class OnnxLanguageModel final : public LanguageModel {
public:
    OnnxLanguageModel(OnnxModelOptions options, std::unique_ptr<Tokenizer> tokenizer);
    ~OnnxLanguageModel() override;
    std::string generate(const std::string&, const std::string&, const Cancellation&) override;
    const std::string& selectedProvider() const noexcept;
    const std::string& fallbackReason() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Built with LEARNDRIVE_WITH_HF_TOKENIZER; tokenizers-cpp reads the model's
// tokenizer.json including special tokens. No whitespace/byte tokenizer fallback.
std::unique_ptr<Tokenizer> makeHuggingFaceTokenizer(const std::string& tokenizerPath);
} // namespace ld::cognition
