// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>

#include "common/common_types.h"

namespace VideoCommon::RenderTrace {

inline bool Enabled() {
    static const bool enabled = std::getenv("EDEN_NX2_RENDER_TRACE") != nullptr;
    return enabled;
}

inline bool SampleFrame(u64 frame) {
    return Enabled() && frame % 600 == 0;
}

} // namespace VideoCommon::RenderTrace
