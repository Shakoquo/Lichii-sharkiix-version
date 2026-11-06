// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Half conversion oracle and RTX 4090 reference fragments adapted from Duck912/eden-fefw
// native_shader.cpp at 8c6f0522b6ee07caa47ab4363823d3dc00aa8ac1.
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Native = Shader::Ampere::Instruction;
constexpr Native Exit{{0x794d, 0, 0x03800000, 0}};
double DecodeTestHalf(u16 bits) {
    const u32 exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    const double value = exponent == 31  ? (mantissa ? std::numeric_limits<double>::quiet_NaN()
                                                     : std::numeric_limits<double>::infinity())
                         : exponent == 0 ? std::ldexp(static_cast<double>(mantissa), -24)
                                         : std::ldexp(static_cast<double>(1024 + mantissa),
                                                      static_cast<int>(exponent) - 25);
    return (bits & 0x8000) ? -value : value;
}
u16 EncodeTestHalf(double value) {
    const u16 sign = std::signbit(value) ? 0x8000 : 0;
    value = std::abs(value);
    if (std::isnan(value))
        return 0xfe01; // Deliberately noncanonical: ReLU must canonicalize it.
    if (value >= 65520)
        return sign | 0x7c00;
    if (value < std::ldexp(1.0, -14))
        return sign | static_cast<u16>(std::nearbyint(std::ldexp(value, 24)));
    int exponent{};
    std::frexp(value, &exponent);
    --exponent;
    u32 mantissa = static_cast<u32>(std::nearbyint(std::ldexp(value, 10 - exponent)));
    if (mantissa == 2048) {
        ++exponent;
        mantissa = 1024;
    }
    return sign | static_cast<u16>(((exponent + 15) << 10) | (mantissa - 1024));
}

void RunWarp(std::span<const u64> code, std::array<std::array<u32, 256>, 32>& regs,
             std::span<const u16> shared = {}) {
    for (u64 word : code) {
        const auto bits = [&](u32 start, u32 size) {
            return u32((word >> start) & ((u64{1} << size) - 1));
        };
        REQUIRE(bits(16, 4) == 7);
        const u64 op = word & 0xfff8000000000000ULL;
        if ((word >> 52) == 0xe30)
            return;
        const auto previous = regs;
        for (u32 lane = 0; lane < 32; ++lane) {
            const auto get = [&](u32 reg) { return reg == 255 ? 0U : previous[lane][reg]; };
            const u32 dest = bits(0, 8), a = get(bits(8, 8)), b = get(bits(20, 8));
            u32 result{};
            if (op == 0xf0c8000000000000ULL)
                result = lane;
            else if ((word >> 56) == 0x04)
                result = a & bits(20, 32);
            else if (op == 0x3848000000000000ULL)
                result = a << bits(20, 5);
            else if (op == 0x3828000000000000ULL)
                result = a >> bits(20, 5);
            else if ((word >> 56) == 0x1c)
                result = a + bits(20, 32);
            else if (op == 0x5c10000000000000ULL)
                result = a + b;
            else if ((word >> 56) == 0x01)
                result = bits(20, 32);
            else if (op == 0xef10000000000000ULL) {
                REQUIRE(b < 32);
                REQUIRE(bits(34, 5) == 31);
                result = bits(8, 8) == 255 ? 0 : previous[b][bits(8, 8)];
            } else if (op == 0xef48000000000000ULL) {
                const u32 address = a + bits(20, 24);
                REQUIRE(address % 2 == 0);
                REQUIRE(address / 2 + 1 < shared.size());
                REQUIRE((bits(48, 3) == 2 || bits(48, 3) == 4));
                result = shared[address / 2];
                if (bits(48, 3) == 4)
                    result |= u32{shared[address / 2 + 1]} << 16;
            } else if (op == 0x5ca8000000000000ULL) {
                if (bits(8, 2) == 2 && bits(10, 2) == 1)
                    result = std::bit_cast<u32>(
                        static_cast<float>(DecodeTestHalf(u16(b >> (bits(41, 1) * 16)))));
                else {
                    REQUIRE(bits(8, 2) == 1);
                    REQUIRE(bits(10, 2) == 2);
                    result = EncodeTestHalf(std::bit_cast<float>(b));
                }
            } else if ((word >> 52) == 0x598) {
                result =
                    std::bit_cast<u32>(std::fma(std::bit_cast<float>(a), std::bit_cast<float>(b),
                                                std::bit_cast<float>(get(bits(39, 8)))));
            } else if (op == 0x5c18000000000000ULL)
                result = (a << bits(39, 5)) + b;
            else
                FAIL("Unexpected matrix expansion opcode " << std::hex << word);
            if (dest != 255)
                regs[lane][dest] = result;
        }
    }
}
} // namespace

TEST_CASE("Native matrix loads preserve uniform offsets and aliased row addresses",
          "[video_core][ampere][fire_emblem]") {
    const u32 mode = GENERATE(0U, 1U, 2U);
    const u32 dest = GENERATE(8U, 96U);
    const bool uniform = GENERATE(false, true);
    const bool transpose = GENERATE(false, true);
    const Native load{{0x0800783bU | (dest << 16), (32U << 8) | (uniform ? 5U : 0U),
                       (mode << 8) | (transpose ? 0x4000U : 0U) | (uniform ? 0x08000000U : 0U), 0}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{load, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    std::array<u16, 1024> shared;
    for (u32 i = 0; i < shared.size(); ++i)
        shared[i] = u16(i * 71 + 11);
    std::array<std::array<u32, 256>, 32> regs{};
    for (u32 lane = 0; lane < 32; ++lane) {
        for (u32 r = 0; r < 255; ++r)
            regs[lane][r] = r * 101 + lane;
        regs[lane][8] = 128 + lane * 16;
        regs[lane][221] = 64; // UR5
    }
    const auto before = regs;
    RunWarp(lowered.instructions, regs, shared);
    for (u32 lane = 0; lane < 32; ++lane) {
        for (u32 matrix = 0; matrix < (1U << mode); ++matrix) {
            const u32 row = transpose ? 2 * (lane % 4) : lane / 4;
            const u32 column = transpose ? lane / 4 : 2 * (lane % 4);
            const u32 address =
                (128 + (matrix * 8 + row) * 16 + 32 + (uniform ? 64 : 0)) / 2 + column;
            const u32 expected =
                shared[address] | (u32{shared[address + (transpose ? 8 : 1)]} << 16);
            REQUIRE(regs[lane][dest + matrix] == expected);
        }
        for (u32 r = 0; r < 244; ++r)
            if (r < dest || r >= dest + (1U << mode))
                REQUIRE(regs[lane][r] == before[lane][r]);
    }
}

TEST_CASE("Native half matrix rounding matches measured hardware fragments",
          "[video_core][ampere][fire_emblem]") {
    const u32 depth = GENERATE(8U, 16U);
    const u32 dest = GENERATE(8U, 16U, 24U, 32U);
    const bool uniforms = GENERATE(false, true);
    // RTX 4090 driver-API MMA samples, seed zero with a nonzero C fragment.
    static constexpr std::array<std::array<u32, 64>, 2> hardware{
        {{{
             0x42af42edU, 0xc05ec08fU, 0x42334271U, 0xbff4c02cU, 0x41b641f5U, 0xbf2dbf90U,
             0x413a4178U, 0xbe66bec9U, 0x414e417eU, 0xc1bfc1ffU, 0x40ed411dU, 0xc140c17fU,
             0x408d40bdU, 0xc0c0c100U, 0x402c405cU, 0xc040c080U, 0x3fd8400eU, 0xc01dc042U,
             0x3f4f3f94U, 0xbfa7bff0U, 0x3ec63f0bU, 0xbf13bf5dU, 0x3e3e3e82U, 0xbe80bec9U,
             0x3d153d3dU, 0x32de338dU, 0x3cc43cecU, 0x318d3234U, 0x3c733c9cU, 0x303630dfU,
             0x3c223c4aU, 0x2dbf2f14U, 0x38a438bcU, 0x42a742e1U, 0x3872388bU, 0x4233426dU,
             0x38403859U, 0x41be41f9U, 0x380e3827U, 0x414a4184U, 0xaf15b008U, 0x41454171U,
             0xad24ae1cU, 0x40ed4119U, 0xaa63ac2bU, 0x409540c1U, 0xa4fea870U, 0x403d4069U,
             0xba69bac0U, 0x3fc84002U, 0xb9bbba12U, 0x3f4f3f8bU, 0xb90cb964U, 0x3ed73f13U,
             0xb85eb8b5U, 0x3e5e3e9aU, 0xbdf8be3fU, 0x3d043d24U, 0xbd69bdb0U, 0x3cc43ce4U,
             0xbcd9bd21U, 0x3c833ca4U, 0xbc4abc92U, 0x3c433c63U,
         }},
         {{
             0x43d6441bU, 0xc207c249U, 0x43164376U, 0xc183c1c5U, 0x425542b6U, 0xc0fec140U,
             0x419441f5U, 0xc07ac0bcU, 0x42114255U, 0xc1cdc1edU, 0x418841ccU, 0xc18cc1acU,
             0x40ff4144U, 0xc14ac16bU, 0x407640baU, 0xc108c129U, 0x404b4073U, 0xbc95bc6aU,
             0x3ff44023U, 0xbceabcbfU, 0x3f523fa3U, 0xbd3ebd14U, 0x3eb13f02U, 0xbd94bd69U,
             0x3d0a3d22U, 0x3b713c26U, 0x3cd83cf0U, 0x39bb3a96U, 0x3ca63cc0U, 0x380538e0U,
             0x3c753c8dU, 0x349f3654U, 0x35f83579U, 0x43b14405U, 0x36f13675U, 0x43014359U,
             0x37eb376eU, 0x425042a9U, 0x38723834U, 0x41a041f8U, 0xb81cb8cbU, 0x41eb4227U,
             0xb57eb6dbU, 0x417241aeU, 0xb189b422U, 0x40fa4136U, 0x995cad9eU, 0x408240beU,
             0xbd9abe29U, 0x40254045U, 0xbc7bbd0bU, 0x3fc94005U, 0xbab9bbd9U, 0x3f493f89U,
             0xb87db99bU, 0x3ec83f08U, 0xc093c0f7U, 0x3cbe3cc6U, 0xbf98c02fU, 0x3cad3cb6U,
             0xbe08bed0U, 0x3c9c3ca5U, 0xbc7abd42U, 0x3c8c3c94U,
         }}}};
    std::vector<Native> input{{
        {{(8U << 24) | (dest << 16) | 0x723cU, 16, (depth == 16 ? 0x800U : 0U) | 24U, 0}},
        Exit,
    }};
    if (uniforms)
        input.insert(input.begin(), Native{{0xfc02, 0, 0, 0}});
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    const auto a = [](u32 row, u32 k) {
        return (static_cast<int>((row * 17 + k * 5) % 199) - 99) * 0.00731;
    };
    const auto b = [](u32 k, u32 col) {
        return (static_cast<int>((k * 7 + col * 3) % 211) - 105) * 0.00917;
    };
    const auto c = [](u32 row, u32 col) {
        return (static_cast<int>((row * 5 + col * 3) % 97) - 48) * 0.00319;
    };
    const auto pack = [](double low, double high) {
        return u32{EncodeTestHalf(low)} | (u32{EncodeTestHalf(high)} << 16);
    };
    std::array<std::array<u32, 256>, 32> registers{};
    for (auto& lane : registers)
        for (u32 reg = 0; reg < 255; ++reg)
            lane[reg] = 0xcafe0000U + reg;
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 group = lane / 4, thread = lane % 4;
        for (u32 fragment = 0; fragment < depth / 4; ++fragment) {
            const u32 row = group + (fragment % 2) * 8, k = thread * 2 + (fragment / 2) * 8;
            registers[lane][8 + fragment] = pack(a(row, k), a(row, k + 1));
        }
        for (u32 fragment = 0; fragment < 2; ++fragment) {
            const u32 k = thread * 2 + fragment * 8;
            if (fragment < depth / 8)
                registers[lane][16 + fragment] = pack(b(k, group), b(k + 1, group));
            registers[lane][24 + fragment] =
                pack(c(group + fragment * 8, thread * 2), c(group + fragment * 8, thread * 2 + 1));
        }
    }
    const auto before = registers;
    RunWarp(lowered.instructions, registers);
    for (u32 lane = 0; lane < 32; ++lane)
        for (u32 fragment = 0; fragment < 2; ++fragment) {
            INFO("depth=" << depth << " lane=" << lane << " fragment=" << fragment);
            REQUIRE(registers[lane][dest + fragment] == hardware[depth == 16][lane * 2 + fragment]);
        }
    for (u32 lane = 0; lane < 32; ++lane)
        for (u32 reg = 0; reg < 246; ++reg)
            if (reg != dest && reg != dest + 1)
                REQUIRE(registers[lane][reg] == before[lane][reg]);
}
