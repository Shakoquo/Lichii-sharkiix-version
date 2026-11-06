// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/native_indirect_reset.h"

TEST_CASE("Native indirect reset requires the complete captured program", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    REQUIRE(Reset::Matches(Reset::Program));
    REQUIRE_FALSE(Reset::Matches(std::span{Reset::Program}.first(43)));
    for (size_t i = 0; i < Reset::Program.size(); ++i) {
        auto other = Reset::Program;
        other[i] ^= 1;
        REQUIRE_FALSE(Reset::Matches(other));
    }
    REQUIRE(Reset::Record == std::array<u32, 5>{0, 1, 0, 0, 0});
}

TEST_CASE("Native indirect reset list ranges never wrap or truncate", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    constexpr u32 size = ~u32{0};
    const auto range = Reset::GetListRange(0x1000, size, 7, 18);
    REQUIRE(range);
    REQUIRE(range->address == 0x1038);
    REQUIRE(range->bytes == 144);
    REQUIRE_FALSE(Reset::GetListRange(0x1000, size, 0, 0));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, size, 0, (1U << 20) + 1));
    REQUIRE_FALSE(Reset::GetListRange(~u64{0}, size, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(~u64{0} - 15, size, 1, 1));
    REQUIRE(Reset::GetListRange(~u64{0} - 16, size, 1, 1));
}

TEST_CASE("Native indirect reset stays inside its list descriptor", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    REQUIRE_FALSE(Reset::GetListRange(0, 16, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 7, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, 2, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, 1, 2));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, ~u32{0}, 1));
    const auto range = Reset::GetListRange(0x1000, 23, 1, 1);
    REQUIRE(range);
    REQUIRE(range->address == 0x1008);
    REQUIRE(range->bytes == 8);
}
