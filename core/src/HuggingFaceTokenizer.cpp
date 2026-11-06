#include "learndrive/OnnxLanguageModel.hpp"
#ifdef LEARNDRIVE_WITH_HF_TOKENIZER
#include <tokenizers_cpp.h>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace ld::cognition {
namespace {
class HuggingFaceTokenizer final : public Tokenizer {
public:
    explicit HuggingFaceTokenizer(const std::string& path) {
        std::ifstream stream(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("cannot open tokenizer.json");
        const auto size = stream.tellg();
        if (size <= 0 || size > 64 * 1024 * 1024) throw std::runtime_error("invalid tokenizer.json size");
        std::string json(static_cast<size_t>(size), '\0');
        stream.seekg(0);
        if (!stream.read(json.data(), size)) throw std::runtime_error("cannot read tokenizer.json");
        tokenizer_ = tokenizers::Tokenizer::FromBlobJSON(json);
        if (!tokenizer_) throw std::runtime_error("invalid Hugging Face tokenizer");
    }
    std::vector<int32_t> encode(const std::string& text) override { return tokenizer_->Encode(text); }
    std::string decode(const std::vector<int32_t>& tokens) override { return tokenizer_->Decode(tokens); }
private:
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;
};
}
std::unique_ptr<Tokenizer> makeHuggingFaceTokenizer(const std::string& path) { return std::make_unique<HuggingFaceTokenizer>(path); }
} // namespace ld::cognition
#endif
