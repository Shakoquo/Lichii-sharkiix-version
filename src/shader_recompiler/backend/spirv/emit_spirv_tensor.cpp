// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Backend::SPIRV {
void DefineSoftwareMatrixFunctions(EmitContext& ctx, const IR::Program& program) {
    std::array<bool, 2> needed{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::TensorMulAdd || inst.Flags<u32>() == 0) continue;
            const u32 depth = inst.Flags<u32>();
            if (depth != 8 && depth != 16) throw LogicError("Invalid software HMMA depth");
            needed[depth == 16] = true;
        }
    }
    for (u32 variant = 0; variant < 2; ++variant) {
        if (!needed[variant]) continue;
        ctx.AddCapability(spv::Capability::GroupNonUniform);
        ctx.AddCapability(spv::Capability::GroupNonUniformShuffle);
        if (!ctx.profile.SupportsSubgroupStage(program.stage) ||
            ctx.profile.has_broken_spirv_subgroup_shuffle)
            throw NotImplementedException("Software HMMA requires subgroup shuffle support");
        const Id u32ty = ctx.U32[1], f32ty = ctx.F32[1];
        const auto imm = [&](u32 v) { return ctx.Const(v); };
        const auto add = [&](Id a, Id b) { return ctx.OpIAdd(u32ty, a, b); };
        const auto and_ = [&](Id a, u32 b) { return ctx.OpBitwiseAnd(u32ty, a, imm(b)); };
        const auto unpack = [&](Id v) { return ctx.OpUnpackHalf2x16(ctx.F32[2], v); };
        const auto round_half = [&](Id value) {
            // Do not express the intermediate rounding as unpack(pack(value)): drivers may
            // eliminate that round trip. Round normal significands explicitly, ties to even;
            // half subnormals lie on the exactly representable 2^-24 grid.
            const Id bits = ctx.OpBitcast(u32ty, value);
            const Id magnitude = and_(bits, 0x7fffffffU);
            const Id tie = and_(ctx.OpShiftRightLogical(u32ty, magnitude, imm(13)), 1);
            const Id rounded = and_(add(add(magnitude, imm(0xfff)), tie), ~0x1fffU);
            const Id normal = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, rounded, imm(0x47800000)), imm(0x7f800000), rounded);
            const Id scaled = ctx.OpFMul(f32ty, ctx.OpBitcast(f32ty, magnitude), ctx.Const(16777216.0f));
            const Id subnormal = ctx.OpBitcast(u32ty,
                ctx.OpFMul(f32ty, ctx.OpRoundEven(f32ty, scaled), ctx.Const(0x1p-24f)));
            const Id finite = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, magnitude, imm(0x38800000)), normal, subnormal);
            const Id result = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, magnitude, imm(0x7f800000)), magnitude, finite);
            return ctx.OpBitcast(f32ty, ctx.OpBitwiseOr(u32ty, result, and_(bits, 0x80000000)));
        };
        const Id type = ctx.TypeFunction(ctx.U32[2], ctx.U32[4], ctx.U32[2], ctx.U32[2]);
        const Id fn = ctx.OpFunction(ctx.U32[2], spv::FunctionControlMask::DontInline, type);
        ctx.software_matrix_functions[variant] = fn;
        const Id matrix_a = ctx.OpFunctionParameter(ctx.U32[4]);
        const Id matrix_b = ctx.OpFunctionParameter(ctx.U32[2]);
        const Id matrix_c = ctx.OpFunctionParameter(ctx.U32[2]);
        ctx.AddLabel();
        const Id counter = ctx.AddLocalVariable(ctx.TypePointer(spv::StorageClass::Function, u32ty),
                                           spv::StorageClass::Function);
        std::array<Id, 4> accumulators;
        for (auto& acc : accumulators)
            acc = ctx.AddLocalVariable(ctx.TypePointer(spv::StorageClass::Function, f32ty),
                                 spv::StorageClass::Function);
        std::array<Id, 8> inputs;
        for (u32 i = 0; i < 4; ++i)
            inputs[i] = ctx.OpCompositeExtract(u32ty, matrix_a, i);
        for (u32 i = 0; i < 2; ++i) {
            inputs[4 + i] = ctx.OpCompositeExtract(u32ty, matrix_b, i);
            inputs[6 + i] = ctx.OpCompositeExtract(u32ty, matrix_c, i);
        }
        for (u32 row = 0; row < 2; ++row) {
            const Id c = unpack(inputs[6 + row]);
            for (u32 col = 0; col < 2; ++col)
                ctx.OpStore(accumulators[row * 2 + col], ctx.OpCompositeExtract(f32ty, c, col));
        }
        ctx.OpStore(counter, imm(0));
        // Lane (group g, thread t) of an m16n8k16 fragment owns A rows g and g+8 and computes
        // C columns 2t and 2t+1. Its A operands live in its own quad (lanes 4g..4g+3): a quad
        // broadcast with a constant index, which AMD and NVIDIA do as a register swizzle instead
        // of a crossbar permute. Its B operands live in groups 2t and 2t+1: a real shuffle.
        // Each K=8 half is unrolled so every index is a constant and the half rounding at its
        // end needs no branch; only the two halves of K=16 share the loop.
        const bool quad_broadcast = ctx.profile.support_quad_shuffles;
        if (quad_broadcast) {
            ctx.AddCapability(spv::Capability::GroupNonUniformQuad);
        }
        const Id lane = ctx.OpLoad(u32ty, ctx.subgroup_local_invocation_id);
        const Id warp_base = and_(lane, ~31U);
        const Id a_base = add(warp_base, and_(lane, 28));
        const Id b_base = add(warp_base, ctx.OpShiftLeftLogical(u32ty, and_(lane, 3), imm(3)));
        const Id header = ctx.OpLabel(), body = ctx.OpLabel(), next = ctx.OpLabel(), end = ctx.OpLabel();
        ctx.OpBranch(header);
        ctx.AddLabel(header);
        const Id half_k = ctx.OpLoad(u32ty, counter);
        const Id more = ctx.OpULessThan(ctx.U1, half_k, imm(variant ? 2 : 1));
        ctx.OpLoopMerge(end, next, spv::LoopControlMask::DontUnroll);
        ctx.OpBranchConditional(more, body, end);
        ctx.AddLabel(body);
        const Id upper = ctx.OpINotEqual(ctx.U1, half_k, imm(0));
        const Id b = ctx.OpSelect(u32ty, upper, inputs[5], inputs[4]);
        std::array<Id, 2> a_rows;
        for (u32 row = 0; row < 2; ++row)
            a_rows[row] = ctx.OpSelect(u32ty, upper, inputs[row + 2], inputs[row]);
        std::array<Id, 4> values;
        for (u32 i = 0; i < 4; ++i)
            values[i] = ctx.OpLoad(f32ty, accumulators[i]);
        for (u32 component = 0; component < 4; ++component) {
            std::array<Id, 2> bs;
            for (u32 col = 0; col < 2; ++col)
                bs[col] = unpack(ctx.OpGroupNonUniformShuffle(u32ty, imm(3), b,
                                                              add(b_base, imm(col * 4 + component))));
            for (u32 row = 0; row < 2; ++row) {
                const Id shared = quad_broadcast
                    ? ctx.OpGroupNonUniformQuadBroadcast(u32ty, imm(3), a_rows[row], imm(component))
                    : ctx.OpGroupNonUniformShuffle(u32ty, imm(3), a_rows[row],
                                                   add(a_base, imm(component)));
                const Id as = unpack(shared);
                for (u32 col = 0; col < 2; ++col) {
                    Id value = values[row * 2 + col];
                    for (u32 half = 0; half < 2; ++half) {
                        value = ctx.OpFma(f32ty, ctx.OpCompositeExtract(f32ty, as, half),
                                          ctx.OpCompositeExtract(f32ty, bs[col], half), value);
                        ctx.Decorate(value, spv::Decoration::NoContraction);
                    }
                    values[row * 2 + col] = value;
                }
            }
        }
        // HMMA.F16 rounds after each K=8 group, including the midpoint of K=16.
        for (u32 i = 0; i < 4; ++i)
            ctx.OpStore(accumulators[i], round_half(values[i]));
        ctx.OpBranch(next);
        ctx.AddLabel(next);
        ctx.OpStore(counter, add(half_k, imm(1)));
        ctx.OpBranch(header);
        ctx.AddLabel(end);
        std::array<Id, 2> packed;
        for (u32 row = 0; row < 2; ++row)
            packed[row] = ctx.OpPackHalf2x16(u32ty, ctx.OpCompositeConstruct(ctx.F32[2],
                ctx.OpLoad(f32ty, accumulators[row * 2]), ctx.OpLoad(f32ty, accumulators[row * 2 + 1])));
        ctx.OpReturnValue(ctx.OpCompositeConstruct(ctx.U32[2], packed[0], packed[1]));
        ctx.OpFunctionEnd();
    }
}

Id EmitTensorMulAdd(EmitContext& ctx, IR::Inst* inst, Id a, Id b, Id c) {
    const u32 depth = inst->Flags<u32>();
    if (depth != 8 && depth != 16) {
        throw NotImplementedException("Unsupported matrix multiply depth");
    }
    return ctx.OpFunctionCall(ctx.U32[2], ctx.software_matrix_functions[depth == 16], a, b, c);
}

} // namespace Shader::Backend::SPIRV
