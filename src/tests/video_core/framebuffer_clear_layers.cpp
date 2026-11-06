// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/framebuffer_clear_layers.h"

TEST_CASE("Framebuffer clears stay within the smallest attachment",
          "[video_core][framebuffer_layers]") {
    const auto clear = Vulkan::GetFramebufferClearLayers(0, 4, 1);
    REQUIRE(clear.count == 1);
    REQUIRE(clear.covers_all_layers);
}

TEST_CASE("Framebuffer clears skip empty and nonoverlapping layer ranges",
          "[video_core][framebuffer_layers]") {
    for (const u32 base : {0U, 1U, 4U, (std::numeric_limits<u32>::max)()}) {
        const auto empty = Vulkan::GetFramebufferClearLayers(base, 0, 1);
        REQUIRE(empty.count == 0);
        REQUIRE_FALSE(empty.covers_all_layers);
        if (base != 0) {
            const auto outside = Vulkan::GetFramebufferClearLayers(base, 4, 1);
            REQUIRE(outside.count == 0);
            REQUIRE_FALSE(outside.covers_all_layers);
        }
    }
}

TEST_CASE("Framebuffer clear clipping preserves the base and avoids overflow",
          "[video_core][framebuffer_layers]") {
    const auto tail = Vulkan::GetFramebufferClearLayers(2, 4, 4);
    REQUIRE(tail.count == 2);
    REQUIRE_FALSE(tail.covers_all_layers);

    const auto huge = Vulkan::GetFramebufferClearLayers(1, (std::numeric_limits<u32>::max)(), 4);
    REQUIRE(huge.count == 3);
    REQUIRE_FALSE(huge.covers_all_layers);
}

TEST_CASE("Only clears covering every framebuffer layer can become load-op clears",
          "[video_core][framebuffer_layers]") {
    const auto partial = Vulkan::GetFramebufferClearLayers(0, 1, 4);
    REQUIRE(partial.count == 1);
    REQUIRE_FALSE(partial.covers_all_layers);

    const auto full = Vulkan::GetFramebufferClearLayers(0, 4, 4);
    REQUIRE(full.count == 4);
    REQUIRE(full.covers_all_layers);

    const auto single = Vulkan::GetFramebufferClearLayers(0, 1, 1);
    REQUIRE(single.count == 1);
    REQUIRE(single.covers_all_layers);
}
