// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <algorithm>
#include <cstring>
#include <span>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::TessellationStage;
namespace IR = Shader::IR;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr std::array<u32, 20> TennisHeader{{
    0x000c0881, 0x07000000, 0x04000000, 0x70000000,
    0x000ff000, 0xf0000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x0000f000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
}};

constexpr std::array<Instruction, 79> TennisControl{{
    {{0x00007919, 0x00000000, 0x00001100, 0x000e2800}},
    {{0x00017919, 0x00000000, 0x00001d00, 0x000e6200}},
    {{0x00007824, 0x00000004, 0x078e00ff, 0x001fe200}},
    {{0x01057816, 0x00007772, 0x000000ff, 0x042fe400}},
    {{0x01047816, 0x00007770, 0x000000ff, 0x000fe400}},
    {{0xff007819, 0x00000002, 0x00011400, 0x000fc400}},
    {{0x0500780c, 0x00000002, 0x03f23070, 0x040fe200}},
    {{0x05087224, 0x00000004, 0x078e02ff, 0x040fe200}},
    {{0x0500720c, 0x00000000, 0x03f03070, 0x040fe200}},
    {{0x05067224, 0x00000004, 0x078e0200, 0x0c0fe400}},
    {{0x05097424, 0x00000002, 0x078e0204, 0x000fc600}},
    {{0x06017923, 0x00000000, 0x00000000, 0x000e2e00}},
    {{0xff010424, 0x80000000, 0x078e00ff, 0x001fe200}},
    {{0x0500720c, 0x000000ff, 0x03f03070, 0x000fca00}},
    {{0xff007321, 0x00007001, 0x00000c00, 0x000e2800}},
    {{0xff007322, 0x00007000, 0x00000cff, 0x001fe800}},
    {{0x080a7923, 0x00000000, 0x00000000, 0x000e2800}},
    {{0x090b7923, 0x00000000, 0x00000000, 0x000e6200}},
    {{0x000a0802, 0x80000000, 0x00000f00, 0x001fe200}},
    {{0xff0b1424, 0x80000000, 0x078e00ff, 0x002fc800}},
    {{0xff047321, 0x0000700a, 0x00000000, 0x000fe800}},
    {{0xff057321, 0x0000780a, 0x00000000, 0x000fe800}},
    {{0xff077321, 0x0000700b, 0x00000000, 0x000e2800}},
    {{0xff067321, 0x0000780b, 0x00000000, 0x000e6200}},
    {{0x04047221, 0x00000007, 0x00010000, 0x001fe200}},
    {{0x05057221, 0x00000006, 0x00010000, 0x002fc600}},
    {{0x04017820, 0x3f000000, 0x00410000, 0x000fe200}},
    {{0x05007820, 0x3f000000, 0x00410000, 0x000fc600}},
    {{0x01027621, 0x03c06400, 0x00010100, 0x000fe200}},
    {{0x00057802, 0x3b2aaaab, 0x00000f00, 0x000fe200}},
    {{0x00037621, 0x03c06500, 0x00010100, 0x000fca00}},
    {{0x02027209, 0x40000003, 0x07810200, 0x000fca00}},
    {{0x02027423, 0x3f2b0000, 0x00010005, 0x000fca00}},
    {{0x02027809, 0x40000000, 0x03810000, 0x000fcc00}},
    {{0x00027305, 0x00000002, 0x00207100, 0x000e2400}},
    {{0x02037811, 0x00000160, 0x078e20ff, 0x001fe400}},
    {{0x00057245, 0x00000002, 0x00201400, 0x000fe400}},
    {{0x03087b82, 0x03c00000, 0x00000a00, 0x000e2400}},
    {{0x00067305, 0x00000005, 0x00212800, 0x000e6200}},
    {{0x00047623, 0x03c06700, 0x00010009, 0x001fe200}},
    {{0x01077623, 0x03c06600, 0x00010008, 0x000fc800}},
    {{0x060a7b60, 0xb0800a04, 0x009e0709, 0x002f4200}},
    {{0x06087b60, 0xb0800804, 0x009e01ff, 0x000f4200}},
    {{0x00007621, 0x83c04200, 0x00010000, 0x000fe200}},
    {{0x01017621, 0x83c04000, 0x00010000, 0x000fc600}},
    {{0x00007220, 0x00000000, 0x00410000, 0x000fc800}},
    {{0x01007223, 0x00000001, 0x00010000, 0x000fcc00}},
    {{0x00007308, 0x00000000, 0x00002000, 0x000fe200}},
    {{0x0000791a, 0x0000d040, 0x00000000, 0x000fc800}},
    {{0x0900780b, 0x3f000000, 0x03f1e000, 0x000fc800}},
    {{0x0a0a7208, 0x0000000b, 0x04000000, 0x000fca00}},
    {{0x0a0a7820, 0x437f0000, 0x00410000, 0x000fcc00}},
    {{0x000a7305, 0x0000000a, 0x0021f100, 0x000e2400}},
    {{0x0a017812, 0x0000003f, 0x078ec0ff, 0x001fc800}},
    {{0x01027811, 0x00010000, 0x078e18ff, 0x000fcc00}},
    {{0x02027b82, 0x00000000, 0x00008800, 0x000e2200}},
    {{0x08087621, 0x83c04100, 0x00010000, 0x020fca00}},
    {{0x0801780a, 0x42c80000, 0x0381e200, 0x000fca00}},
    {{0x01017a23, 0x0400a000, 0x00010000, 0x000fc800}},
    {{0x01037621, 0x0400a000, 0x00010100, 0x000fcc00}},
    {{0x00037308, 0x00000003, 0x00000c00, 0x000e6400}},
    {{0xff007322, 0x00001803, 0x000010ff, 0x002fe800}},
    {{0xff057321, 0x000018ff, 0x00009000, 0x000e6200}},
    {{0x02017810, 0xffffffff, 0x07ffe0ff, 0x001fc800}},
    {{0xff017217, 0x00000001, 0x07800200, 0x000fc800}},
    {{0x01017819, 0x00000004, 0x000006ff, 0x000fcc00}},
    {{0x01017b82, 0x04000000, 0x00000800, 0x000e2200}},
    {{0x00007305, 0x00000005, 0x0021f100, 0x002e6400}},
    {{0x00007245, 0x00000000, 0x00201400, 0x002fcc00}},
    {{0x00007308, 0x00000000, 0x00000800, 0x000e6400}},
    {{0xff047209, 0x00000000, 0x07810000, 0x002fc800}},
    {{0x04047209, 0x00000001, 0x03810000, 0x001fca00}},
    {{0xff007322, 0x00000004, 0x000010ff, 0x000fe800}},
    {{0xff007322, 0x00000404, 0x000010ff, 0x000fe800}},
    {{0xff007322, 0x00000804, 0x000010ff, 0x000fe800}},
    {{0xff007322, 0x00000c04, 0x000010ff, 0x000fe800}},
    {{0xff007322, 0x00001004, 0x000010ff, 0x000fe800}},
    {{0xff007322, 0x00001404, 0x000010ff, 0x000fe200}},
    {{0x0000794d, 0x00000000, 0x03800000, 0x000fea00}},
}};

// Run3 logged only these five header words; the test environment zero-fills the unknown remainder.
constexpr std::array<u32, 5> TennisEvaluationHeaderPrefix{0x000c0c81, 0, 0, 0, 0xbd0bc000};

constexpr std::array<Instruction, 47> TennisEvaluation{{
    {{0x00007919, 0x00000000, 0x00001d00, 0x000e2800}},
    {{0xff0c7321, 0x000018ff, 0x00001000, 0x000e6200}},
    {{0x00017816, 0x00007772, 0x000000ff, 0x041fe400}},
    {{0x00007816, 0x00007770, 0x000000ff, 0x000fe400}},
    {{0x0100720c, 0x000000ff, 0x03f03070, 0x040fe400}},
    {{0x0100780c, 0x00000001, 0x03f23070, 0x040fe200}},
    {{0x01027224, 0x00000000, 0x078e02ff, 0x0c0fe200}},
    {{0x0100780c, 0x00000002, 0x03f43070, 0x040fe200}},
    {{0x01037424, 0x00000001, 0x078e0200, 0x0c0fe200}},
    {{0x0100780c, 0x00000003, 0x03f63070, 0x040fe200}},
    {{0x01047424, 0x00000002, 0x078e0200, 0x0c0fe200}},
    {{0x020a7923, 0x00000000, 0x00000000, 0x000e2200}},
    {{0x01057424, 0x00000003, 0x078e0200, 0x000fc600}},
    {{0x030b7923, 0x00000000, 0x00000000, 0x000ea800}},
    {{0x040d7923, 0x00000000, 0x00000000, 0x000ee800}},
    {{0x050e7923, 0x00000000, 0x00000000, 0x000f2800}},
    {{0x00007919, 0x00000000, 0x00000000, 0x000f6800}},
    {{0xff007322, 0x0000800c, 0x000000ff, 0x002fe200}},
    {{0x000a0802, 0x80000000, 0x00000f00, 0x001fc400}},
    {{0x000b1802, 0x80000000, 0x00000f00, 0x004fc600}},
    {{0xff037321, 0x0000700a, 0x00000000, 0x000fe200}},
    {{0x000d2802, 0x80000000, 0x00000f00, 0x008fc600}},
    {{0xff027321, 0x0000700b, 0x00000000, 0x000e2200}},
    {{0x000e3802, 0x80000000, 0x00000f00, 0x010fc600}},
    {{0xff067321, 0x0000700d, 0x00000000, 0x000fe800}},
    {{0xff097321, 0x0000700e, 0x00000000, 0x000e6800}},
    {{0xff057321, 0x0000780a, 0x00000000, 0x000fe800}},
    {{0xff047321, 0x0000780b, 0x00000000, 0x000ea800}},
    {{0xff077321, 0x0000780d, 0x00000000, 0x000fe800}},
    {{0xff087321, 0x0000780e, 0x00000000, 0x000ee200}},
    {{0x03027221, 0x00000002, 0x00010100, 0x001fc600}},
    {{0xff007321, 0x0002f000, 0x00008400, 0x020e2200}},
    {{0x06067221, 0x80000009, 0x00010000, 0x002fe200}},
    {{0x05047221, 0x00000004, 0x00010100, 0x004fe200}},
    {{0x07077221, 0x80000008, 0x00010000, 0x108fe200}},
    {{0x00027223, 0x00000002, 0x00010003, 0x041fe200}},
    {{0x00097223, 0x00000006, 0x00010009, 0x040fe200}},
    {{0x00047223, 0x00000004, 0x00010005, 0x040fe200}},
    {{0x00077223, 0x00000007, 0x00010008, 0x000fe200}},
    {{0x000b7202, 0x00000001, 0x00000f00, 0x000fe200}},
    {{0x02097221, 0x00000009, 0x00010100, 0x000fe400}},
    {{0x04077221, 0x00000007, 0x00010100, 0x000fc400}},
    {{0x01087223, 0x00000009, 0x00010002, 0x040fe200}},
    {{0x00097202, 0x00000000, 0x00000f00, 0x000fe200}},
    {{0x010a7223, 0x00000007, 0x00010004, 0x000fca00}},
    {{0xff007322, 0x00007008, 0x00000cff, 0x000fe200}},
    {{0x0000794d, 0x00000000, 0x03800000, 0x000fea00}},
}};

class TessellationEnvironment final : public Shader::Environment {
public:
    TessellationEnvironment(std::span<const u64> code_, Shader::Stage stage_,
                            std::span<const u32> header = TennisHeader) : code{code_} {
        stage = stage_;
        is_proprietary_driver = true;
        std::memcpy(&sph, header.data(), (std::min)(header.size_bytes(), sizeof(sph)));
        if (stage == Shader::Stage::TessellationEval) {
            sph.common0.shader_type.Assign(3);
        }
    }
    u64 ReadInstruction(u32 address) override {
        for (u32 index = 0; index < code.size(); ++index) {
            if (address == 0x50 + index * 8 + ((index + 1) / 3) * 8) {
                return code[index];
            }
        }
        FAIL("Read outside captured tessellation program at " << address);
        return 0;
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override { return (bank << 16) | offset; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 15; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return true; }
    bool IsNativeProgram() const override { return true; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    std::span<const u64> code;
};
} // namespace

TEST_CASE("Captured Mario Tennis Fever control shader preserves its patch outputs",
          "[video_core][ampere][tessellation]") {
    const auto lowered = LowerToMaxwell(TennisControl, false, true, {}, TessellationStage::Control);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(TennisControl));
    for (const u32 offset : {0xb0U, 0x100U, 0x110U}) {
        const auto& source = TennisControl[offset / 16];
        const u64 word = lowered.instructions[lowered.source_instruction_offsets[offset / 16]];
        REQUIRE((word >> 48) == 0x5c98);
        REQUIRE((word & 0xff) == source.Bits(16, 8));
        REQUIRE(((word >> 20) & 0xff) == source.Bits(24, 8));
    }
    for (const u32 offset : {0x3d0U, 0x3e0U, 0x480U, 0x490U, 0x4a0U, 0x4b0U, 0x4c0U, 0x4d0U}) {
        const auto& source = TennisControl[offset / 16];
        const u64 word = lowered.instructions[lowered.source_instruction_offsets[offset / 16]];
        REQUIRE(((word >> 31) & 1) == 1);
        REQUIRE(((word >> 20) & 0x3ff) == source.Bits(40, 10));
        REQUIRE(((word >> 32) & 1) == source.Bits(79, 1));
    }

    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationControl};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.invocations == 4);
    REQUIRE(program.info.stores_tess_level_outer);
    REQUIRE(program.info.stores_tess_level_inner);
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE(program.info.uses_invocation_id);
    REQUIRE(program.info.uses_invocation_info);
    std::array<bool, 7> patches{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : *block) {
            if (inst.GetOpcode() == IR::Opcode::SetPatch) {
                const auto patch = static_cast<size_t>(inst.Arg(0).Patch());
                REQUIRE(patch < patches.size());
                patches[patch] = true;
            }
        }
    }
    REQUIRE(std::all_of(patches.begin(), patches.end(), [](bool written) { return written; }));
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Native tessellation patch loads select the correct input or output namespace",
          "[video_core][ampere][tessellation]") {
    const Instruction control_load{{0xff057321, 0x000018ff, 0x00009000, 0x000e6200}};
    auto eval_load = control_load;
    eval_load.words[2] &= ~(1U << 15);
    const Instruction store{{0xff007322, 0x00001803, 0x000010ff, 0x002fe800}};
    REQUIRE(LowerToMaxwell(std::array{control_load, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE(LowerToMaxwell(std::array{eval_load, Exit}, false, false, {}, TessellationStage::Evaluation));
    REQUIRE(LowerToMaxwell(std::array{store, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE_FALSE(LowerToMaxwell(std::array{control_load, Exit}, false, false, {}, TessellationStage::Evaluation));
    REQUIRE_FALSE(LowerToMaxwell(std::array{eval_load, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE_FALSE(LowerToMaxwell(std::array{store, Exit}, false, false, {}, TessellationStage::Evaluation));
    for (const auto instruction : {control_load, eval_load, store}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
        REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}, true));
    }
    auto physical = control_load;
    physical.words[2] |= 1U << 13;
    REQUIRE_FALSE(LowerToMaxwell(std::array{physical, Exit}, false, false, {}, TessellationStage::Control));
    auto vertex_output = control_load;
    vertex_output.words[2] &= ~(1U << 12);
    REQUIRE_FALSE(LowerToMaxwell(std::array{vertex_output, Exit}, false, false, {}, TessellationStage::Control));
}

TEST_CASE("Native tessellation evaluation patch inputs reach SPIRV",
          "[video_core][ampere][tessellation]") {
    const std::array code{
        Instruction{{0xff057321, 0x000018ff, 0x00001000, 0}},
        Instruction{{0xff007322, 0x00007005, 0x000000ff, 0}},
        Exit,
    };
    const auto lowered = LowerToMaxwell(code, false, false, {}, TessellationStage::Evaluation);
    INFO(lowered.error);
    REQUIRE(lowered);
    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationEval};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE_FALSE(program.info.stores_tess_level_outer);
    REQUIRE_FALSE(program.info.stores_tess_level_inner);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Native tessellation vertex handles retain only supported ISBERD map reads",
          "[video_core][ampere][tessellation]") {
    const Instruction read{{0x06017923, 0, 0, 0x000e2e00}};
    for (const auto stage : {TessellationStage::Control, TessellationStage::Evaluation}) {
        const auto lowered = LowerToMaxwell(std::array{read, Exit}, false, false, {}, stage);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions[0] == 0x5c98078000670001ULL);
        for (const auto invalid : std::array{
                 Instruction{{0x06017923, 0x100, 0, 0}},
                 Instruction{{0x06017923, 0, 0x8000, 0}},
                 Instruction{{0x06017923, 0, 0x4000, 0}},
                 Instruction{{0x06017927, 0, 0, 0}},
             }) {
            REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}, false, false, {}, stage));
        }
    }
}

TEST_CASE("Captured Mario Tennis Fever evaluation shader reads both tessellation coordinates",
          "[video_core][ampere][tessellation][tennis_evaluation]") {
    const auto lowered = LowerToMaxwell(TennisEvaluation, false, true, {}, TessellationStage::Evaluation);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(TennisEvaluation));
    const u64 coordinates = lowered.instructions[lowered.source_instruction_offsets[0x1f0 / 16]];
    REQUIRE(((coordinates >> 20) & 0x3ff) == 0x2f0);
    REQUIRE(((coordinates >> 31) & 1) == 0);
    REQUIRE(((coordinates >> 32) & 1) == 1);
    REQUIRE(((coordinates >> 47) & 3) == 1);
    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationEval,
                               TennisEvaluationHeaderPrefix};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.info.loads[IR::Attribute::TessellationEvaluationPointU]);
    REQUIRE(program.info.loads[IR::Attribute::TessellationEvaluationPointV]);
    REQUIRE(program.info.stores.AllComponents(IR::Attribute::PositionX));
    REQUIRE(program.info.stores[IR::Attribute::Generic0X]);
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE(program.info.uses_invocation_info);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    Shader::RuntimeInfo runtime{};
    // The capture has no runtime tessellation state; quads are a synthetic test context.
    runtime.tess_primitive = Shader::TessPrimitive::Quads;
    Shader::Backend::Bindings bindings{};
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Tessellation output-coordinate loads remain confined to evaluation U and V",
          "[video_core][ampere][tessellation][tennis_evaluation]") {
    const Instruction coordinates{{0xff007321, 0x0002f000, 0x00008400, 0x020e2200}};
    for (const auto stage : {TessellationStage::None, TessellationStage::Control}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{coordinates, Exit}, false, false, {}, stage));
    }
    REQUIRE_FALSE(LowerToMaxwell(std::array{coordinates, Exit}, true));
    for (const Instruction invalid : std::array{
             Instruction{{0xff007321, 0x0002f800, 0x00008000, 0}},
             Instruction{{0xff007321, 0x0002f000, 0x00008800, 0}},
             Instruction{{0xff007321, 0x0002f400, 0x00008400, 0}},
             Instruction{{0xff007321, 0x0002f000, 0x0000a400, 0}},
             Instruction{{0x01007321, 0x0002f000, 0x00008400, 0}},
             Instruction{{0xff007321, 0x00007000, 0x00008400, 0}},
         }) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}, false, false, {}, TessellationStage::Evaluation));
    }
    const Instruction v{{0xff007321, 0x0002f400, 0x00008000, 0}};
    REQUIRE(LowerToMaxwell(std::array{v, Exit}, false, false, {}, TessellationStage::Evaluation));
}
