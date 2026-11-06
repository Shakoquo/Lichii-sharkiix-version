// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};
}

TEST_CASE("Mario Tennis captured signed halfword conversions lower without losing sign",
          "[video_core][ampere][tennis]") {
    for (const Instruction captured : std::array{
             Instruction{{0x00017306, 0x00000001, 0x00101400, 0x020ea200}},
             Instruction{{0x00037306, 0x00000003, 0x00101400, 0x000e2400}}}) {
        const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{captured, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 2);
        REQUIRE(lowered.instructions.front() ==
                (0x5cb8000000072600ULL | captured.Bits(16, 8) |
                 (u64{captured.Bits(32, 8)} << 20)));
    }
}

TEST_CASE("Ampere halfword conversion keeps both selectors signedness and predicates",
          "[video_core][ampere][tennis]") {
    for (const u32 sign : {0U, 1U}) {
        for (const u32 half : {0U, 1U}) {
            for (const u32 guard : {0U, 7U, 9U}) {
                const Instruction native{{0x00020306U | (guard << 12),
                                          3U | (half << 28), 0x00101000U | (sign << 10), 0}};
                const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{native, Exit});
                REQUIRE(lowered);
                const u64 word = lowered.instructions.front();
                REQUIRE((word & 255) == 2);
                REQUIRE(((word >> 8) & 3) == 2);
                REQUIRE(((word >> 10) & 3) == 1);
                REQUIRE(((word >> 13) & 1) == sign);
                REQUIRE(((word >> 16) & 15) == guard);
                REQUIRE(((word >> 20) & 255) == 3);
                REQUIRE(((word >> 41) & 3) == half * 2);
            }
        }
    }
}
