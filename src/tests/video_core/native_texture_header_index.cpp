// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/textures/texture.h"

TEST_CASE("Legacy texture indices never decode as native header offsets",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    REQUIRE(TextureHeaderIndex(64, 63, false) == 64);
    REQUIRE(TextureHeaderIndex(2016, 63, false) == 2016);
    REQUIRE(TextureHeaderIndex(32, 0, false) == 32);
}

TEST_CASE("Native header offsets decode only when the shifted index is valid",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    REQUIRE(TextureHeaderIndex(64, 63, true) == 2);
    REQUIRE(TextureHeaderIndex(2016, 63, true) == 63);
    REQUIRE(TextureHeaderIndex(2048, 63, true) == 2048);
    REQUIRE(TextureHeaderIndex(65, 63, true) == 65);
    REQUIRE(TextureHeaderIndex(32, 0, true) == 32);
}

TEST_CASE("Resolved texture indices stay unchanged for both memory layouts",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    constexpr u32 maximum = std::numeric_limits<u32>::max();
    for (const bool native_tiling : {false, true}) {
        REQUIRE(TextureHeaderIndex(0, 0, native_tiling) == 0);
        REQUIRE(TextureHeaderIndex(32, 63, native_tiling) == 32);
        REQUIRE(TextureHeaderIndex(64, 64, native_tiling) == 64);
        REQUIRE(TextureHeaderIndex(maximum, maximum, native_tiling) == maximum);
        REQUIRE(TextureHeaderIndex(maximum, 63, native_tiling) == maximum);
    }
}
