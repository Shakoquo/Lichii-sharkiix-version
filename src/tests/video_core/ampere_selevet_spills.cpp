// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};

Instruction Spill(bool store, u32 type, u32 value, u32 address, u32 offset,
                  u32 uniform = 64, bool alternate = false, u32 predicate = 7) {
    const bool uses_uniform = uniform != 64;
    return {{(store ? (alternate ? 0x987U : 0x387U) : 0x983U) | (predicate << 12) |
                 (store ? 0U : value << 16) | (address << 24),
             (store ? value : uses_uniform ? uniform : 0U) | (offset << 8),
             0x00100000U | (type << 9) | (uses_uniform ? 0x08000000U : 0U) |
                 (store && uses_uniform ? uniform : 0U), 0}};
}

u32 UniformMirror(u32 uniform) {
    if (uniform < 22) return 216 + uniform;
    if (uniform < 28) return 218 + uniform;
    if (uniform == 63) return 255;
    return 216 - ((uniform - 26) & ~1U) + uniform - 28;
}

struct SpillMachine {
    std::array<std::array<u32, 256>, 3> registers{};
    std::array<u8, 512> memory{};
    std::array<bool, 8> predicates{};

    SpillMachine() {
        predicates[7] = true;
        for (u32 i = 0; i < memory.size(); ++i) memory[i] = (i * 17 + 131) & 255;
    }

    void Run(std::span<const u64> code) {
        for (u64 word : code) {
            const u32 guard = (word >> 16) & 15;
            if (predicates[guard & 7] == bool(guard & 8)) continue;
            const u32 dest = word & 255, a = (word >> 8) & 255, b = (word >> 20) & 255;
            const auto read = [&](u32 bank, u32 reg) { return reg == 255 ? 0U : registers[bank][reg]; };
            if ((word >> 48) == 0x5c98) {
                const bool banked = ((word >> 44) & 15) == 10;
                const u32 bank = banked ? (word >> 8) & 3 : 0;
                if (dest != 255) registers[bank][dest] = read(banked ? (word >> 10) & 3 : 0, b);
            } else if ((word >> 48) == 0x5c10) {
                if (dest != 255) registers[0][dest] = read(0, a) + read(0, b);
            } else if ((word >> 52) == 0xef4 || (word >> 52) == 0xef5) {
                const bool store = (word >> 52) == 0xef5;
                const u32 type = (word >> 48) & 7;
                REQUIRE(type <= 4);
                const u32 bytes = type < 2 ? 1 : type < 4 ? 2 : 4;
                const u32 address = read(0, a) + ((word >> 20) & 0xffffff);
                REQUIRE(address + bytes <= memory.size());
                if (store) {
                    for (u32 i = 0; i < bytes; ++i) memory[address + i] = read(0, dest) >> (8 * i);
                } else {
                    u32 value = 0;
                    for (u32 i = 0; i < bytes; ++i) value |= u32{memory[address + i]} << (8 * i);
                    if ((type == 1 || type == 3) && (value & (1U << (8 * bytes - 1))))
                        value |= ~0U << (8 * bytes);
                    if (dest != 255) registers[0][dest] = value;
                }
            } else {
                REQUIRE(word == 0xe30000000007000fULL);
            }
        }
    }
};
}

TEST_CASE("Ampere local spills preserve subword signs and neighboring bytes",
          "[video_core][ampere][selevet_spills]") {
    for (u32 type = 0; type < 4; ++type) {
        const u32 bytes = type < 2 ? 1 : 2;
        for (u32 offset = 0; offset < 8; offset += bytes) {
            SpillMachine machine;
            machine.registers[0][4] = 32;
            machine.registers[0][2] = 0x1234fedc;
            const auto original = machine.memory;
            const auto lowered = LowerToMaxwell(std::array{
                Spill(true, type, 2, 4, offset), Spill(false, type, 3, 4, offset), Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            machine.Run(lowered.instructions);
            REQUIRE(machine.registers[0][3] == (type == 0 ? 0xdcU : type == 1 ? 0xffffffdcU :
                                               type == 2 ? 0xfedcU : 0xfffffedcU));
            for (u32 i = 0; i < machine.memory.size(); ++i)
                REQUIRE(machine.memory[i] == (i >= 32 + offset && i < 32 + offset + bytes ?
                    u8(0x1234fedcU >> (8 * (i - 32 - offset))) : original[i]));
        }
    }
}

TEST_CASE("Ampere uniform local spills cover both stores and every uniform bank",
          "[video_core][ampere][selevet_spills]") {
    for (u32 uniform : {0U, 21U, 22U, 27U, 28U, 29U, 31U, 62U, 63U}) {
        for (u32 base : {4U, 255U}) {
            for (u32 type = 0; type <= 6; ++type) {
                for (bool alternate : {false, true}) {
                    const u32 words = type == 6 ? 4 : type == 5 ? 2 : 1;
                    const auto lowered = LowerToMaxwell(std::array{
                        Spill(true, type, 8, base, 4, uniform, alternate),
                        Spill(false, type, 12, base, 4, uniform), Exit});
                    INFO(lowered.error);
                    REQUIRE(lowered);
                    SpillMachine machine;
                    machine.registers[0][4] = 32;
                    machine.registers[0][UniformMirror(uniform)] = 64;
                    for (u32 i = 0; i < words; ++i) machine.registers[0][8 + i] = 0x9876fedcU + i;
                    machine.Run(lowered.instructions);
                    for (u32 i = 0; i < words; ++i)
                        REQUIRE(machine.registers[0][12 + i] == (type == 0 ? 0xdcU : type == 1 ? 0xffffffdcU :
                            type == 2 ? 0xfedcU : type == 3 ? 0xfffffedcU : 0x9876fedcU + i));
                    REQUIRE(machine.registers[0][4] == 32);
                }
            }
        }
    }
}

TEST_CASE("Ampere uniform spills bank high data and address registers without corruption",
          "[video_core][ampere][selevet_spills]") {
    for (bool alternate : {false, true}) {
        const auto lowered = LowerToMaxwell(std::array{
            Spill(true, 6, 240, 244, 4, 31, alternate), Spill(false, 6, 248, 244, 4, 31), Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        SpillMachine machine;
        machine.registers[0][UniformMirror(31)] = 64;
        machine.registers[1][244] = 32;
        for (u32 i = 0; i < 4; ++i) machine.registers[1][240 + i] = 0xdeadbeefU + i;
        const auto original = machine.registers[0];
        machine.Run(lowered.instructions);
        for (u32 i = 0; i < 4; ++i) REQUIRE(machine.registers[1][248 + i] == 0xdeadbeefU + i);
        REQUIRE(machine.registers[1][244] == 32);
        REQUIRE(machine.registers[0][UniformMirror(31)] == 64);
        for (u32 i = 0; i < 212; ++i) REQUIRE(machine.registers[0][i] == original[i]);
    }
}

TEST_CASE("Ampere scalarized wide loads snapshot an overlapping address",
          "[video_core][ampere][selevet_spills]") {
    for (u32 type : {5U, 6U}) {
        SpillMachine machine;
        machine.registers[0][4] = 32;
        const auto lowered = LowerToMaxwell(std::array{Spill(false, type, 4, 4, 4), Exit});
        REQUIRE(lowered);
        machine.Run(lowered.instructions);
        for (u32 i = 0; i < (type == 6 ? 4U : 2U); ++i) {
            u32 expected = 0;
            for (u32 b = 0; b < 4; ++b) expected |= u32{machine.memory[36 + 4 * i + b]} << (8 * b);
            REQUIRE(machine.registers[0][4 + i] == expected);
        }
    }
}

TEST_CASE("Ampere local spills preserve predication and zero stores",
          "[video_core][ampere][selevet_spills]") {
    for (u32 guard : {0U, 7U, 8U, 15U}) {
        for (u32 type = 0; type <= 6; ++type) {
            const auto lowered = LowerToMaxwell(std::array{
                Spill(true, type, 255, 4, 4, 0, true, guard),
                Spill(false, type, 8, 4, 4, 0, false, guard), Exit});
            REQUIRE(lowered);
            SpillMachine machine;
            machine.registers[0][4] = 32;
            machine.registers[0][216] = 64;
            machine.registers[0][8] = 0xdeadbeef;
            const auto original = machine.memory;
            machine.Run(lowered.instructions);
            if (guard == 0 || guard == 15) {
                REQUIRE(machine.memory == original);
                REQUIRE(machine.registers[0][8] == 0xdeadbeef);
            } else {
                REQUIRE(machine.registers[0][8] == 0);
            }
        }
    }
}

TEST_CASE("Ampere local spill validation keeps sizes modes and ranges bounded",
          "[video_core][ampere][selevet_spills]") {
    for (bool store : {false, true}) {
        for (u32 uniform : {0U, 63U, 64U}) {
            for (u32 type = 0; type <= 6; ++type) {
                const u32 bytes = type < 2 ? 1 : type < 4 ? 2 : 4;
                const u32 words = type == 6 ? 4 : type == 5 ? 2 : 1;
                REQUIRE(LowerToMaxwell(std::array{Spill(store, type, 8, 4, 0x800000 - bytes * words, uniform), Exit}));
                REQUIRE_FALSE(LowerToMaxwell(std::array{Spill(store, type, 8, 4, 0x800000, uniform), Exit}));
                if (bytes > 1)
                    REQUIRE_FALSE(LowerToMaxwell(std::array{Spill(store, type, 8, 4, 1, uniform), Exit}));
                auto invalid = Spill(store, type, 8, 4, 0, uniform);
                invalid.words[2] |= 1U << 6;
                REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
            }
        }
        REQUIRE_FALSE(LowerToMaxwell(std::array{Spill(store, 7, 8, 4, 0), Exit}));
    }
    auto invalid = Spill(false, 4, 8, 4, 0, 0);
    invalid.words[1] = 64;
    REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    invalid = Spill(true, 4, 8, 4, 0, 0, true);
    invalid.words[2] &= ~0x08000000U;
    REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    REQUIRE(LowerToMaxwell(std::array{Spill(false, 6, 244, 4, 4), Exit}));
    REQUIRE(LowerToMaxwell(std::array{Spill(true, 6, 244, 4, 4), Exit}));
}
