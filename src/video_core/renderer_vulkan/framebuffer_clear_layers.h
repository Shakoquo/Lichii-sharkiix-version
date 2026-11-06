// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "common/common_types.h"

namespace Vulkan {

struct FramebufferClearLayers {
    u32 count;
    bool covers_all_layers;
};

constexpr FramebufferClearLayers GetFramebufferClearLayers(u32 base_layer, u32 layer_count,
                                                           u32 framebuffer_layers) {
    if (base_layer >= framebuffer_layers || layer_count == 0) {
        return {};
    }
    const u32 count = (std::min)(layer_count, framebuffer_layers - base_layer);
    return {count, base_layer == 0 && count == framebuffer_layers};
}

} // namespace Vulkan
