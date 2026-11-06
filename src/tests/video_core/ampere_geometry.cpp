// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/object_pool.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

bool Contains(const Shader::Ampere::LoweredProgram& lowered, u64 instruction) {
    return std::find(lowered.instructions.begin(), lowered.instructions.end(), instruction) !=
           lowered.instructions.end();
}

constexpr std::array<Instruction, 16> GEOMETRY_PROGRAM{{
    {{0x0000734e, 0, 0, 0}},
    {{0x00037802, 0xd0, 0xf00, 0}},
    {{0x00007210, 3, 0x07f1e0ff, 0}},
    {{0xff017224, 0xff, 0x000e0401, 0}},
    {{0x00007357, 0, 0, 0}},
    {{0x00007802, 0, 0xf00, 0}},
    {{0x00017802, 0, 0xf00, 0}},
    {{0x00037802, 0, 0xf00, 0}},
    {{0x0100720c, 0xff, 0x03f03070, 0}},
    {{0x00017923, 0, 0, 0}},          // ISBERD R1, [R0]
    {{0x00027324, 0xff, 0x4000, 0}},  // OUT.EMIT R2, R0, RZ
    {{0x02037924, 0, 0x8000, 0}},     // OUT.CUT R3, R2, 0
    EXIT,
    {{0x00ff7324, 0xff, 0, 0}},
    {{0x00007957, 0, 0, 0}},
    EXIT,
}};
} // namespace

TEST_CASE("Ampere geometry program counter uses the native address", "[video_core][ampere]") {
    const Instruction nop{{0x00007918, 0, 0, 0}};
    const Instruction lepc{{0x0014734e, 0, 0, 0}};
    const std::array code{nop, lepc, EXIT};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(code)));
    REQUIRE_FALSE(LowerToMaxwell(code, true));
    const auto result = LowerToMaxwell(code, {.geometry = true,
                                            .native_code_address = 0x00000012fffffff0ULL});
    REQUIRE(static_cast<bool>(result));
    REQUIRE(result.instructions.size() == 4);
    REQUIRE((result.instructions[1] & 0xff) == 20);
    REQUIRE(((result.instructions[1] >> 20) & 0xffffffffULL) == 0);
    REQUIRE((result.instructions[2] & 0xff) == 21);
    REQUIRE(((result.instructions[2] >> 20) & 0xffffffffULL) == 0x13);

    Instruction unsupported = lepc;
    unsupported.words[1] = 1;
    REQUIRE_FALSE(LowerToMaxwell(std::array{nop, unsupported, EXIT},
                                 {.geometry = true, .native_code_address = 0x00000012fffffff0ULL}));
    unsupported = lepc;
    unsupported.words[0] = (unsupported.words[0] & ~0xff0000U) | (245U << 16);
    REQUIRE_FALSE(LowerToMaxwell(std::array{nop, unsupported, EXIT},
                                 {.geometry = true, .native_code_address = 0x00000012fffffff0ULL}));
}

TEST_CASE("Ampere geometry stage map and exit setup are stage specific", "[video_core][ampere]") {
    const Instruction map_read{{0x05127923, 0, 0, 0}};
    const Instruction exit_setup{{0x03007357, 0, 0, 0}};
    auto code = GEOMETRY_PROGRAM;
    code[9] = map_read;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(code)));
    const auto result = LowerToMaxwell(code, true);
    REQUIRE(static_cast<bool>(result));
    const u64 map = result.instructions[result.source_instruction_offsets[9]];
    REQUIRE((map & 0xff) == 18);
    REQUIRE(((map >> 20) & 0xff) == 5);
    REQUIRE_FALSE(LowerToMaxwell(std::array{map_read, exit_setup, EXIT}, true));

    Instruction unsupported = exit_setup;
    unsupported.words[1] = 1;
    REQUIRE_FALSE(LowerToMaxwell(std::array{unsupported, EXIT}, true));
}

TEST_CASE("Ampere F2I supports 16-bit truncation", "[video_core][ampere]") {
    for (const bool is_signed : {false, true}) {
        const Instruction convert{{0x00067305, 9, 0x0021e800U | (is_signed ? 0x100U : 0), 0}};
        const auto result = LowerToMaxwell(std::array{convert, EXIT});
        REQUIRE(static_cast<bool>(result));
        const auto word = std::ranges::find_if(result.instructions, [](u64 value) {
            return (value & 0xfff0000000000000ULL) == 0x5cb0000000000000ULL;
        });
        REQUIRE(word != result.instructions.end());
        REQUIRE(((*word >> 8) & 3) == 1);
        REQUIRE(((*word >> 12) & 1) == static_cast<u64>(is_signed));
        REQUIRE(((*word >> 39) & 3) == 3);
        REQUIRE(((*word >> 44) & 1) == 1);
    }
    const Instruction unsupported{{0x00067305, 9, 0x0021e801, 0}};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{unsupported, EXIT})));
}

TEST_CASE("Ampere geometry programs lower their output instructions", "[video_core][ampere]") {
    const auto lowered = LowerToMaxwell(GEOMETRY_PROGRAM, true);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(GEOMETRY_PROGRAM));
    for (size_t index = 0; index <= Shader::Ampere::GeometryExitSetupInstructions; ++index) {
        REQUIRE(lowered.source_instruction_offsets[index] == 0);
    }
    REQUIRE(Contains(lowered, 0x5c98078000070001ULL)); // MOV R1, R0
    REQUIRE(Contains(lowered, 0xfbe000800ff70002ULL)); // OUT.EMIT R2, R0, RZ
    REQUIRE(Contains(lowered, 0xf6e0010000070203ULL)); // OUT.CUT R3, R2, 0
    REQUIRE(Contains(lowered, 0xfbe000000ff700ffULL));
    REQUIRE(lowered.instructions.back() == 0xe30000000007000fULL);
}

TEST_CASE("Ampere output instructions stay rejected outside geometry programs",
          "[video_core][ampere]") {
    const auto lowered = LowerToMaxwell(GEOMETRY_PROGRAM);
    REQUIRE(!static_cast<bool>(lowered));
    REQUIRE(lowered.error == "native program counter load outside geometry stage");
    for (const auto& output : {GEOMETRY_PROGRAM[10], GEOMETRY_PROGRAM[11]}) {
        const auto rejected = LowerToMaxwell(std::array{output, EXIT});
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error == "native geometry output outside geometry stage");
    }
}

TEST_CASE("Ampere geometry rejects an unvalidated program counter load", "[video_core][ampere]") {
    const Instruction lepc{{0x0004734e, 0, 0, 0}};
    const auto truncated = LowerToMaxwell(std::array{lepc, EXIT}, true);
    REQUIRE_FALSE(truncated);
    REQUIRE(truncated.error == "native exit envelope truncated");
    auto invalid = GEOMETRY_PROGRAM;
    invalid[0] = lepc;
    const auto rejected = LowerToMaxwell(invalid, true);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error == "native exit handler installation");
}

TEST_CASE("SPIR-V geometry stream zero needs no geometry streams capability",
          "[video_core][ampere][spirv]") {
    namespace IR = Shader::IR;
    Shader::ObjectPool<IR::Inst> inst_pool;
    IR::Block block{inst_pool};
    IR::IREmitter ir{block};
    ir.EmitVertex(ir.Imm32(0));
    ir.EndPrimitive(ir.Imm32(0));
    IR::Program program;
    program.stage = Shader::Stage::Geometry;
    program.output_topology = Shader::OutputTopology::PointList;
    program.output_vertices = 1;
    program.invocations = 1;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    IR::AbstractSyntaxNode block_node{};
    block_node.type = IR::AbstractSyntaxNode::Type::Block;
    block_node.data.block = &block;
    IR::AbstractSyntaxNode return_node{};
    return_node.type = IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {block_node, return_node};
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    profile.support_geometry_streams = false;
    const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(code.size() > 5);
    constexpr u32 OpCapability = 17;
    constexpr u32 OpEmitVertex = 218;
    constexpr u32 OpEndPrimitive = 219;
    constexpr u32 OpEmitStreamVertex = 220;
    constexpr u32 OpEndStreamPrimitive = 221;
    constexpr u32 CapabilityGeometry = 2;
    constexpr u32 CapabilityGeometryStreams = 54;
    u32 emits = 0, ends = 0, geometry = 0;
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        const u32 opcode = code[offset] & 0xffff;
        REQUIRE(words > 0);
        REQUIRE(offset + words <= code.size());
        if (opcode == OpCapability) {
            REQUIRE(words == 2);
            REQUIRE(code[offset + 1] != CapabilityGeometryStreams);
            geometry += code[offset + 1] == CapabilityGeometry;
        }
        emits += opcode == OpEmitVertex;
        ends += opcode == OpEndPrimitive;
        REQUIRE(opcode != OpEmitStreamVertex);
        REQUIRE(opcode != OpEndStreamPrimitive);
        offset += words;
    }
    REQUIRE(emits == 1);
    REQUIRE(ends == 1);
    REQUIRE(geometry == 1);
}
