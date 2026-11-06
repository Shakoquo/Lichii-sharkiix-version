// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_texture_cache.h"

TEST_CASE("Packed surface geometry preserves complete B32 accesses", "[packed_surface]") {
    const auto extent{Vulkan::PackedByteAddressedExtent(1920, 1088)};
    REQUIRE(extent);
    CHECK(extent->width == 960);
    CHECK(extent->height == 1088);
    CHECK(u64{extent->width} * extent->height * 4 == u64{1920} * 1088 * 2);
    for (const auto width : {0U, 1U, 3U, 17U, 1919U, 1921U}) {
        CHECK_FALSE(Vulkan::PackedByteAddressedExtent(width, 1088));
    }
    CHECK_FALSE(Vulkan::PackedByteAddressedExtent(1920, 0));
    REQUIRE(Vulkan::PackedByteAddressedExtent(2, 1));
    const auto narrow{Vulkan::PackedByteAddressedExtent(16, 1)};
    REQUIRE(narrow);
    for (const s32 byte_x : {-8, -4, 0, 4, 28, 32, 36}) {
        const s32 packed_x{byte_x >> 2};
        const bool inside{packed_x >= 0 && static_cast<u32>(packed_x) < narrow->width};
        CHECK(inside == (byte_x >= 0 && byte_x <= 28));
    }
}

TEST_CASE("Packed surface aliases use mapped device intervals", "[packed_surface]") {
    using Vulkan::ByteAddressedRange;
    const ByteAddressedRange target{0x100000, 0x4000};
    CHECK(target.Overlaps(target));
    CHECK(target.Overlaps({0x100001, 1}));
    CHECK(target.Overlaps({0xfffff, 2}));
    CHECK(target.Overlaps({0x103fff, 2}));
    CHECK_FALSE(target.Overlaps({0x104000, 1}));
    CHECK_FALSE(target.Overlaps({0xfffff, 1}));
    CHECK_FALSE(target.Overlaps({0x100000, 0}));
    CHECK_FALSE(ByteAddressedRange{0x100000, 0}.Overlaps(target));
    const ByteAddressedRange high{(std::numeric_limits<u64>::max)() - 16, 16};
    CHECK_FALSE(high.Overlaps(target));
    CHECK(high.Overlaps({(std::numeric_limits<u64>::max)() - 1, 1}));
}
