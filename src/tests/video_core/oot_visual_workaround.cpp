// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/ir_opt/oot_visual_workaround.h"

namespace {
namespace IR = Shader::IR;
constexpr u64 Title = 0x0400486024DEC000ULL;
constexpr u64 Fog = 0x780c0536b89fc89eULL;
constexpr u64 Final = 0xbded5ce917183214ULL;

struct Fixture {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    IR::IREmitter ir{block};

    Fixture() {
        program.blocks.push_back(&block);
        program.stage = Shader::Stage::Compute;
    }
};

float Evaluate(IR::Value value) {
    if (value.IsImmediate()) {
        return value.F32();
    }
    const auto& inst = *value.InstRecursive();
    switch (inst.GetOpcode()) {
    case IR::Opcode::GetCbufF32:
        return 2.0f;
    case IR::Opcode::FPExp2:
        return std::exp2(Evaluate(inst.Arg(0)));
    case IR::Opcode::FPNeg32:
        return -Evaluate(inst.Arg(0));
    case IR::Opcode::FPAdd32:
        return Evaluate(inst.Arg(0)) + Evaluate(inst.Arg(1));
    case IR::Opcode::FPMul32:
        return Evaluate(inst.Arg(0)) * Evaluate(inst.Arg(1));
    default:
        FAIL("Unexpected opcode in visual-workaround fixture");
        return 0;
    }
}
} // namespace

TEST_CASE("OoT visual workaround scales slice distance before extinction and scattering",
          "[video_core][oot_visual_workaround]") {
    Fixture f;
    const auto distance = f.ir.FPAdd(f.ir.FPNeg(f.ir.FPExp2(f.ir.Imm32(1.0f))),
                                    f.ir.FPExp2(f.ir.Imm32(3.0f)));
    const auto extinction = f.ir.FPAdd(f.ir.Imm32(1.0f), f.ir.FPMul(distance, f.ir.Imm32(2.0f)));
    const auto scattering = f.ir.FPMul(distance, f.ir.Imm32(4.0f));
    const auto transmittance = f.ir.FPExp2(IR::F32{f.ir.FPNeg(extinction)});
    REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog, Shader::Optimization::OotFog::Reduced) == 1);
    CHECK(Evaluate(distance) == 1.5f);
    CHECK(Evaluate(extinction) == 4.0f);
    CHECK(Evaluate(scattering) == 6.0f);
    CHECK(Evaluate(transmittance) == Catch::Approx(0.0625f));
}

TEST_CASE("OoT exposure workaround scales only the verified exposure load",
          "[video_core][oot_visual_workaround]") {
    Fixture f;
    f.program.stage = Shader::Stage::Fragment;
    const auto exposure = f.ir.GetFloatCbuf(f.ir.Imm32(3U), f.ir.Imm32(0x38U));
    const auto other_bank = f.ir.GetFloatCbuf(f.ir.Imm32(4U), f.ir.Imm32(0x38U));
    const auto other_offset = f.ir.GetFloatCbuf(f.ir.Imm32(3U), f.ir.Imm32(0x34U));
    const auto color = f.ir.FPMul(f.ir.Imm32(10.0f), exposure);
    const auto alpha = f.ir.Imm32(0.75f);
    REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Final, Shader::Optimization::OotFog::Reduced) == 1);
    CHECK(Evaluate(color) == Catch::Approx(13.0f));
    CHECK(Evaluate(other_bank) == 2.0f);
    CHECK(Evaluate(other_offset) == 2.0f);
    CHECK(Evaluate(alpha) == 0.75f);
}

TEST_CASE("OoT fog matches the native sign-bit negate without touching other bit operations",
          "[video_core][oot_visual_workaround]") {
    Fixture f;
    const auto value = f.ir.FPExp2(f.ir.Imm32(1.0f));
    const auto bits = f.ir.BitCast<IR::U32>(value);
    const auto negated = f.ir.BitCast<IR::F32>(f.ir.BitwiseXor(bits, f.ir.Imm32(0x80000000U)));
    static_cast<void>(f.ir.FPAdd(negated, f.ir.FPExp2(f.ir.Imm32(3.0f))));
    REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog, Shader::Optimization::OotFog::Reduced) == 1);
}

TEST_CASE("OoT visual workaround is off in original mode and rejects unknown titles stages and programs",
          "[video_core][oot_visual_workaround]") {
    Fixture f;
    f.program.stage = Shader::Stage::Fragment;
    const auto exposure = f.ir.GetFloatCbuf(f.ir.Imm32(3U), f.ir.Imm32(0x38U));
    CHECK_FALSE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Final, Shader::Optimization::OotFog::Original));
    CHECK_FALSE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title + 1, Final, Shader::Optimization::OotFog::Reduced));
    CHECK_FALSE(Shader::Optimization::ApplyOotVisualWorkaround(
        f.program, Title, 0x6cb06edf14be1d79ULL, Shader::Optimization::OotFog::Reduced));
    f.program.stage = Shader::Stage::Compute;
    CHECK_FALSE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Final, Shader::Optimization::OotFog::Reduced));
    CHECK(Evaluate(exposure) == 2.0f);
}

TEST_CASE("OoT visual workaround fails closed when the matched scalar shape changes",
          "[video_core][oot_visual_workaround]") {
    Fixture f;
    CHECK(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog, Shader::Optimization::OotFog::Reduced) == 0);
    for (int i = 0; i < 2; ++i) {
        static_cast<void>(f.ir.FPAdd(f.ir.FPNeg(f.ir.FPExp2(f.ir.Imm32(1.0f))),
                                    f.ir.FPExp2(f.ir.Imm32(3.0f))));
    }
    const auto size = f.block.size();
    CHECK(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog, Shader::Optimization::OotFog::Reduced) == 0);
    CHECK(f.block.size() == size);
}

TEST_CASE("OoT fog off zeroes the slice distance and keeps the exposure change",
          "[video_core][oot_visual_workaround]") {
    using Shader::Optimization::OotFog;
    {
        Fixture f;
        const auto distance = f.ir.FPAdd(f.ir.FPNeg(f.ir.FPExp2(f.ir.Imm32(1.0f))),
                                        f.ir.FPExp2(f.ir.Imm32(3.0f)));
        const auto extinction =
            f.ir.FPAdd(f.ir.Imm32(1.0f), f.ir.FPMul(distance, f.ir.Imm32(2.0f)));
        const auto scattering = f.ir.FPMul(distance, f.ir.Imm32(4.0f));
        REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog,
                                                               OotFog::Off) == 1);
        CHECK(Evaluate(distance) == 0.0f);
        CHECK(Evaluate(extinction) == 1.0f);
        CHECK(Evaluate(scattering) == 0.0f);
    }
    {
        Fixture f;
        f.program.stage = Shader::Stage::Fragment;
        const auto exposure = f.ir.GetFloatCbuf(f.ir.Imm32(3U), f.ir.Imm32(0x38U));
        const auto color = f.ir.FPMul(f.ir.Imm32(10.0f), exposure);
        REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Final,
                                                               OotFog::Off) == 1);
        CHECK(Evaluate(color) == Catch::Approx(13.0f));
    }
}

TEST_CASE("OoT fog switch reads on, reduced, off and original and honors the older opt-out",
          "[video_core][oot_visual_workaround]") {
    using Shader::Optimization::OotFog;
    using Shader::Optimization::ParseOotFog;
    CHECK(ParseOotFog(nullptr, nullptr) == OotFog::Original);
    CHECK(ParseOotFog(nullptr, "1") == OotFog::Reduced);
    CHECK(ParseOotFog(nullptr, "0") == OotFog::Original);
    CHECK(ParseOotFog("on", nullptr) == OotFog::On);
    CHECK(ParseOotFog("reduced", nullptr) == OotFog::Reduced);
    CHECK(ParseOotFog("off", nullptr) == OotFog::Off);
    CHECK(ParseOotFog("off", "1") == OotFog::Off);
    CHECK(ParseOotFog("on", "1") == OotFog::On);
    CHECK(ParseOotFog("reduced", "0") == OotFog::Original);
    CHECK(ParseOotFog("original", nullptr) == OotFog::Original);
    CHECK(ParseOotFog("original", "1") == OotFog::Original);
    CHECK(ParseOotFog("on", "0") == OotFog::On);
    CHECK(ParseOotFog("off", "0") == OotFog::Off);
    CHECK(ParseOotFog("bogus", "1") == OotFog::Reduced);
    CHECK(ParseOotFog("bogus", nullptr) == OotFog::Original);
}

TEST_CASE("OoT fog on keeps the full fog and the exposure correction",
          "[video_core][oot_visual_workaround]") {
    using Shader::Optimization::OotFog;
    {
        Fixture f;
        const auto distance = f.ir.FPAdd(f.ir.FPNeg(f.ir.FPExp2(f.ir.Imm32(1.0f))),
                                        f.ir.FPExp2(f.ir.Imm32(3.0f)));
        CHECK_FALSE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Fog,
                                                                   OotFog::On));
        CHECK(Evaluate(distance) == 6.0f);
    }
    {
        Fixture f;
        f.program.stage = Shader::Stage::Fragment;
        const auto color = f.ir.FPMul(f.ir.Imm32(10.0f), f.ir.GetFloatCbuf(f.ir.Imm32(3U),
                                                                           f.ir.Imm32(0x38U)));
        REQUIRE(Shader::Optimization::ApplyOotVisualWorkaround(f.program, Title, Final,
                                                               OotFog::On) == 1);
        CHECK(Evaluate(color) == Catch::Approx(13.0f));
    }
}
