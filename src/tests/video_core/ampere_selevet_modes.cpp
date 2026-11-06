// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <bit>
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};

bool Compare(u32 a, u32 b, u32 operation, bool signed_compare) {
    const bool lt = signed_compare ? s32(a) < s32(b) : a < b;
    const bool eq = a == b;
    return operation == 0 ? false : operation == 1 ? lt : operation == 2 ? eq :
           operation == 3 ? lt || eq : operation == 4 ? !lt && !eq :
           operation == 5 ? !eq : operation == 6 ? !lt : true;
}

struct ModeMachine {
    std::array<u32, 256> registers{};
    std::array<bool, 8> predicates{true, false, true, true, false, true, false, true};

    void Run(std::span<const u64> code) {
        const auto read = [&](u32 reg) { return reg == 255 ? 0U : registers[reg]; };
        for (const u64 word : code) {
            const u32 guard = (word >> 16) & 15;
            if (predicates[guard & 7] == bool(guard & 8)) continue;
            const u32 dest = word & 255, a = (word >> 8) & 255, b = (word >> 20) & 255;
            const u32 immediate = word >> 20;
            if ((word >> 48) == 0x38e8) {
                const u32 mask = immediate & 127;
                u32 packed = 0;
                for (u32 i = 0; i < 7; ++i) packed |= u32{predicates[i]} << i;
                if (dest != 255) registers[dest] = (read(a) & ~mask) | (packed & mask);
            } else if ((word >> 48) == 0x38f0) {
                const u32 value = read(a), mask = immediate & 127;
                for (u32 i = 0; i < 7; ++i) if (mask & (1U << i)) predicates[i] = (value >> i) & 1;
            } else if ((word >> 48) == 0x0100 || (word >> 56) == 1) {
                if (dest != 255) registers[dest] = immediate;
            } else if ((word >> 48) == 0x5b60 || ((word >> 48) & 0xfff0) == 0x5b60) {
                const u32 output = (word >> 3) & 7, input = (word >> 39) & 7;
                const bool cmp = Compare(read(a), read(b), (word >> 49) & 7, (word >> 48) & 1);
                const bool other = predicates[input] != bool((word >> 42) & 1);
                const u32 op = (word >> 45) & 3;
                if (output != 7) predicates[output] = op == 0 ? cmp && other : op == 1 ? cmp || other : cmp != other;
            } else if (((word >> 48) & 0xfffe) == 0x3800) {
                const u32 width = (word >> 28) & 255;
                REQUIRE(width <= 32);
                u32 value = width == 32 ? read(a) : read(a) & ((1U << width) - 1);
                if (width && width < 32 && ((word >> 48) & 1) && (value & (1U << (width - 1))))
                    value |= ~0U << width;
                if (dest != 255) registers[dest] = value;
            } else if (((word >> 48) & 0xfffc) == 0x5c10) {
                const u32 lhs = ((word >> 49) & 1) ? -read(a) : read(a);
                const u32 rhs = ((word >> 48) & 1) ? -read(b) : read(b);
                if (dest != 255) registers[dest] = lhs + rhs;
            } else if ((word >> 56) == 0x1c || (word >> 56) == 0x1d) {
                if (dest != 255) registers[dest] = (((word >> 56) & 1) ? -read(a) : read(a)) + immediate;
            } else if (((word >> 48) & 0xfffe) == 0x5c18) {
                if (dest != 255) registers[dest] = (read(a) << ((word >> 39) & 31)) +
                    (((word >> 48) & 1) ? -read(b) : read(b));
            } else if ((word >> 48) == 0x5080) {
                const float value = std::bit_cast<float>(read(a));
                const u32 op = (word >> 20) & 15;
                REQUIRE(op == 2);
                if (dest != 255) registers[dest] = std::bit_cast<u32>(std::exp2(value));
            } else {
                REQUIRE(word == 0xe30000000007000fULL);
            }
        }
    }
};
}

TEST_CASE("Ampere signed SGXT extends the selected low bits",
          "[video_core][ampere][selevet_modes]") {
    for (u32 width : {0U, 1U, 8U, 16U, 31U, 32U}) {
        for (bool sign : {false, true}) {
            const auto lowered = LowerToMaxwell(std::array{
                Instruction{{0x0201781a, width, u32{sign} << 9, 0}}, Exit});
            REQUIRE(lowered);
            for (u32 value : {0U, 1U, 0xffU, 0x8000U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
                ModeMachine machine;
                machine.registers[2] = value;
                machine.Run(lowered.instructions);
                const u32 mask = width == 32 ? ~0U : (1U << width) - 1;
                const u32 truncated = value & mask;
                const s64 modulus = s64{1} << width;
                const bool negative = sign && width && (s64{truncated} >= modulus / 2);
                const u32 expected = negative ? u32(s64{truncated} - modulus) : truncated;
                REQUIRE(machine.registers[1] == expected);
            }
        }
    }
    REQUIRE_FALSE(LowerToMaxwell(std::array{Instruction{{0x0201781a, 33, 0x200, 0}}, Exit}));
    REQUIRE_FALSE(LowerToMaxwell(std::array{Instruction{{0x0201781a, 8, 0xa00, 0}}, Exit}));
}

TEST_CASE("Ampere UISETP combines uniform predicates without changing ordinary predicates",
          "[video_core][ampere][selevet_modes]") {
    constexpr std::array<std::array<u32, 2>, 4> Values{{{0, 0}, {1, 2}, {~0U, 0}, {0x80000000, 0x7fffffff}}};
    for (u32 output = 0; output < 7; ++output) {
        for (u32 input = 0; input < 8; ++input) {
            for (u32 combine = 0; combine < 3; ++combine) {
                for (bool negate : {false, true}) {
                    for (bool sign : {false, true}) {
                        for (u32 cmp : {1U, 2U, 5U, 6U}) {
                            for (const auto values : Values) {
                                for (bool immediate : {false, true}) {
                                    const Instruction instruction{{immediate ? 0x0000788cU : 0x0000728cU,
                                        immediate ? values[1] : 1U,
                                        0x08700070U | (u32{sign} << 9) | (combine << 10) | (cmp << 12) |
                                            (output << 17) | (input << 23) | (u32{negate} << 26), 0}};
                                    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                                    INFO(lowered.error);
                                    REQUIRE(lowered);
                                    ModeMachine machine;
                                    machine.registers[216] = values[0];
                                    machine.registers[217] = values[1];
                                    machine.registers[239] = 0xa5a50055;
                                    const auto predicates = machine.predicates;
                                    machine.Run(lowered.instructions);
                                    const bool lhs = Compare(values[0], values[1], cmp, sign);
                                    const bool rhs = (input == 7 || ((0x55U >> (6 - input)) & 1)) != negate;
                                    const bool expected = combine == 0 ? lhs && rhs : combine == 1 ? lhs || rhs : lhs != rhs;
                                    const u32 mask = 1U << (6 - output);
                                    REQUIRE(machine.registers[239] == ((0xa5a50055U & ~mask) | (u32{expected} << (6 - output))));
                                    REQUIRE(machine.predicates == predicates);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Ampere immediate MUFU materializes the full float bit pattern",
          "[video_core][ampere][selevet_modes]") {
    for (float value : {-2.0f, -0.0f, 0.0f, 1.0f, 10.0f}) {
        const auto lowered = LowerToMaxwell(std::array{
            Instruction{{0x00017908, std::bit_cast<u32>(value), 2U << 10, 0}}, Exit});
        REQUIRE(lowered);
        ModeMachine machine;
        machine.Run(lowered.instructions);
        REQUIRE(machine.registers[1] == std::bit_cast<u32>(std::exp2(value)));
    }
    for (u32 invalid : {0x200U, 0x100U, 6U << 10, 7U << 10, 10U << 10})
        REQUIRE_FALSE(LowerToMaxwell(std::array{Instruction{{0x00017908, 0x3f800000, invalid, 0}}, Exit}));
}

TEST_CASE("Ampere simple uniform adds and LEAs obey positive and inverted guards",
          "[video_core][ampere][selevet_modes]") {
    for (u32 guard = 0; guard < 15; ++guard) {
        for (u32 upreds : {0U, 0x55U, 0x7fU}) {
            for (bool immediate : {false, true}) {
                for (bool lea : {false, true}) {
                    const u32 op = lea ? (immediate ? 0x891 : 0x291) : (immediate ? 0x890 : 0x290);
                    const Instruction instruction{{op | (guard << 12) | (1U << 24),
                        immediate ? 13U : 2U, lea ? 0x080e183fU : 0x0fffe03fU, 0}};
                    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                    INFO(lowered.error);
                    REQUIRE(lowered);
                    ModeMachine machine;
                    machine.registers[216] = 0xdeadbeef;
                    machine.registers[217] = 0x12345678;
                    machine.registers[218] = 41;
                    machine.registers[239] = upreds;
                    const auto predicates = machine.predicates;
                    machine.Run(lowered.instructions);
                    const bool executes = (guard & 7) == 7 ||
                        (bool((upreds >> (6 - (guard & 7))) & 1) != bool(guard & 8));
                    const u32 expected = (0x12345678U << (lea ? 3 : 0)) + (immediate ? 13 : 41);
                    REQUIRE(machine.registers[216] == (executes ? expected : 0xdeadbeef));
                    REQUIRE(machine.registers[239] == upreds);
                    REQUIRE(machine.predicates == predicates);
                }
            }
        }
    }
    REQUIRE_FALSE(LowerToMaxwell(std::array{Instruction{{0x01000890, 13, 0x0ff3e03f, 0}}, Exit}));
    REQUIRE_FALSE(LowerToMaxwell(std::array{Instruction{{0x01000891, 13, 0x080e1c3f, 0}}, Exit}));
}

TEST_CASE("Ampere guarded three-input adds preserve the sign of full-width immediates",
          "[video_core][ampere][selevet_modes]") {
    for (u32 guard : {0U, 8U}) {
        for (bool condition : {false, true}) {
            for (s32 immediate : {-13, 13, -2147483647}) {
                const Instruction instruction{{0x01000890U | (guard << 12), u32(immediate),
                                                0x0fffe003, 0}};
                const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                REQUIRE(lowered);
                ModeMachine machine;
                machine.registers[216] = 0xdeadbeef;
                machine.registers[217] = 100;
                machine.registers[219] = 7;
                machine.registers[239] = u32{condition} << 6;
                const auto predicates = machine.predicates;
                machine.Run(lowered.instructions);
                const bool executes = condition != bool(guard & 8);
                REQUIRE(machine.registers[216] == (executes ? 107U + u32(immediate) : 0xdeadbeef));
                REQUIRE(machine.registers[239] == u32{condition} << 6);
                REQUIRE(machine.predicates == predicates);
            }
        }
    }
}
