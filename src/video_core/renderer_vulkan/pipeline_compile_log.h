// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>

#include "common/logging.h"

namespace Vulkan {

// EDEN_NX2_LOG_PIPELINE_COMPILES=1 logs every driver pipeline compile (vkCreate*Pipelines) when it
// starts and when it returns. A driver whose compiler hangs leaves a "start" line with no matching
// "done" line, naming the shaders it hangs on (Ocarina of Time froze at its title screen on Radeon
// RX 6000 cards with AMD's Windows driver, with no device loss: the GPU thread waited on a compile).
[[nodiscard]] inline bool LogPipelineCompiles() {
    static const bool enabled = [] {
        const char* const value = std::getenv("EDEN_NX2_LOG_PIPELINE_COMPILES");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

class PipelineCompileLog {
public:
    PipelineCompileLog(const char* kind_, std::string shaders_)
        : enabled{LogPipelineCompiles()}, kind{kind_}, shaders{std::move(shaders_)} {
        if (enabled) {
            begin = std::chrono::steady_clock::now();
            LOG_INFO(Render_Vulkan, "Driver compile start: {} {}", kind, shaders);
        }
    }

    ~PipelineCompileLog() {
        if (enabled) {
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - begin)
                                  .count();
            LOG_INFO(Render_Vulkan, "Driver compile done: {} {} in {:.1f} ms", kind, shaders, ms);
        }
    }

    PipelineCompileLog(const PipelineCompileLog&) = delete;
    PipelineCompileLog& operator=(const PipelineCompileLog&) = delete;

private:
    bool enabled;
    const char* kind;
    std::string shaders;
    std::chrono::steady_clock::time_point begin{};
};

} // namespace Vulkan
