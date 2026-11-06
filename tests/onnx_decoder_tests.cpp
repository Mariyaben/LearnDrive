#include "learndrive/OnnxLanguageModel.hpp"
#include <iostream>
#include <stdexcept>
using namespace ld::cognition;
// Test-only tokenizer aligns a one-token prefill with the synthetic graph's
// byte vocabulary. Production always uses the model's tokenizer.json.
struct FixtureTokenizer final : Tokenizer {
    std::vector<int32_t> encode(const std::string& text) override {
        if (text.find("<|system|>") == std::string::npos || text.find("<|assistant|>") == std::string::npos)
            throw std::runtime_error("Phi-3 chat template missing");
        return {1};
    }
    std::string decode(const std::vector<int32_t>& tokens) override { return std::string(tokens.begin(), tokens.end()); }
};
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected synthetic decoder path");
        for (const std::string& suffix : {std::string{}, std::string{".fp16.onnx"}}) {
            OnnxModelOptions options; options.modelPath = std::string(argv[1]) + suffix;
            options.eosTokens = {0}; options.maxInputTokens = 8; options.maxNewTokens = 96;
            OnnxLanguageModel model(options, std::make_unique<FixtureTokenizer>());
            std::atomic<uint64_t> epoch{1}; std::atomic<bool> stopping{false};
            Cancellation cancel{epoch, stopping, 1, SteadyClock::now() + std::chrono::seconds(5)};
            ContextualDecision result;
            const auto response = model.generate("test system", "{}", cancel);
            if (!parseResponse(response, result) || result.action != Action::holdLaneAndDistance)
                throw std::runtime_error("causal decoding failed: " + response);
            ++epoch;
            if (!model.generate("test system", "{}", cancel).empty()) throw std::runtime_error("cancelled inference produced response");
            std::cout << "PASS real ORT " << (suffix.empty() ? "FP32" : "FP16") << " prefill, KV decode, JSON and cancellation\n";
        }
        OnnxModelOptions invalid; invalid.modelPath = argv[1]; invalid.provider = ExecutionProvider::nnapi;
#ifndef __ANDROID__
        bool rejected = false;
        try { OnnxLanguageModel model(invalid, std::make_unique<FixtureTokenizer>()); } catch (const std::exception&) { rejected = true; }
        if (!rejected) throw std::runtime_error("unsupported provider silently accepted");
        invalid.allowCpuFallback = true;
        OnnxLanguageModel fallback(invalid, std::make_unique<FixtureTokenizer>());
        if (fallback.selectedProvider() != "CPU" || fallback.fallbackReason().empty()) throw std::runtime_error("provider fallback diagnostics missing");
        std::cout << "PASS explicit provider fallback and diagnostics\n";
#endif
        invalid.provider = ExecutionProvider::cpu; invalid.maxCacheBytes = 1;
        bool bounded = false;
        try { OnnxLanguageModel model(invalid, std::make_unique<FixtureTokenizer>()); } catch (const std::exception&) { bounded = true; }
        if (!bounded) throw std::runtime_error("KV memory bound ignored");
        std::cout << "PASS KV memory bound\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; return 1; }
}
