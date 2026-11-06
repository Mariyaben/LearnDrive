#include "learndrive/PostDriveSummary.hpp"
#include <stdexcept>
namespace ld::cognition {
std::future<std::string> PostDriveSummary::start(ContextualCognitionEngine::ModelFactory factory,std::string telemetry) {
    if(telemetry.size()>32768)throw std::invalid_argument("post-drive telemetry exceeds 32 KiB");
    return std::async(std::launch::async,[factory=std::move(factory),telemetry=std::move(telemetry)] {
        auto model=factory();std::atomic<uint64_t> epoch{0};std::atomic<bool> stopping{false};
        const Cancellation cancellation{epoch,stopping,0,SteadyClock::now()+std::chrono::seconds(60)};
        return model->generate("Summarize this completed drive. Describe only observed events and uncertainty. "
            "Do not issue live driving commands or infer unmeasured events.",telemetry,cancellation);
    });
}
} // namespace ld::cognition
