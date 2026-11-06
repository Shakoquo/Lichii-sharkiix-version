// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::LowerToMaxwellWith;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr Instruction Popcount{{0x000272bf, 3, 0x08000000, 0}};

void Set(Instruction& instruction, u32 bit, u32 count, u32 value) {
    for (u32 i = 0; i < count; ++i) {
        const u32 mask = 1U << ((bit + i) % 32);
        auto& word = instruction.words[(bit + i) / 32];
        word = (word & ~mask) | (((value >> i) & 1U) ? mask : 0U);
    }
}

Instruction Surface(u32 opcode, u32 dimension, u32 mode) {
    Instruction instruction{{opcode | 0x7000U, 0, mode, 0}};
    Set(instruction, 16, 8, opcode == 0xb9b ? 0 : 8);
    Set(instruction, 24, 8, 2);
    Set(instruction, 32, 8, opcode == 0xb97 ? 0 : 12);
    Set(instruction, 40, 14, 0x10);
    Set(instruction, 54, 5, 2);
    Set(instruction, 61, 3, dimension);
    return instruction;
}

Instruction ArrayGather() {
    Instruction instruction{{0x04087f63, 0, 0x081e0fff, 0}};
    Set(instruction, 32, 8, 6);
    Set(instruction, 40, 6, 3);
    Set(instruction, 60, 1, 1);
    Set(instruction, 61, 3, 5);
    return instruction;
}

u64 Copy(u32 predicate, u32 destination, u32 source) {
    return 0x5c98000000000000ULL | (u64{predicate} << 16) | destination |
           (u64{15} << 39) | (u64{source} << 20);
}
} // namespace

TEST_CASE("Yoshi UPOPC preserves inverted sources and reserves uniform registers",
          "[video_core][ampere][yoshi]") {
    for (const u32 invert : {0U, 1U}) {
        auto instruction = Popcount;
        Set(instruction, 63, 1, invert);
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 2);
        REQUIRE(lowered.instructions.front() ==
                (0x5c08000000070000ULL | 218 | (u64{219} << 20) | (u64{invert} << 40)));
    }
    for (const auto [bit, count, value] : std::array{
             std::array{16U, 8U, 63U}, std::array{40U, 8U, 1U},
             std::array{12U, 4U, 0U}, std::array{64U, 1U, 1U}}) {
        auto invalid = Popcount;
        Set(invalid, bit, count, value);
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto atomic = Surface(0x593, 0, 0x001ee000);
    Set(atomic, 32, 8, 215);
    REQUIRE(LowerToMaxwell(std::array{Popcount, atomic, Exit}));
    Set(atomic, 32, 8, 216);
    REQUIRE(LowerToMaxwell(std::array{atomic, Exit}));
    REQUIRE_FALSE(LowerToMaxwell(std::array{Popcount, atomic, Exit}));
    Set(atomic, 32, 8, 212);
    REQUIRE_FALSE(LowerToMaxwellWith(std::array{Popcount, atomic, Exit}, 4));
}

TEST_CASE("Yoshi global atomics preserve operations and signed address offsets",
          "[video_core][ampere][yoshi]") {
    for (u32 operation = 0; operation <= 8; ++operation) {
        for (const s32 offset : {-0x80000, 0, 0x7ffff}) {
            Instruction instruction{{0x020493a8, 6, 0x001ee100, 0}};
            Set(instruction, 40, 24, static_cast<u32>(offset));
            Set(instruction, 87, 4, operation);
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.front() ==
                    (0xed00000000090004ULL | (u64{2} << 8) | (u64{6} << 20) |
                     (u64{static_cast<u32>(offset) & 0xfffff} << 28) |
                     (u64{1} << 48) | (u64{operation} << 52)));
        }
    }
    for (const auto [bit, count, value] : std::array{
             std::array{87U, 4U, 9U}, std::array{24U, 8U, 3U},
             std::array{40U, 24U, 0x80000U}, std::array{73U, 1U, 1U}}) {
        Instruction invalid{{0x020473a8, 6, 0x001ee100, 0}};
        Set(invalid, bit, count, value);
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
}

TEST_CASE("Yoshi one-dimensional surfaces retain masks and strong stores",
          "[video_core][ampere][yoshi]") {
    for (const u32 dimension : {0U, 1U, 3U, 4U, 5U}) {
        for (const u32 mask : {1U, 3U, 15U}) {
            for (const u32 opcode : {0xb97U, 0xb9bU}) {
                for (const bool strong : {false, true}) {
                    if (strong && opcode == 0xb97) continue;
                    auto instruction = Surface(opcode, dimension,
                        opcode == 0xb97 ? 0x001e0000U : strong ? 0x0010a000U : 0x00100000U);
                    Set(instruction, 72, 4, mask);
                    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                    INFO(lowered.error);
                    REQUIRE(lowered);
                    REQUIRE(lowered.required_texture_buffer == 2);
                    REQUIRE(lowered.instructions[strong ? 1 : 0] ==
                            ((opcode == 0xb97 ? 0xeb00000000070008ULL : 0xeb2000000007000cULL) |
                             (u64{2} << 8) | (u64{mask} << 20) | (u64{dimension} << 33) |
                             (u64{0x10} << 36) | (u64{1} << 51)));
                    REQUIRE(std::count(lowered.instructions.begin(), lowered.instructions.end(),
                                       0xef98000000070100ULL) == (strong ? 2 : 0));
                }
            }
        }
    }
    for (const u32 opcode : {0xb97U, 0xb9bU}) {
        auto instruction = Surface(opcode, 2, opcode == 0xb97 ? 0x001e0f00 : 0x00100f00);
        REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
        Set(instruction, 61, 3, 1);
        Set(instruction, opcode == 0xb97 ? 16 : 32, 8, 214);
        REQUIRE_FALSE(LowerToMaxwell(std::array{Popcount, instruction, Exit}));
    }
    auto first = Surface(0xb9b, 1, 0x00100300);
    auto invalid_mask = first;
    Set(invalid_mask, 72, 4, 5);
    REQUIRE_FALSE(LowerToMaxwell(std::array{invalid_mask, Exit}));
    auto second = first;
    Set(second, 54, 5, 3);
    const auto lowered = LowerToMaxwell(std::array{first, second, Exit});
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    REQUIRE(lowered.instructions[lowered.source_instruction_offsets[1] + 1] ==
            (0xeb2000000007000cULL | (u64{2} << 8) | (u64{3} << 20) |
             (u64{1} << 33) | (u64{250} << 39)));
}

TEST_CASE("Yoshi surface atomics match Maxwell bound U32 operation fields",
          "[video_core][ampere][yoshi]") {
    for (const u32 dimension : {0U, 1U, 3U, 4U, 5U}) {
        for (u32 operation = 0; operation <= 8; ++operation) {
            auto instruction = Surface(0x593, dimension, 0x001ee000);
            Set(instruction, 87, 4, operation);
            Set(instruction, 12, 4, 9);
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.required_texture_buffer == 2);
            REQUIRE(lowered.instructions.front() ==
                    (0xea00000000090008ULL | (u64{2} << 8) | (u64{12} << 20) |
                     (u64{operation} << 29) | (u64{dimension} << 33) | (u64{0x10} << 36)));
        }
    }
    for (const auto [bit, count, value] : std::array{
             std::array{87U, 4U, 9U}, std::array{61U, 3U, 2U},
             std::array{54U, 5U, 18U}, std::array{40U, 14U, 0x2000U},
             std::array{59U, 1U, 1U}, std::array{73U, 1U, 1U},
             std::array{96U, 1U, 1U}, std::array{32U, 8U, 246U},
             std::array{24U, 8U, 244U}}) {
        auto invalid = Surface(0x593, 5, 0x001ee000);
        Set(invalid, bit, count, value);
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto instruction = Surface(0x593, 5, 0x001ee000);
    Set(instruction, 24, 8, 213);
    REQUIRE(LowerToMaxwell(std::array{Popcount, instruction, Exit}));
    Set(instruction, 24, 8, 214);
    REQUIRE_FALSE(LowerToMaxwell(std::array{Popcount, instruction, Exit}));
    Set(instruction, 24, 8, 210);
    REQUIRE_FALSE(LowerToMaxwellWith(std::array{Popcount, instruction, Exit}, 4));
    auto other_binding = Surface(0x593, 0, 0x001ee000);
    Set(other_binding, 54, 5, 3);
    REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, other_binding, Exit}));
    Set(instruction, 16, 8, 255);
    Set(instruction, 32, 8, 255);
    REQUIRE(LowerToMaxwell(std::array{instruction, Exit}));
}

TEST_CASE("Yoshi register high-word left shifts retain clamp wrap and predicates",
          "[video_core][ampere][yoshi]") {
    for (const u32 guard : {0U, 7U, 9U}) {
        for (const u32 wrap : {0U, 1U}) {
            Instruction instruction{{0xff087219, 6, 0x00010604, 0}};
            Set(instruction, 12, 4, guard);
            Set(instruction, 75, 1, wrap);
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            const u64 word = lowered.instructions.front();
            REQUIRE(word == (0x5bf8000000000008ULL | (u64{guard} << 16) | (u64{255} << 8) |
                             (u64{6} << 20) | (u64{4} << 39) | (u64{wrap} << 50)));
            for (const u32 amount : {0U, 1U, 31U, 32U, 33U, 63U}) {
                const u32 shift = ((word >> 50) & 1) ? amount & 31 : std::min(amount, 32U);
                const u64 packed = u64{0x81234567} << 32;
                const u32 actual = static_cast<u32>((packed << shift) >> 32);
                const u32 expected = wrap ? 0x81234567U << (amount & 31)
                                         : amount < 32 ? 0x81234567U << amount : 0;
                REQUIRE(actual == expected);
            }
            Set(instruction, 24, 8, 2);
            REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
        }
    }
    for (const u32 opcode : {0xa19U, 0xc19U}) {
        Instruction instruction{{0xff087000U | opcode, opcode == 0xa19 ? 0x00800000U : 3U,
                                 opcode == 0xa19 ? 0x00010604U : 0x08010604U, 0}};
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions[lowered.instructions.size() - 2] ==
                (0x5bf8000000070008ULL | (u64{255} << 8) |
                 (u64{opcode == 0xa19 ? 251U : 219U} << 20) | (u64{4} << 39)));
    }
}

TEST_CASE("Yoshi uniform array gathers preserve coordinates handles and packed outputs",
          "[video_core][ampere][yoshi]") {
    for (const u32 mask : {1U, 3U, 5U, 7U, 15U}) {
        for (const u32 second : {16U, 255U}) {
            for (const u32 component : {0U, 1U, 2U, 3U}) {
                auto instruction = ArrayGather();
                Set(instruction, 72, 4, mask);
                Set(instruction, 64, 8, second);
                Set(instruction, 87, 2, component);
                Set(instruction, 12, 4, 9);
                const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                REQUIRE(lowered.required_texture_buffer == ~0U);
                const auto& words = lowered.instructions;
                REQUIRE(words[0] == Copy(9, 250, 4));
                REQUIRE(words[1] == Copy(9, 251, 5));
                REQUIRE(words[2] == Copy(9, 252, 6));
                REQUIRE(words[3] == Copy(9, 246, 219));
                REQUIRE(words[4] == (0xdec00000000900faULL | (u64{250} << 8) |
                                    (u64{246} << 20) | (u64{3} << 28) | (u64{mask} << 31) |
                                    (u64{component} << 38) | (u64{7} << 51)));
                const u32 count = static_cast<u32>(std::popcount(mask));
                REQUIRE(words.size() == 6 + count);
                for (u32 i = 0; i < count; ++i) {
                    REQUIRE(words[5 + i] == Copy(9, second == 255 || i < 2 ? 8 + i : second + i - 2,
                                                250 + i));
                }
            }
        }
    }
}

TEST_CASE("Yoshi uniform array gathers reject reserved spans and unsupported payloads",
          "[video_core][ampere][yoshi]") {
    for (const auto [bit, count, value] : std::array{
             std::array{16U, 8U, 215U}, std::array{24U, 8U, 215U},
             std::array{32U, 8U, 216U}, std::array{64U, 8U, 215U},
             std::array{64U, 8U, 9U}, std::array{60U, 1U, 0U},
             std::array{76U, 1U, 1U}, std::array{79U, 1U, 1U},
             std::array{80U, 1U, 1U}, std::array{89U, 1U, 1U},
             std::array{91U, 1U, 0U}, std::array{96U, 1U, 1U},
             std::array{72U, 4U, 0U}}) {
        auto invalid = ArrayGather();
        Set(invalid, bit, count, value);
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto instruction = ArrayGather();
    Set(instruction, 16, 8, 212);
    REQUIRE(LowerToMaxwell(std::array{instruction, Exit}));
    REQUIRE_FALSE(LowerToMaxwellWith(std::array{instruction, Exit}, 4));
    Set(instruction, 16, 8, 208);
    REQUIRE(LowerToMaxwellWith(std::array{instruction, Exit}, 4));
    Set(instruction, 24, 8, 211);
    REQUIRE_FALSE(LowerToMaxwellWith(std::array{instruction, Exit}, 4));
    Set(instruction, 24, 8, 4);
    Set(instruction, 40, 6, 31);
    const auto high_handle = LowerToMaxwell(std::array{instruction, Exit});
    INFO(high_handle.error);
    REQUIRE(high_handle);
    REQUIRE(high_handle.instructions[3] == Copy(7, 246, 215));
    Set(instruction, 16, 8, 255);
    const auto discarded = LowerToMaxwell(std::array{instruction, Exit});
    REQUIRE(discarded);
    REQUIRE(discarded.instructions.size() == 6);
}
