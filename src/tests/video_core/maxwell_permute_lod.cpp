// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <cmath>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace {
namespace IR = Shader::IR;

class TestEnvironment final : public Shader::Environment {
public:
    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32) override { return cbuf; }
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
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

    u32 cbuf{};
};

struct Translation {
    TestEnvironment env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    std::array<u32, IR::NUM_REGS> regs{};
    std::array<f32, 4> lod{2.75f, -1.5f, 0.0f, 0.0f};

    void Run() {
        using IR::Opcode;
        using Words = std::array<u32, 4>;
        std::unordered_map<const IR::Inst*, Words> values;
        auto read = [&](IR::Value value) -> Words {
            if (!value.IsImmediate()) {
                return values.at(value.InstRecursive());
            }
            if (value.Type() == IR::Type::F32) {
                return {std::bit_cast<u32>(value.F32())};
            }
            return {value.U32()};
        };
        for (const IR::Inst& inst : block) {
            auto arg = [&](size_t index) { return read(inst.Arg(index))[0]; };
            Words result{};
            switch (inst.GetOpcode()) {
            case Opcode::GetRegister:
                result[0] = regs.at(static_cast<size_t>(inst.Arg(0).Reg()));
                break;
            case Opcode::SetRegister:
                regs.at(static_cast<size_t>(inst.Arg(0).Reg())) = arg(1);
                break;
            case Opcode::GetCbufU32:
                result[0] = env.ReadCbufValue(arg(0), arg(1));
                break;
            case Opcode::BitCastU32F32:
            case Opcode::BitCastF32U32:
                result[0] = arg(0);
                break;
            case Opcode::IAdd32:
                result[0] = arg(0) + arg(1);
                break;
            case Opcode::BitwiseAnd32:
                result[0] = arg(0) & arg(1);
                break;
            case Opcode::ShiftLeftLogical32:
                result[0] = arg(0) << arg(1);
                break;
            case Opcode::ULessThan:
                result[0] = arg(0) < arg(1);
                break;
            case Opcode::INotEqual:
                result[0] = arg(0) != arg(1);
                break;
            case Opcode::SelectU32:
                result[0] = arg(0) ? arg(1) : arg(2);
                break;
            case Opcode::BitFieldUExtract:
            case Opcode::BitFieldSExtract: {
                const u32 width = arg(2);
                const u32 bits = (arg(0) >> arg(1)) & ((1U << width) - 1);
                result[0] = inst.GetOpcode() == Opcode::BitFieldSExtract
                                ? static_cast<u32>(static_cast<s32>(bits << (32 - width)) >>
                                                   (32 - width))
                                : bits;
                break;
            }
            case Opcode::BitFieldInsert: {
                const u32 mask = ((1U << arg(3)) - 1) << arg(2);
                result[0] = (arg(0) & ~mask) | ((arg(1) << arg(2)) & mask);
                break;
            }
            case Opcode::CompositeConstructF32x2:
            case Opcode::CompositeConstructF32x3:
                for (size_t i = 0; i < inst.NumArgs(); ++i) {
                    result[i] = arg(i);
                }
                break;
            case Opcode::BoundImageQueryLod:
            case Opcode::BindlessImageQueryLod:
                for (size_t i = 0; i < lod.size(); ++i) {
                    result[i] = std::bit_cast<u32>(lod[i]);
                }
                break;
            case Opcode::CompositeExtractF32x4:
                result[0] = read(inst.Arg(0)).at(arg(1));
                break;
            case Opcode::FPMul32:
                result[0] = std::bit_cast<u32>(std::bit_cast<f32>(arg(0)) *
                                               std::bit_cast<f32>(arg(1)));
                break;
            case Opcode::ConvertS32F32:
                result[0] = static_cast<u32>(static_cast<s32>(std::bit_cast<f32>(arg(0))));
                break;
            case Opcode::ConvertU32F32:
                REQUIRE(std::bit_cast<f32>(arg(0)) >= 0.0f);
                result[0] = static_cast<u32>(std::bit_cast<f32>(arg(0)));
                break;
            default:
                FAIL("Unexpected translated opcode " << IR::NameOf(inst.GetOpcode()));
            }
            values.emplace(&inst, result);
        }
    }
};

u32 Permute(u32 a, u32 selector, u32 c) {
    const std::array<u8, 8> bytes{
        static_cast<u8>(a),       static_cast<u8>(a >> 8),  static_cast<u8>(a >> 16),
        static_cast<u8>(a >> 24), static_cast<u8>(c),       static_cast<u8>(c >> 8),
        static_cast<u8>(c >> 16), static_cast<u8>(c >> 24)};
    u32 result = 0;
    for (u32 i = 0; i < 4; ++i) {
        const u32 nibble = (selector >> (i * 4)) & 15;
        const u8 byte = bytes[nibble % 8];
        const u8 selected = nibble < 8 ? byte : (byte >= 128 ? 255 : 0);
        result |= static_cast<u32>(selected) << (i * 8);
    }
    return result;
}
} // namespace

TEST_CASE("Maxwell PRMT default mode selects and sign-replicates bytes",
          "[video_core][maxwell][prmt]") {
    Translation test;
    test.visitor.PRMT_reg(1ULL << 8 | 2ULL << 20 | 3ULL << 39);
    constexpr u32 a = 0x817f00ff;
    constexpr u32 c = 0x01fe80aa;
    for (u32 selector = 0; selector < 0x10000; ++selector) {
        test.regs[1] = a;
        test.regs[2] = selector;
        test.regs[3] = c;
        test.Run();
        CAPTURE(selector);
        REQUIRE(test.regs[0] == Permute(a, selector, c));
    }
}

TEST_CASE("Maxwell PRMT operand forms and overlapping destination preserve inputs",
          "[video_core][maxwell][prmt]") {
    constexpr u32 a = 0x817f00ff;
    constexpr u32 c = 0x01fe80aa;
    constexpr u32 selector = 0xfa50;
    for (u32 form = 0; form < 4; ++form) {
        Translation test;
        test.regs[1] = a;
        test.regs[3] = c;
        const u64 common = 1ULL | 1ULL << 8 | 3ULL << 39;
        switch (form) {
        case 0:
            test.regs[2] = selector;
            test.visitor.PRMT_reg(common | 2ULL << 20);
            break;
        case 1:
            test.regs[3] = selector;
            test.env.cbuf = c;
            test.visitor.PRMT_rc(common | 5ULL << 34 | 0x40ULL << 20);
            break;
        case 2:
            test.env.cbuf = selector;
            test.visitor.PRMT_cr(common | 5ULL << 34 | 0x40ULL << 20);
            break;
        case 3:
            test.visitor.PRMT_imm(common | static_cast<u64>(selector) << 20);
            break;
        }
        test.Run();
        CAPTURE(form);
        REQUIRE(test.regs[1] == Permute(a, selector, c));
    }
    for (u64 mode = 1; mode < 8; ++mode) {
        Translation test;
        REQUIRE_THROWS_AS(test.visitor.PRMT_reg(mode << 48), Shader::NotImplementedException);
    }
}

TEST_CASE("Maxwell TMML packs fractional relative and absolute LOD in guest order",
          "[video_core][maxwell][tmml]") {
    for (bool bindless : {false, true}) {
        for (u32 type : {0U, 1U, 2U, 3U, 4U, 6U, 7U}) {
            for (u32 mask : {1U, 2U, 3U}) {
                Translation test;
                const u64 insn = 10ULL | 1ULL << 8 | 8ULL << 20 |
                                 static_cast<u64>(type) << 28 | static_cast<u64>(mask) << 31;
                if (bindless) {
                    test.visitor.TMML_b(insn);
                } else {
                    test.visitor.TMML(insn);
                }
                test.Run();
                CAPTURE(bindless, type, mask);
                if (mask & 1) {
                    REQUIRE(test.regs[10] == 0xfe80);
                }
                if (mask & 2) {
                    REQUIRE(test.regs[mask == 3 ? 11 : 10] == 0x02c0);
                }
            }
        }
    }
}

TEST_CASE("Generic attribute classification includes all components and excludes neighbours",
          "[video_core][shader][attribute]") {
    for (u32 slot = 0; slot < 32; ++slot) {
        for (u32 component = 0; component < 4; ++component) {
            const auto attribute = IR::Attribute::Generic0X + slot * 4 + component;
            REQUIRE(IR::IsGeneric(attribute));
            REQUIRE(IR::GenericAttributeIndex(attribute) == slot);
            REQUIRE(IR::GenericAttributeElement(attribute) == component);
        }
    }
    for (const auto attribute : {static_cast<IR::Attribute>(u32(IR::Attribute::Generic0X) - 1),
                                 IR::Attribute::Generic31W + 1}) {
        REQUIRE_FALSE(IR::IsGeneric(attribute));
        REQUIRE_THROWS_AS(IR::GenericAttributeIndex(attribute), Shader::InvalidArgument);
        REQUIRE_THROWS_AS(IR::GenericAttributeElement(attribute), Shader::InvalidArgument);
    }
}
