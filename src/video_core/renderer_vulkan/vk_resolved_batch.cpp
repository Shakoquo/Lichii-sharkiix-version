// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "video_core/host_gpu_profile.h"
#include "video_core/renderer_vulkan/vk_resolved_batch.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace Profile = VideoCommon::HostGpuProfile;

ResolvedCommand::ResolvedCommand(Value value_) : value{std::move(value_)} {}

bool ResolvedCommand::IsDraw() const {
    return std::holds_alternative<Resolved::Draw>(value) ||
           std::holds_alternative<Resolved::DrawIndexed>(value);
}

size_t ResolvedCommand::ByteSize() const {
    return std::visit(
        [](const auto& v) {
            size_t size = sizeof(ResolvedCommand);
            const auto add = [&size](const auto& items) {
                size += items.size() * sizeof(typename std::decay_t<decltype(items)>::value_type);
            };
            if constexpr (requires { v.data; })
                add(v.data);
            if constexpr (requires { v.sets; })
                add(v.sets);
            if constexpr (requires { v.offsets; })
                add(v.offsets);
            if constexpr (requires { v.buffers; })
                add(v.buffers);
            if constexpr (requires { v.sizes; }) {
                if (v.sizes)
                    add(*v.sizes);
            }
            if constexpr (requires { v.strides; }) {
                if (v.strides)
                    add(*v.strides);
            }
            if constexpr (requires { v.bindings; })
                add(v.bindings);
            if constexpr (requires { v.attributes; })
                add(v.attributes);
            if constexpr (requires { v.indices; })
                add(v.indices);
            if constexpr (requires { v.values; })
                add(v.values);
            return size;
        },
        value);
}

void ResolvedCommand::Replay(vk::CommandBuffer cb) const {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            using namespace Resolved;
            if constexpr (std::is_same_v<T, Pipeline>) {
                cb.BindPipeline(v.bind_point, v.pipeline);
            } else if constexpr (std::is_same_v<T, DescriptorSets>) {
                cb.BindDescriptorSets(v.bind_point, v.layout, v.first, v.sets, v.offsets);
            } else if constexpr (std::is_same_v<T, PushDescriptors>) {
                cb.PushDescriptorSetWithTemplateKHR(v.update_template, v.layout, v.set,
                                                    v.data.data(), v.data.size());
            } else if constexpr (std::is_same_v<T, PushConstants>) {
                cb.PushConstants(v.layout, v.stages, v.offset, static_cast<u32>(v.data.size()),
                                 v.data.data());
            } else if constexpr (std::is_same_v<T, IndexBuffer>) {
                cb.BindIndexBuffer(v.buffer, v.offset, v.type);
            } else if constexpr (std::is_same_v<T, VertexBuffers>) {
                if (v.extended) {
                    cb.BindVertexBuffers2EXT(v.first, static_cast<u32>(v.buffers.size()),
                                             v.buffers.data(), v.offsets.data(),
                                             v.sizes ? v.sizes->data() : nullptr,
                                             v.strides ? v.strides->data() : nullptr);
                } else {
                    cb.BindVertexBuffers(v.first, static_cast<u32>(v.buffers.size()),
                                         v.buffers.data(), v.offsets.data());
                }
            } else if constexpr (std::is_same_v<T, DescriptorBuffers>) {
                cb.BindDescriptorBuffersEXT(v.bindings);
            } else if constexpr (std::is_same_v<T, DescriptorOffsets>) {
                cb.SetDescriptorBufferOffsetsEXT(v.bind_point, v.layout, v.first, v.indices,
                                                 v.offsets);
            } else if constexpr (std::is_same_v<T, Viewports>) {
                cb.SetViewport(v.first, v.values);
            } else if constexpr (std::is_same_v<T, Scissors>) {
                cb.SetScissor(v.first, v.values);
            } else if constexpr (std::is_same_v<T, BlendConstants>) {
                cb.SetBlendConstants(v.values.data());
            } else if constexpr (std::is_same_v<T, StencilMask>) {
                switch (v.kind) {
                case StencilMaskKind::Compare:
                    cb.SetStencilCompareMask(v.faces, v.value);
                    break;
                case StencilMaskKind::Reference:
                    cb.SetStencilReference(v.faces, v.value);
                    break;
                case StencilMaskKind::Write:
                    cb.SetStencilWriteMask(v.faces, v.value);
                    break;
                }
            } else if constexpr (std::is_same_v<T, DepthBias>) {
                if (v.extended) {
                    auto representation = v.representation;
                    cb.SetDepthBias(v.constant_factor, v.clamp, v.slope_factor,
                                    representation ? &*representation : nullptr);
                } else {
                    cb.SetDepthBias(v.constant_factor, v.clamp, v.slope_factor);
                }
            } else if constexpr (std::is_same_v<T, DepthBounds>) {
                cb.SetDepthBounds(v.minimum, v.maximum);
            } else if constexpr (std::is_same_v<T, Boolean>) {
                switch (v.kind) {
                case BooleanKind::DepthBoundsTest:
                    cb.SetDepthBoundsTestEnableEXT(v.value);
                    break;
                case BooleanKind::DepthTest:
                    cb.SetDepthTestEnableEXT(v.value);
                    break;
                case BooleanKind::DepthWrite:
                    cb.SetDepthWriteEnableEXT(v.value);
                    break;
                case BooleanKind::PrimitiveRestart:
                    cb.SetPrimitiveRestartEnableEXT(v.value);
                    break;
                case BooleanKind::RasterizerDiscard:
                    cb.SetRasterizerDiscardEnableEXT(v.value);
                    break;
                case BooleanKind::LineStipple:
                    cb.SetLineStippleEnableEXT(v.value);
                    break;
                case BooleanKind::DepthBias:
                    cb.SetDepthBiasEnableEXT(v.value);
                    break;
                case BooleanKind::LogicOp:
                    cb.SetLogicOpEnableEXT(v.value);
                    break;
                case BooleanKind::AlphaToCoverage:
                    cb.SetAlphaToCoverageEnableEXT(v.value);
                    break;
                case BooleanKind::AlphaToOne:
                    cb.SetAlphaToOneEnableEXT(v.value);
                    break;
                case BooleanKind::DepthClamp:
                    cb.SetDepthClampEnableEXT(v.value);
                    break;
                case BooleanKind::StencilTest:
                    cb.SetStencilTestEnableEXT(v.value);
                    break;
                }
            } else if constexpr (std::is_same_v<T, Enum>) {
                switch (v.kind) {
                case EnumKind::CullMode:
                    cb.SetCullModeEXT(v.value);
                    break;
                case EnumKind::DepthCompareOp:
                    cb.SetDepthCompareOpEXT(static_cast<VkCompareOp>(v.value));
                    break;
                case EnumKind::ConservativeRasterizationMode:
                    cb.SetConservativeRasterizationModeEXT(
                        static_cast<VkConservativeRasterizationModeEXT>(v.value));
                    break;
                case EnumKind::LineRasterizationMode:
                    cb.SetLineRasterizationModeEXT(
                        static_cast<VkLineRasterizationModeEXT>(v.value));
                    break;
                case EnumKind::FrontFace:
                    cb.SetFrontFaceEXT(static_cast<VkFrontFace>(v.value));
                    break;
                case EnumKind::LogicOp:
                    cb.SetLogicOpEXT(static_cast<VkLogicOp>(v.value));
                    break;
                case EnumKind::PrimitiveTopology:
                    cb.SetPrimitiveTopologyEXT(static_cast<VkPrimitiveTopology>(v.value));
                    break;
                }
            } else if constexpr (std::is_same_v<T, LineStipple>) {
                cb.SetLineStippleEXT(v.factor, v.pattern);
            } else if constexpr (std::is_same_v<T, PatchControlPoints>) {
                cb.SetPatchControlPointsEXT(v.count);
            } else if constexpr (std::is_same_v<T, ColorValues>) {
                switch (v.kind) {
                case ColorValuesKind::WriteMask:
                    cb.SetColorWriteMaskEXT(v.first, v.values);
                    break;
                case ColorValuesKind::WriteEnable:
                    cb.SetColorWriteEnableEXT(v.values);
                    break;
                case ColorValuesKind::BlendEnable:
                    cb.SetColorBlendEnableEXT(v.first, v.values);
                    break;
                }
            } else if constexpr (std::is_same_v<T, BlendEquations>) {
                cb.SetColorBlendEquationEXT(v.first, v.values);
            } else if constexpr (std::is_same_v<T, LineWidth>) {
                cb.SetLineWidth(v.value);
            } else if constexpr (std::is_same_v<T, StencilOp>) {
                cb.SetStencilOpEXT(v.faces, v.fail, v.pass, v.depth_fail, v.compare);
            } else if constexpr (std::is_same_v<T, VertexInput>) {
                cb.SetVertexInputEXT(v.bindings, v.attributes);
            } else if constexpr (std::is_same_v<T, Draw>) {
                cb.Draw(v.count, v.instances, v.first, v.first_instance);
            } else if constexpr (std::is_same_v<T, DrawIndexed>) {
                cb.DrawIndexed(v.count, v.instances, v.first, v.vertex_offset, v.first_instance);
            }
        },
        value);
}

void ResolvedBatch::Replay(vk::CommandBuffer cb) const {
    for (const auto& command : initial_state)
        command.Replay(cb);
    for (const auto& command : commands)
        command.Replay(cb);
}

PassRecorder::PassRecorder(RecordBatches callback) : PassRecorder{std::move(callback), Limits{}} {}
PassRecorder::PassRecorder(RecordBatches callback, Limits limits_)
    : record_batches{std::move(callback)}, limits{limits_} {
    if (limits.draws_per_batch == 0 || limits.minimum_batches == 0)
        throw std::invalid_argument("Zero resolved batch draw count or minimum batch count");
}
PassRecorder::~PassRecorder() = default;

void PassRecorder::BeginSubmission(u64 tick, vk::CommandBuffer primary_) {
    if (pass_open)
        throw std::logic_error("Submission changed inside a render pass");
    primary = std::make_unique<vk::CommandBuffer>(primary_.WithoutRecorder());
    epoch = std::make_shared<const SubmissionEpoch>(SubmissionEpoch{tick});
    state.clear();
    commands.clear();
    clears.clear();
    state_bytes = pass_bytes = replay_bytes = initial_state_count = 0;
    sequence = 0;
    active_queries = 0;
    transform_feedback = conditional_rendering = inline_pass = disabled = false;
    fallback_reason.reset();
    disabled_reason = InlineFallbackReason::UnsupportedState;
}

vk::CommandBuffer PassRecorder::CommandBuffer() const {
    if (!primary)
        throw std::logic_error("Resolved recorder has no submission");
    return primary->WithRecorder(const_cast<PassRecorder*>(this));
}

void PassRecorder::EndSubmission() {
    MaterializeInline();
    if (pass_open)
        throw std::logic_error("Submission ended inside a render pass");
}

void PassRecorder::BeginInline() {
    if (!pass_open || inline_pass)
        return;
    pass.pClearValues = clears.data();
    primary->BeginRenderPass(pass, VK_SUBPASS_CONTENTS_INLINE);
    inline_pass = true;
}

void PassRecorder::NoteFallback(InlineFallbackReason reason) {
    if (pass_open && !fallback_reason) {
        fallback_reason = reason;
    }
}

void PassRecorder::MaterializeInline(InlineFallbackReason reason) {
    if (!pass_open || inline_pass)
        return;
    NoteFallback(reason);
    BeginInline();
    for (const auto& command : commands)
        command.Replay(*primary);
    commands.clear();
    pass_bytes = 0;
    StopRetainingInlineState(reason);
}

void PassRecorder::StopRetainingInlineState(InlineFallbackReason reason) {
    if (limits.retain_inline_state)
        return;
    const auto cause = fallback_reason.value_or(reason);
    if (cause == InlineFallbackReason::NoDraws || cause == InlineFallbackReason::SingleBatch ||
        cause == InlineFallbackReason::EncoderUnavailable) {
        return;
    }
    if (!disabled) {
        disabled_reason = cause;
    }
    disabled = true;
    state.clear();
    state_bytes = 0;
}

void PassRecorder::DisableCapture(InlineFallbackReason reason) {
    MaterializeInline(reason);
    if (!disabled) {
        disabled_reason = reason;
    }
    disabled = true;
    state.clear();
    state_bytes = 0;
}

void PassRecorder::Record(ResolvedCommand command) {
    if (disabled) {
        command.Replay(*primary);
        return;
    }
    const size_t bytes = command.ByteSize();
    if (!disabled && !command.IsDraw()) {
        if (bytes > limits.state_bytes - std::min(state_bytes, limits.state_bytes)) {
            Profile::Add(Profile::Counter::BudgetFallbacks);
            DisableCapture(InlineFallbackReason::Budget);
        } else {
            state.push_back(command);
            state_bytes += bytes;
        }
    }
    if (pass_open && !inline_pass && !disabled) {
        if (bytes <= limits.pass_bytes - std::min(pass_bytes, limits.pass_bytes)) {
            commands.push_back(std::move(command));
            pass_bytes += bytes;
            return;
        }
        Profile::Add(Profile::Counter::BudgetFallbacks);
        DisableCapture(InlineFallbackReason::Budget);
    }
    command.Replay(*primary);
}

void PassRecorder::BeginPass(const VkRenderPassBeginInfo& begin, VkSubpassContents contents) {
    if (pass_open)
        throw std::logic_error("Nested render pass capture");
    fallback_reason.reset();
    if (begin.pNext || contents != VK_SUBPASS_CONTENTS_INLINE) {
        DisableCapture();
        primary->BeginRenderPass(begin, contents);
        pass_open = inline_pass = true;
        NoteFallback(disabled_reason);
        return;
    }
    std::optional<InlineFallbackReason> reason;
    if (disabled) {
        reason = disabled_reason;
    } else if (active_queries) {
        reason = InlineFallbackReason::ActiveQuery;
    } else if (transform_feedback) {
        reason = InlineFallbackReason::TransformFeedback;
    } else if (conditional_rendering) {
        reason = InlineFallbackReason::Conditional;
    } else if (size_t{begin.clearValueCount} * sizeof(VkClearValue) > limits.pass_bytes) {
        reason = InlineFallbackReason::Budget;
    }
    if (reason) {
        StopRetainingInlineState(*reason);
        primary->BeginRenderPass(begin, contents);
        pass_open = inline_pass = true;
        NoteFallback(*reason);
        return;
    }
    pass = begin;
    pass.pClearValues = nullptr;
    clears.clear();
    if (begin.clearValueCount != 0) {
        clears.assign(begin.pClearValues, begin.pClearValues + begin.clearValueCount);
    }
    initial_state_count = state.size();
    commands.clear();
    pass_bytes = clears.size() * sizeof(VkClearValue);
    pass_open = true;
    inline_pass = false;
}

std::optional<std::vector<ResolvedBatch>> PassRecorder::MakeBatches() {
    const size_t draws = std::count_if(commands.begin(), commands.end(),
                                       [](const auto& command) { return command.IsDraw(); });
    if (draws == 0) {
        NoteFallback(InlineFallbackReason::NoDraws);
        return std::nullopt;
    }
    if ((draws - 1) / limits.draws_per_batch + 1 < limits.minimum_batches) {
        NoteFallback(InlineFallbackReason::SingleBatch);
        return std::nullopt;
    }
    const auto over_budget = [this]() -> std::optional<std::vector<ResolvedBatch>> {
        Profile::Add(Profile::Counter::BudgetFallbacks);
        NoteFallback(InlineFallbackReason::Budget);
        if (!disabled) {
            disabled_reason = InlineFallbackReason::Budget;
        }
        disabled = true;
        return std::nullopt;
    };
    size_t prefix_bytes{};
    for (size_t i = 0; i < initial_state_count; ++i)
        prefix_bytes += state[i].ByteSize();
    size_t cost = state_bytes;
    size_t draw_count{};
    bool new_batch = true;
    for (const auto& command : commands) {
        if (new_batch) {
            if (prefix_bytes > limits.replay_bytes - std::min(cost, limits.replay_bytes))
                return over_budget();
            cost += prefix_bytes;
            new_batch = false;
        }
        if (command.IsDraw()) {
            if (++draw_count == limits.draws_per_batch) {
                draw_count = 0;
                new_batch = true;
            }
        } else {
            prefix_bytes += command.ByteSize();
        }
    }
    if (cost > limits.replay_bytes - std::min(replay_bytes, limits.replay_bytes))
        return over_budget();
    replay_bytes += cost;
    std::vector<ResolvedBatch> batches;
    size_t prefix = initial_state_count;
    draw_count = 0;
    new_batch = true;
    for (const auto& command : commands) {
        if (new_batch) {
            batches.push_back({epoch,
                               sequence++,
                               pass.renderPass,
                               pass.framebuffer,
                               0,
                               {state.begin(), state.begin() + prefix},
                               {}});
            new_batch = false;
        }
        batches.back().commands.push_back(command);
        if (command.IsDraw()) {
            if (++draw_count == limits.draws_per_batch) {
                draw_count = 0;
                new_batch = true;
            }
        } else {
            ++prefix;
        }
    }
    return batches;
}

void PassRecorder::ReplayState() const {
    for (const auto& command : state)
        command.Replay(*primary);
}

void PassRecorder::EndPass() {
    if (!pass_open)
        throw std::logic_error("Render pass capture ended without a begin");
    if (!inline_pass) {
        auto batches = MakeBatches();
        if (batches) {
            Profile::Add(Profile::Counter::ResolvedPasses);
            Profile::Add(Profile::Counter::ResolvedBatches, batches->size());
            auto secondary = record_batches ? record_batches(*batches) : std::nullopt;
            if (secondary) {
                if (secondary->size() != batches->size())
                    throw std::logic_error("Wrong secondary count");
                pass.pClearValues = clears.data();
                primary->BeginRenderPass(pass, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
                primary->ExecuteCommands(*secondary);
                primary->EndRenderPass();
                ReplayState();
                Profile::Add(Profile::Counter::ParallelPasses);
                Profile::Add(Profile::Counter::ParallelBatches, batches->size());
                pass_open = false;
                commands.clear();
                clears.clear();
                return;
            }
            NoteFallback(InlineFallbackReason::EncoderUnavailable);
            BeginInline();
            for (const auto& batch : *batches) {
                for (const auto& command : batch.commands) {
                    command.Replay(*primary);
                }
            }
            commands.clear();
        }
    }
    MaterializeInline();
    primary->EndRenderPass();
    StopRetainingInlineState(InlineFallbackReason::EncoderUnavailable);
    Profile::Add(Profile::Counter::InlinePasses);
    if (Profile::enabled) {
        static constexpr std::array reason_counters{
            Profile::Counter::InlineActiveQuery,
            Profile::Counter::InlineLateQuery,
            Profile::Counter::InlineConditional,
            Profile::Counter::InlineTransformFeedback,
            Profile::Counter::InlineIndirectMesh,
            Profile::Counter::InlineTimestampCapture,
            Profile::Counter::InlineSerialCommand,
            Profile::Counter::InlineUnsupportedState,
            Profile::Counter::InlineDrain,
            Profile::Counter::InlineNoDraws,
            Profile::Counter::InlineBudget,
            Profile::Counter::InlineSingleBatch,
            Profile::Counter::InlineEncoderUnavailable,
        };
        static_assert(reason_counters.size() ==
                      static_cast<size_t>(InlineFallbackReason::EncoderUnavailable) + 1);
        Profile::Add(reason_counters[static_cast<size_t>(
            fallback_reason.value_or(InlineFallbackReason::SerialCommand))]);
    }
    pass_open = inline_pass = false;
    commands.clear();
    clears.clear();
}

void PassRecorder::SetQueryActive(bool active) {
    MaterializeInline(InlineFallbackReason::LateQuery);
    if (active)
        ++active_queries;
    else if (active_queries)
        --active_queries;
}

void PassRecorder::SetTransformFeedbackActive(bool active) {
    DisableCapture(InlineFallbackReason::TransformFeedback);
    transform_feedback = active;
}

void PassRecorder::SetConditionalRenderingActive(bool active) {
    MaterializeInline(InlineFallbackReason::Conditional);
    conditional_rendering = active;
}

} // namespace Vulkan
