// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;

class TestEnvironment final : public Shader::Environment {
public:
    bool native{};
    TestEnvironment() { stage = Shader::Stage::Compute; }
    bool IsNativeProgram() const override { return native; }
    bool IsCbufStorageBufferPointer(u32, u32) override { return true; }
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
    std::array<u32, 3> WorkgroupSize() const override { return {32, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

IR::U32 Word(IR::IREmitter& ir, u32 offset) { return ir.GetCbuf(ir.Imm32(0), ir.Imm32(offset)); }

IR::Inst* Phi(IR::Block& block) {
    block.AppendNewInst(IR::Opcode::Phi, {});
    auto* phi = &block.back();
    phi->SetFlags(IR::Type::U32);
    return phi;
}

IR::U64 Address(IR::IREmitter& ir, IR::U32 low, IR::U32 high) {
    return ir.PackUint2x32(ir.CompositeConstruct(low, high));
}

std::vector<u32> Offsets(IR::Program& program, bool native = false) {
    TestEnvironment env;
    env.native = native;
    Shader::HostTranslateInfo host{};
    host.min_ssbo_alignment = 16;
    Shader::Optimization::GlobalMemoryToStorageBufferPass(env, program, host);
    std::vector<u32> offsets;
    for (const auto& descriptor : program.info.storage_buffers_descriptors) {
        offsets.push_back(descriptor.cbuf_offset);
    }
    return offsets;
}

struct ConditionalPointer {
    bool native{};
    Shader::ObjectPool<IR::Inst> pool{256};
    IR::Block entry{pool};
    IR::Block definition{pool};
    IR::Block merge{pool};
    IR::Block access{pool};
    IR::Block exit{pool};
    IR::Program program;

    std::vector<u32> Run(bool same, bool negate, u32 packed, u32 old_base = 0x160,
                         u32 new_base = 0x168, bool deeper = false) {
        program.stage = Shader::Stage::Compute;
        program.blocks = {&entry, &definition, &merge, &access, &exit};
        program.post_order_blocks = {&exit, &access, &merge, &definition, &entry};
        for (u32 index = 0; index < program.blocks.size(); ++index) {
            program.blocks[index]->SetOrder(index);
        }
        entry.AddBranch(&definition);
        entry.AddBranch(&merge);
        definition.AddBranch(&merge);
        merge.AddBranch(&access);
        merge.AddBranch(&exit);
        access.AddBranch(&exit);
        IR::IREmitter start{entry};
        const auto predicate = start.ILessThan(start.LocalInvocationIdX(), start.Imm32(16), false);
        const auto old_low = Word(start, old_base);
        const auto old_high = Word(start, old_base + 4);
        const auto update_guard = start.ConditionRef(predicate);
        IR::IREmitter update{definition};
        auto new_low = Word(update, new_base);
        const auto new_high = Word(update, new_base + 4);
        if (deeper) {
            new_low = update.IAdd(new_low, update.Imm32(16));
        }
        auto* low = Phi(merge);
        auto* high = Phi(merge);
        low->AddPhiOperand(&entry, old_low);
        low->AddPhiOperand(&definition, new_low);
        high->AddPhiOperand(&entry, old_high);
        high->AddPhiOperand(&definition, new_high);
        IR::IREmitter select{merge};
        IR::U1 access_predicate =
            same ? predicate
                 : select.ILessThan(select.LocalInvocationIdY(), select.Imm32(4), false);
        if (packed == 1) {
            const auto saved = select.BitwiseOr(
                select.Imm32(0),
                IR::U32{select.Select(access_predicate, select.Imm32(32), select.Imm32(0))});
            const auto bit = select.BitFieldExtract(saved, select.Imm32(5), select.Imm32(1), false);
            access_predicate = select.LogicalNot(select.GetZeroFromOp(bit));
        } else if (packed == 2) {
            auto* mask = Phi(merge);
            mask->AddPhiOperand(&entry, start.Imm32(0));
            mask->AddPhiOperand(&definition, update.Imm32(64));
            access_predicate = select.INotEqual(
                select.BitwiseAnd(IR::U32{IR::Value{mask}}, select.Imm32(64)), select.Imm32(0));
        }
        if (negate)
            access_predicate = select.LogicalNot(access_predicate);
        const auto access_guard = select.ConditionRef(access_predicate);
        IR::IREmitter store{access};
        store.WriteGlobal32(Address(store, IR::U32{IR::Value{low}}, IR::U32{IR::Value{high}}),
                            store.Imm32(7));
        program.syntax_list = {
            {.data{.block = &entry}, .type = IR::AbstractSyntaxNode::Type::Block},
            {.data{.if_node{update_guard, &definition, &merge}},
             .type = IR::AbstractSyntaxNode::Type::If},
            {.data{.block = &definition}, .type = IR::AbstractSyntaxNode::Type::Block},
            {.data{.end_if{&merge}}, .type = IR::AbstractSyntaxNode::Type::EndIf},
            {.data{.block = &merge}, .type = IR::AbstractSyntaxNode::Type::Block},
            {.data{.if_node{access_guard, &access, &exit}},
             .type = IR::AbstractSyntaxNode::Type::If},
            {.data{.block = &access}, .type = IR::AbstractSyntaxNode::Type::Block},
            {.data{.end_if{&exit}}, .type = IR::AbstractSyntaxNode::Type::EndIf},
            {.data{.block = &exit}, .type = IR::AbstractSyntaxNode::Type::Block},
        };
        return Offsets(program, native);
    }
};
} // namespace

TEST_CASE("Predicated stores ignore the pointer from their untaken definition",
          "[shader][storage_buffer_control_flow]") {
    ConditionalPointer fixture;
    fixture.native = GENERATE(false, true);
    CHECK(fixture.Run(true, false, false) == std::vector<u32>{0x168});
}

TEST_CASE("Restored predicate bits retain pointer-definition correlation",
          "[shader][storage_buffer_control_flow]") {
    ConditionalPointer fixture;
    fixture.native = GENERATE(false, true);
    CHECK(fixture.Run(true, false, true) == std::vector<u32>{0x168});
}

TEST_CASE("Predicated input and weight loads reject a nearer stale skip pointer",
          "[shader][storage_buffer_control_flow]") {
    const bool native = GENERATE(false, true);
    for (const u32 base : {0x160, 0x170}) {
        ConditionalPointer fixture;
        fixture.native = native;
        CHECK(fixture.Run(true, false, 0, 0x168, base, true) == std::vector<u32>{base});
    }
}

TEST_CASE("Conditionally written output mask bits retain pointer-definition correlation",
          "[shader][storage_buffer_control_flow]") {
    ConditionalPointer fixture;
    fixture.native = GENERATE(false, true);
    CHECK(fixture.Run(true, false, 2) == std::vector<u32>{0x168});
}

TEST_CASE("Complementary predicates select the unchanged pointer",
          "[shader][storage_buffer_control_flow]") {
    ConditionalPointer fixture;
    fixture.native = GENERATE(false, true);
    CHECK(fixture.Run(true, true, true) == std::vector<u32>{0x160});
}

TEST_CASE("Unrelated predicates do not justify pruning a pointer phi",
          "[shader][storage_buffer_control_flow]") {
    for (const bool packed : {false, true}) {
        ConditionalPointer fixture;
        CHECK(fixture.Run(false, false, packed) == std::vector<u32>{0x160});
    }
}

TEST_CASE("Conditionally advanced pointers retain their base through selects and phis",
          "[shader][storage_buffer_control_flow]") {
    const bool native = GENERATE(false, true);
    for (const bool select_update : {false, true}) {
        Shader::ObjectPool<IR::Inst> pool{256};
        IR::Block entry{pool};
        IR::Block loop{pool};
        IR::Block step{pool};
        IR::Block merge{pool};
        IR::Program program;
        program.stage = Shader::Stage::Compute;
        program.blocks = {&entry, &loop, &step, &merge};
        program.post_order_blocks = {&merge, &step, &loop, &entry};
        entry.AddBranch(&loop);
        loop.AddBranch(&step);
        loop.AddBranch(&merge);
        step.AddBranch(&merge);
        merge.AddBranch(&loop);
        IR::IREmitter start{entry};
        const auto initial_low = Word(start, 0x168);
        const auto initial_high = Word(start, 0x16c);
        auto* low = Phi(loop);
        auto* high = Phi(loop);
        IR::IREmitter update{step};
        const auto next_low = update.IAdd(IR::U32{IR::Value{low}}, Word(update, 0x1c0));
        const auto next_high = update.IAdd(IR::U32{IR::Value{high}}, Word(update, 0x1c4));
        IR::IREmitter join{merge};
        IR::U32 selected_low;
        IR::U32 selected_high;
        if (select_update) {
            const auto condition = join.ILessThan(join.LocalInvocationIdX(), join.Imm32(16), false);
            selected_low = IR::U32{join.Select(condition, next_low, IR::U32{IR::Value{low}})};
            selected_high = IR::U32{join.Select(condition, next_high, IR::U32{IR::Value{high}})};
        } else {
            auto* low_phi = Phi(merge);
            auto* high_phi = Phi(merge);
            low_phi->AddPhiOperand(&loop, IR::Value{low});
            low_phi->AddPhiOperand(&step, next_low);
            high_phi->AddPhiOperand(&loop, IR::Value{high});
            high_phi->AddPhiOperand(&step, next_high);
            selected_low = IR::U32{IR::Value{low_phi}};
            selected_high = IR::U32{IR::Value{high_phi}};
        }
        low->AddPhiOperand(&entry, initial_low);
        low->AddPhiOperand(&merge, selected_low);
        high->AddPhiOperand(&entry, initial_high);
        high->AddPhiOperand(&merge, selected_high);
        static_cast<void>(join.LoadGlobal128(Address(join, selected_low, selected_high)));
        CHECK(Offsets(program, native) == std::vector<u32>{0x168});
    }
}

TEST_CASE("A current loop predicate cannot select the preceding iteration's pointer",
          "[shader][storage_buffer_control_flow]") {
    const bool native = GENERATE(false, true);
    Shader::ObjectPool<IR::Inst> pool{256};
    IR::Block entry{pool};
    IR::Block loop{pool};
    IR::Block definition{pool};
    IR::Block merge{pool};
    IR::Block access{pool};
    IR::Block back{pool};
    IR::Block exit{pool};
    IR::Program program;
    program.stage = Shader::Stage::Compute;
    program.blocks = {&entry, &loop, &definition, &merge, &access, &back, &exit};
    program.post_order_blocks = {&exit, &back, &access, &merge, &definition, &loop, &entry};
    for (u32 index = 0; index < program.blocks.size(); ++index) {
        program.blocks[index]->SetOrder(index);
    }
    entry.AddBranch(&loop);
    loop.AddBranch(&definition);
    loop.AddBranch(&merge);
    definition.AddBranch(&merge);
    merge.AddBranch(&access);
    merge.AddBranch(&back);
    access.AddBranch(&back);
    back.AddBranch(&loop);
    back.AddBranch(&exit);
    IR::IREmitter start{entry};
    const auto first_low = Word(start, 0x160);
    const auto first_high = Word(start, 0x164);
    auto* iteration = Phi(loop);
    auto* previous_low = Phi(loop);
    auto* previous_high = Phi(loop);
    IR::IREmitter body{loop};
    const auto predicate = body.IEqual(IR::U32{IR::Value{iteration}}, body.Imm32(0));
    const auto definition_guard = body.ConditionRef(predicate);
    IR::IREmitter update{definition};
    const auto second_low = Word(update, 0x168);
    const auto second_high = Word(update, 0x16c);
    auto* next_low = Phi(merge);
    auto* next_high = Phi(merge);
    next_low->AddPhiOperand(&loop, first_low);
    next_low->AddPhiOperand(&definition, second_low);
    next_high->AddPhiOperand(&loop, first_high);
    next_high->AddPhiOperand(&definition, second_high);
    IR::IREmitter joined{merge};
    const auto access_guard = joined.ConditionRef(predicate);
    IR::IREmitter use{access};
    static_cast<void>(use.LoadGlobal32(
        Address(use, IR::U32{IR::Value{previous_low}}, IR::U32{IR::Value{previous_high}})));
    IR::IREmitter advance{back};
    const auto next_iteration = advance.IAdd(IR::U32{IR::Value{iteration}}, advance.Imm32(1));
    iteration->AddPhiOperand(&entry, start.Imm32(0));
    iteration->AddPhiOperand(&back, next_iteration);
    previous_low->AddPhiOperand(&entry, start.Imm32(0));
    previous_low->AddPhiOperand(&back, IR::Value{next_low});
    previous_high->AddPhiOperand(&entry, start.Imm32(0));
    previous_high->AddPhiOperand(&back, IR::Value{next_high});
    program.syntax_list = {
        {.data{.block = &entry}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.loop{&loop, &back, &exit}}, .type = IR::AbstractSyntaxNode::Type::Loop},
        {.data{.block = &loop}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.if_node{definition_guard, &definition, &merge}},
         .type = IR::AbstractSyntaxNode::Type::If},
        {.data{.block = &definition}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.end_if{&merge}}, .type = IR::AbstractSyntaxNode::Type::EndIf},
        {.data{.block = &merge}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.if_node{access_guard, &access, &back}}, .type = IR::AbstractSyntaxNode::Type::If},
        {.data{.block = &access}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.end_if{&back}}, .type = IR::AbstractSyntaxNode::Type::EndIf},
        {.data{.block = &back}, .type = IR::AbstractSyntaxNode::Type::Block},
        {.data{.repeat{advance.Imm1(true), &loop, &exit}},
         .type = IR::AbstractSyntaxNode::Type::Repeat},
        {.data{.block = &exit}, .type = IR::AbstractSyntaxNode::Type::Block},
    };
    if (native) {
        CHECK(Offsets(program, true) == std::vector<u32>{0x160, 0x168});
        CHECK(program.info.native_global_bounds);
    } else {
        CHECK(Offsets(program) == std::vector<u32>{0x160});
    }
}

TEST_CASE("Unrelated native predicates retain every candidate pointer",
          "[shader][storage_buffer_control_flow]") {
    for (const bool packed : {false, true}) {
        ConditionalPointer fixture;
        fixture.native = true;
        CHECK(fixture.Run(false, false, packed) == std::vector<u32>{0x160, 0x168});
        CHECK(fixture.program.info.native_global_bounds);
        CHECK(fixture.access.back().GetOpcode() == IR::Opcode::WriteGlobal32);
        for (const auto& descriptor : fixture.program.info.storage_buffers_descriptors) {
            CHECK(descriptor.is_written);
        }
    }
}

TEST_CASE("Native stores the backend selects at run time are not rebased writes",
          "[shader][storage_buffer_control_flow]") {
    ConditionalPointer selected;
    selected.native = true;
    REQUIRE(selected.Run(false, false, false) == std::vector<u32>{0x160, 0x168});
    REQUIRE(selected.program.info.native_global_bounds);
    for (const auto& descriptor : selected.program.info.storage_buffers_descriptors) {
        CHECK(descriptor.is_written);
        CHECK_FALSE(descriptor.rebased_writes);
    }

    ConditionalPointer rebased;
    rebased.native = GENERATE(false, true);
    REQUIRE(rebased.Run(true, false, false) == std::vector<u32>{0x168});
    const auto& descriptor = rebased.program.info.storage_buffers_descriptors.front();
    CHECK(descriptor.is_written);
    CHECK(descriptor.rebased_writes);
}
