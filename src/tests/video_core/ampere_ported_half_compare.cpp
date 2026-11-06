// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

TEST_CASE("Ampere packed half comparisons preserve all register-form controls",
          "[video_core][ampere][fire_emblem]") {
    for (u32 cmp = 0; cmp < 16; ++cmp) {
        for (u32 combine = 0; combine < 3; ++combine) {
            for (u32 swizzle = 0; swizzle < 4; ++swizzle) {
                for (u32 flags = 0; flags < 64; ++flags) {
                    const u32 a_neg = flags & 1, a_abs = (flags >> 1) & 1;
                    const u32 b_neg = (flags >> 2) & 1, b_abs = (flags >> 3) & 1;
                    const u32 bf = (flags >> 4) & 1, ftz = (flags >> 5) & 1;
                    const std::array<Instruction, 2> code{{
                        {{0x02017233, 3U | (swizzle << 28) | (b_abs << 30) | (b_neg << 31),
                          (combine << 5) | (bf << 7) | (a_neg << 8) | (a_abs << 9) |
                          (swizzle << 10) | (cmp << 12) | (ftz << 16) | (10U << 23), 0}},
                        {{0x794d, 0, 0x03800000, 0}},
                    }};
                    const auto lowered = LowerToMaxwell(code);
                    REQUIRE(lowered);
                    const u64 expected = 0x5d18000000070001ULL | (2ULL << 8) | (3ULL << 20) |
                        (u64{swizzle} << 28) | (u64{b_abs} << 30) | (u64{b_neg} << 31) |
                        (u64{cmp} << 35) | (10ULL << 39) | (u64{a_neg} << 43) |
                        (u64{a_abs} << 44) | (u64{combine} << 45) | (u64{swizzle} << 47) |
                        (u64{bf} << 49) | (u64{ftz} << 50);
                    REQUIRE(lowered.instructions.front() == expected);
                }
            }
        }
    }
    std::array<Instruction, 2> invalid{{{{0x02017233, 3, 3U << 5, 0}}, {{0x794d, 0, 0x03800000, 0}}}};
    REQUIRE_FALSE(LowerToMaxwell(invalid));
}
