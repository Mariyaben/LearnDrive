#pragma once
#include "ContextualCognitionEngine.hpp"
#include <future>

namespace ld::cognition {
// Explicitly invoked AFTER capture/rules stop. The returned text has no audio
// callback, Action parser or reference to a driving engine.
class PostDriveSummary {
public:
    static std::future<std::string> start(ContextualCognitionEngine::ModelFactory factory,std::string telemetryJson);
};
} // namespace ld::cognition
