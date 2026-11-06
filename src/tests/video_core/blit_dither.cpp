// SPDX-License-Identifier: GPL-3.0-or-later

#include <initializer_list>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;
using IR::Opcode;
using IR::Value;

class BlitEnvironment final : public Shader::Environment {
public:
    bool native{true};

    explicit BlitEnvironment(Shader::Stage shader_stage) {
        stage = shader_stage;
    }

    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32) override { return 0; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return native; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

struct BlitProgram {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    BlitEnvironment env;

    explicit BlitProgram(Shader::Stage stage = Shader::Stage::Fragment) : env{stage} {
        program.stage = stage;
        program.blocks = {&block};
        program.post_order_blocks = {&block};
    }

    Value Emit(Opcode opcode, std::initializer_list<Value> args) {
        block.AppendNewInst(opcode, args);
        return Value{&block.back()};
    }

    Value Cbuf(u32 bank, u32 offset) {
        return Emit(Opcode::GetCbufU32, {Value{bank}, Value{offset}});
    }

    Value ColorBits() {
        const Value color = Emit(Opcode::GetAttribute, {Value{IR::Attribute::Generic0X}, Value{0U}});
        return Emit(Opcode::BitCastU32F32, {color});
    }

    void Run() {
        Shader::Optimization::BlitConstantsPass(env, program);
    }
};

void RequireBlitWord(Value value, u32 offset) {
    value = value.Resolve();
    if (offset == 0x18) {
        REQUIRE(value.IsImmediate());
        REQUIRE(value.U32() == 0x3f800000U);
        return;
    }
    REQUIRE_FALSE(value.IsImmediate());
    const auto* bits = value.InstRecursive();
    REQUIRE(bits->GetOpcode() == Opcode::BitCastU32F32);
    const auto* reciprocal = bits->Arg(0).InstRecursive();
    REQUIRE(reciprocal->GetOpcode() == Opcode::FPRecip32);
    const auto* dimension = reciprocal->Arg(0).InstRecursive();
    REQUIRE(dimension->GetOpcode() == Opcode::CompositeExtractF32x4);
    REQUIRE(dimension->Arg(0).InstRecursive()->GetOpcode() == Opcode::RenderArea);
    REQUIRE(dimension->Arg(1).U32() == (offset == 0x10 ? 0U : 1U));
}

}

TEST_CASE("Blit dither changes only the float color use of shared bias arithmetic",
          "[video_core][blit_dither]") {
    for (const u32 offset : {0x10U, 0x14U, 0x18U}) {
        for (const auto opcode : {Opcode::IAdd32, Opcode::ISub32}) {
            BlitProgram shader;
            const Value bias = shader.Cbuf(17, offset);
            const Value bits = shader.ColorBits();
            const Value sum = shader.Emit(opcode, {bits, bias});
            const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
            const Value integer = shader.Emit(Opcode::IAdd32, {sum, Value{7U}});
            const Value bias_float = shader.Emit(Opcode::BitCastF32U32, {bias});
            const Value fp = shader.Emit(Opcode::FPAdd32, {bias_float, Value{1.0f}});
            const Value typed_load = shader.Emit(Opcode::GetCbufF32, {Value{17U}, Value{offset}});
            const Value typed_fp = shader.Emit(Opcode::FPAdd32, {typed_load, Value{1.0f}});
            shader.Run();
            REQUIRE(color.Inst()->Arg(0).Resolve() == bits);
            REQUIRE(integer.Inst()->Arg(0) == sum);
            REQUIRE(sum.Inst()->Arg(0) == bits);
            RequireBlitWord(sum.Inst()->Arg(1), offset);
            REQUIRE(fp.Inst()->Arg(0) == bias_float);
            RequireBlitWord(bias_float.Inst()->Arg(0), offset);
            REQUIRE(typed_fp.Inst()->Arg(0) == typed_load);
            REQUIRE(typed_load.Resolve().InstRecursive()->GetOpcode() == Opcode::BitCastF32U32);
            RequireBlitWord(typed_load.Resolve().InstRecursive()->Arg(0), offset);
            REQUIRE(shader.program.info.uses_render_area);
        }
    }
}

TEST_CASE("Blit dither removes the bias from a phi of float colors",
          "[video_core][blit_dither]") {
    for (const bool float_operands : {true, false}) {
        BlitProgram shader;
        const Value bias = shader.Cbuf(17, 0x10);
        const Value linear = shader.ColorBits();
        const Value encoded = float_operands ? shader.ColorBits() : Value{0x3f000000U};
        shader.block.AppendNewInst(Opcode::Phi, {});
        IR::Inst* const phi = &shader.block.back();
        phi->AddPhiOperand(&shader.block, linear);
        phi->AddPhiOperand(&shader.block, encoded);
        const Value sum = shader.Emit(Opcode::IAdd32, {Value{phi}, bias});
        const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
        shader.Run();
        if (float_operands) {
            REQUIRE(color.Inst()->Arg(0).Resolve() == Value{phi});
        } else {
            REQUIRE(color.Inst()->Arg(0) == sum);
        }
    }
}

TEST_CASE("Blit dither follows cbuf and arithmetic identities with either add order",
          "[video_core][blit_dither]") {
    BlitProgram shader;
    const Value bank = shader.Emit(Opcode::Identity, {Value{17U}});
    const Value offset = shader.Emit(Opcode::Identity, {Value{0x10U}});
    const Value load = shader.Emit(Opcode::GetCbufU32, {bank, offset});
    const Value alias = shader.Emit(Opcode::Identity, {load});
    const Value bias = shader.Emit(Opcode::Identity, {alias});
    const Value bits = shader.ColorBits();
    const Value color_alias = shader.Emit(Opcode::Identity, {bits});
    const Value sum = shader.Emit(Opcode::IAdd32, {bias, color_alias});
    const Value sum_alias = shader.Emit(Opcode::Identity, {sum});
    const Value color = shader.Emit(Opcode::BitCastF32U32, {sum_alias});
    shader.Run();
    REQUIRE(color.Inst()->Arg(0).Resolve() == bits);
    REQUIRE(sum.Inst()->Arg(1) == color_alias);
    RequireBlitWord(bias, 0x10);
}

TEST_CASE("Blit dither preserves unrelated banks and integer arithmetic",
          "[video_core][blit_dither]") {
    BlitProgram shader;
    const Value blit = shader.Cbuf(17, 0x10);
    const Value unrelated = shader.Cbuf(2, 0x10);
    const Value bits = shader.ColorBits();
    const Value other_sum = shader.Emit(Opcode::IAdd32, {bits, unrelated});
    const Value other_color = shader.Emit(Opcode::BitCastF32U32, {other_sum});
    const Value integer_sum = shader.Emit(Opcode::IAdd32, {Value{123U}, blit});
    const Value integer_float = shader.Emit(Opcode::BitCastF32U32, {integer_sum});
    const Value reversed_sub = shader.Emit(Opcode::ISub32, {blit, bits});
    const Value reversed_color = shader.Emit(Opcode::BitCastF32U32, {reversed_sub});
    shader.Run();
    REQUIRE(unrelated.Inst()->GetOpcode() == Opcode::GetCbufU32);
    REQUIRE(other_color.Inst()->Arg(0) == other_sum);
    REQUIRE(integer_float.Inst()->Arg(0) == integer_sum);
    REQUIRE(reversed_color.Inst()->Arg(0) == reversed_sub);
    RequireBlitWord(blit, 0x10);
}

TEST_CASE("Blit dither preserves root table exclusions before rewriting uses",
          "[video_core][blit_dither]") {
    for (const bool pair : {false, true}) {
        BlitProgram shader;
        const Value bias = shader.Cbuf(17, 0x10);
        const Value bits = shader.ColorBits();
        const Value sum = shader.Emit(Opcode::IAdd32, {bits, bias});
        const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
        const Value root = shader.Emit(pair ? Opcode::GetCbufU32x2 : Opcode::GetCbufU32,
                                       {Value{17U}, Value{pair ? 0x5cU : 0x80U}});
        shader.Run();
        REQUIRE(color.Inst()->Arg(0) == sum);
        REQUIRE(bias.Inst()->GetOpcode() == Opcode::GetCbufU32);
        REQUIRE(root.Inst()->GetOpcode() ==
                (pair ? Opcode::GetCbufU32x2 : Opcode::GetCbufU32));
        REQUIRE_FALSE(shader.program.info.uses_render_area);
    }
    BlitProgram shader;
    const Value zero_word = shader.Cbuf(17, 0x40);
    shader.Run();
    REQUIRE(zero_word.Inst()->GetOpcode() == Opcode::GetCbufU32);
    REQUIRE_FALSE(shader.program.info.uses_render_area);
}

TEST_CASE("Blit dither is limited to fragment bias words and color bitcasts",
          "[video_core][blit_dither]") {
    for (const auto stage : {Shader::Stage::VertexB, Shader::Stage::Fragment, Shader::Stage::Compute}) {
        for (const u32 offset : {0U, 0x8U, 0x1cU}) {
            BlitProgram shader{stage};
            const Value bias = shader.Cbuf(17, offset);
            const Value bits = shader.ColorBits();
            const Value sum = shader.Emit(Opcode::IAdd32, {bits, bias});
            const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
            shader.Run();
            REQUIRE(color.Inst()->Arg(0) == sum);
        }
    }
    for (const auto stage : {Shader::Stage::VertexB, Shader::Stage::Compute}) {
        BlitProgram shader{stage};
        const Value bias = shader.Cbuf(17, 0x10);
        const Value bits = shader.ColorBits();
        const Value sum = shader.Emit(Opcode::IAdd32, {bits, bias});
        const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
        shader.Run();
        REQUIRE(color.Inst()->Arg(0) == sum);
    }
    BlitProgram shader;
    shader.env.native = false;
    const Value bias = shader.Cbuf(17, 0x10);
    const Value bits = shader.ColorBits();
    const Value sum = shader.Emit(Opcode::IAdd32, {bits, bias});
    const Value color = shader.Emit(Opcode::BitCastF32U32, {sum});
    shader.Run();
    REQUIRE(color.Inst()->Arg(0) == sum);
}
