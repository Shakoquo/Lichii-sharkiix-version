// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace {
namespace IR = Shader::IR;

class SurfaceEnvironment final : public Shader::Environment {
public:
    u64 ReadInstruction(u32) override { throw Shader::LogicError("Unexpected instruction read"); }
    u32 ReadCbufValue(u32, u32) override { throw Shader::LogicError("Unexpected cbuf read"); }
    Shader::TextureType ReadTextureType(u32) override {
        throw Shader::LogicError("Unexpected texture type read");
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        throw Shader::LogicError("Unexpected texture format read");
    }
    bool IsTexturePixelFormatInteger(u32) override {
        throw Shader::LogicError("Unexpected texture format read");
    }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

struct SurfaceTranslation {
    SurfaceEnvironment env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    Shader::Maxwell::TranslatorVisitor visitor{env, block};

    void CheckStore(u64 instruction, unsigned components) {
        REQUIRE_NOTHROW(visitor.SUST(instruction));
        const auto& store{block.back()};
        REQUIRE(store.GetOpcode() == IR::Opcode::BoundImageWrite);
        CHECK(store.Arg(0).U32() == ((instruction >> 36) & 0x1fff) * 4);
        const auto info{store.Flags<IR::TextureInstInfo>()};
        CHECK(info.type == Shader::TextureType::Color2D);
        CHECK(info.image_format == Shader::ImageFormat::Typeless);

        const auto* color{store.Arg(2).Inst()};
        REQUIRE(color->GetOpcode() == IR::Opcode::CompositeConstructU32x4);
        const auto data_reg{static_cast<IR::Reg>(instruction & 255)};
        for (unsigned component = 0; component < 4; ++component) {
            CAPTURE(component);
            const auto value{color->Arg(component)};
            if (component < components) {
                REQUIRE_FALSE(value.IsImmediate());
                REQUIRE(value.Inst()->GetOpcode() == IR::Opcode::GetRegister);
                CHECK(value.Inst()->Arg(0).Reg() == data_reg + static_cast<int>(component));
            } else {
                REQUIRE(value.IsImmediate());
                CHECK(value.U32() == 0);
            }
        }

        unsigned stores{};
        unsigned register_reads{};
        for (const auto& inst : block) {
            switch (inst.GetOpcode()) {
            case IR::Opcode::BoundImageWrite:
                ++stores;
                break;
            case IR::Opcode::GetRegister:
                ++register_reads;
                break;
            case IR::Opcode::CompositeConstructU32x2:
            case IR::Opcode::CompositeConstructU32x4:
                break;
            default:
                FAIL("Unexpected instruction in surface store lowering");
            }
        }
        CHECK(stores == 1);
        CHECK(register_reads == components + 2);
    }
};
} // namespace

TEST_CASE("SUST.P lowers PTX-sized vectors without reading omitted channels",
          "[video_core][fast_fusion_surface_store]") {
    constexpr std::array instructions{
        0xeb28053600170200ULL,
        0xeb28054600370402ULL,
        0xeb28056600f70204ULL,
    };
    constexpr std::array components{1U, 2U, 4U};
    for (size_t index = 0; index < instructions.size(); ++index) {
        CAPTURE(index);
        SurfaceTranslation translation;
        translation.CheckStore(instructions[index], components[index]);
    }
}

TEST_CASE("Partial SUST.P reads only its declared source register footprint",
          "[video_core][fast_fusion_surface_store]") {
    SECTION("Scalar source at R254") {
        SurfaceTranslation translation;
        translation.CheckStore(0xeb280536001702feULL, 1);
    }
    SECTION("Pair source at R252") {
        SurfaceTranslation translation;
        translation.CheckStore(0xeb280546003704fcULL, 2);
    }
}

TEST_CASE("SUST.P rejects masks not established by PTX scalar or vector stores",
          "[video_core][fast_fusion_surface_store]") {
    for (u64 mask = 0; mask < 16; ++mask) {
        if (mask == 1 || mask == 3 || mask == 15) {
            continue;
        }
        CAPTURE(mask);
        SurfaceTranslation translation;
        const u64 instruction{(0xeb28053600170200ULL & ~(u64{15} << 20)) | (mask << 20)};
        REQUIRE_THROWS_AS(translation.visitor.SUST(instruction), Shader::NotImplementedException);
        for (const auto& inst : translation.block) {
            CHECK(inst.GetOpcode() != IR::Opcode::BoundImageWrite);
            CHECK(inst.GetOpcode() != IR::Opcode::BindlessImageWrite);
            CHECK(inst.GetOpcode() != IR::Opcode::BoundImageRead);
            CHECK(inst.GetOpcode() != IR::Opcode::BindlessImageRead);
        }
    }
}
