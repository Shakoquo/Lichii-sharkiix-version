// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Duck912/eden-fefw native_shader.cpp at 8c6f052.
#include <array>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/trap_guards.h"
using Native = Shader::Ampere::Instruction;
constexpr Native Exit{{0x794d, 0, 0x03800000, 0}};
TEST_CASE("Uniform native trap guards reject changed constants and unknown inputs",
          "[video_core][ampere][fire_emblem][trap_guards]") {
    std::array<Native, 5> input{{
        {{0xff037624, 0, 0x078e00ff, 0}},
        {{0x0300720c, 255, 0x03f06270, 0}},
        {{0x0947, 16, 0x03800000, 0}},
        {{0x795c, 4, 0x00300000, 0}},
        Exit,
    }};
    u32 constant{};
    const Shader::Ampere::TrapGuardReader read = [&](u32 bank, u32 offset) -> std::optional<u32> {
        REQUIRE(bank == 0);
        REQUIRE(offset == 0);
        return constant;
    };
    const auto prefix = std::span{input}.first(4);
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        constant = value;
        REQUIRE(Shader::Ampere::VerifyTrapGuards(prefix, read) == (value < 0x80000000U));
    }
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, {}));
    REQUIRE_FALSE(
        Shader::Ampere::VerifyTrapGuards(prefix, [](u32, u32) { return std::optional<u32>{}; }));
    constant = 0;
    auto unknown = input;
    unknown[0] = Native{{0xff037224, 255, 0x078e0004, 0}};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[0] = Native{{0x00037919, 0, 0x2100, 0}};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[2].words[1] = 0xfffffff0;
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    const std::array<u32, 1> traps{48};
    REQUIRE(Shader::Ampere::LowerToMaxwell(input, false, false, traps));
    const std::array overwritten{input[0], Native{{0x00037984, 0, 0, 0}}, input[1], input[2],
                                 input[3]};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(overwritten, read));
}

TEST_CASE("Fire Emblem byte fill validates both rectangle bounds on every dispatch",
          "[video_core][ampere][fire_emblem][trap_guards]") {
    const std::array<Native, 19> code{{
        {{0xff017624, 0x0a00, 0x078e00ff, 0}},
        {{0xff007a0c, 0x5f00, 0x03f05270, 0}},
        {{0xff037624, 0x5b00, 0x078e00ff, 0}},
        {{0xff007807, 1, 0x04000000, 0}},
        {{0xff007a0c, 0x5f00, 0x03f05270, 0}},
        {{0x03027a12, 0x5a00, 0x078efcff, 0}},
        {{0xff007388, 0, 0, 0}},
        {{0x0200780c, 0xffffffff, 0x04704670, 0}},
        {{0x0947, 0x10, 0x03800000, 0}},
        {{0x795c, 4, 0x00300000, 0}},
        {{0x03007a10, 0x5d00, 0x07ffe0ff, 0}},
        {{0xff027624, 0x5c00, 0x078e00ff, 0}},
        {{0x7a0c, 0x5900, 0x03f04270, 0}},
        {{0x02007a10, 0x5a00, 0x07ffe0ff, 0}},
        {{0x7a0c, 0x5800, 0x04703270, 0}},
        {{0xff007a0c, 0x5f00, 0x00702670, 0}},
        {{0x0947, 0x10, 0x03800000, 0}},
        {{0x795c, 4, 0x00300000, 0}},
        Exit,
    }};
    std::unordered_map<u32, u32> constants{
        {0x28, 0}, {0x160, 1280}, {0x164, 720}, {0x168, 0}, {0x16c, 0},
        {0x170, 1280}, {0x174, 720}, {0x17c, 1},
    };
    const auto read = [&](u32 bank, u32 offset) -> std::optional<u32> {
        if (bank || !constants.contains(offset))
            return std::nullopt;
        return constants.at(offset);
    };
    const auto prefix = std::span{code}.first(18);
    REQUIRE(Shader::Ampere::VerifyTrapGuards(prefix, read));
    for (const u32 offset : {0x168U, 0x16cU}) {
        constants[offset] = 1; // Rectangle exceeds its buffer by one pixel.
        CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
        constants[offset] = 0xffffffffU; // Negative origin reaches the first trap.
        CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
        constants[offset] = 0;
        CHECK(Shader::Ampere::VerifyTrapGuards(prefix, read));
    }
    constants.erase(0x174);
    CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
    constants[0x174] = 720;
    CHECK(Shader::Ampere::VerifyTrapGuards(prefix, read));
    const std::array<u32, 2> traps{0x90, 0x110};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(code));
    const auto lowered = Shader::Ampere::LowerToMaxwell(code, false, false, traps);
    INFO(lowered.error);
    REQUIRE(lowered);
    const std::array<u32, 1> partial{0x90};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(code, false, false, partial));
    auto invalid = code;
    invalid[17].words[1] = 8;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid, false, false, traps));
}
