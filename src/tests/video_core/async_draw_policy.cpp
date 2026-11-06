// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/async_draw_policy.h"

TEST_CASE("Native pending draws are never discarded", "[video_core][async_draw]") {
    constexpr std::array<u32, 6> counts{0, 1, 6, 7, 4096, std::numeric_limits<u32>::max()};
    for (const bool depth : {false, true}) {
        for (const u32 indices : counts) {
            for (const u32 vertices : counts) {
                REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(true, depth, indices, vertices));
            }
        }
    }
}

TEST_CASE("Legacy pending draws retain the async heuristic", "[video_core][async_draw]") {
    REQUIRE(Vulkan::CanDiscardPendingDraw(false, true, 0, 0));
    REQUIRE(Vulkan::CanDiscardPendingDraw(false, false, 7, 7));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 6, 100));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 100, 6));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 0, 0));
}
