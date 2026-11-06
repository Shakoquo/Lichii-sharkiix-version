// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan.h"

namespace Vulkan {
namespace vk {
class CommandBuffer;
}

namespace Resolved {
template <typename T>
std::vector<T> Copy(const T* values, size_t count) {
    if (count == 0)
        return {};
    return {values, values + count};
}
struct Pipeline {
    VkPipelineBindPoint bind_point;
    VkPipeline pipeline;
};
struct DescriptorSets {
    VkPipelineBindPoint bind_point;
    VkPipelineLayout layout;
    u32 first;
    std::vector<VkDescriptorSet> sets;
    std::vector<u32> offsets;
};
struct PushDescriptors {
    VkDescriptorUpdateTemplate update_template;
    VkPipelineLayout layout;
    u32 set;
    std::vector<u8> data;
};
struct PushConstants {
    VkPipelineLayout layout;
    VkShaderStageFlags stages;
    u32 offset;
    std::vector<u8> data;
};
struct IndexBuffer {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType type;
};
struct VertexBuffers {
    u32 first;
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> offsets;
    std::optional<std::vector<VkDeviceSize>> sizes;
    std::optional<std::vector<VkDeviceSize>> strides;
    bool extended;
};
struct DescriptorBuffers {
    std::vector<VkDescriptorBufferBindingInfoEXT> bindings;
};
struct DescriptorOffsets {
    VkPipelineBindPoint bind_point;
    VkPipelineLayout layout;
    u32 first;
    std::vector<u32> indices;
    std::vector<VkDeviceSize> offsets;
};
struct Viewports {
    u32 first;
    std::vector<VkViewport> values;
};
struct Scissors {
    u32 first;
    std::vector<VkRect2D> values;
};
struct BlendConstants {
    std::array<float, 4> values;
};
enum class StencilMaskKind { Compare, Reference, Write };
struct StencilMask {
    StencilMaskKind kind;
    VkStencilFaceFlags faces;
    u32 value;
};
struct DepthBias {
    float constant_factor;
    float clamp;
    float slope_factor;
    std::optional<VkDepthBiasRepresentationInfoEXT> representation;
    bool extended;
};
struct DepthBounds {
    float minimum;
    float maximum;
};
enum class BooleanKind {
    DepthBoundsTest,
    DepthTest,
    DepthWrite,
    PrimitiveRestart,
    RasterizerDiscard,
    LineStipple,
    DepthBias,
    LogicOp,
    AlphaToCoverage,
    AlphaToOne,
    DepthClamp,
    StencilTest,
};
struct Boolean {
    BooleanKind kind;
    bool value;
};
enum class EnumKind {
    CullMode,
    DepthCompareOp,
    ConservativeRasterizationMode,
    LineRasterizationMode,
    FrontFace,
    LogicOp,
    PrimitiveTopology,
};
struct Enum {
    EnumKind kind;
    u32 value;
};
struct LineStipple {
    u32 factor;
    u16 pattern;
};
struct PatchControlPoints {
    u32 count;
};
enum class ColorValuesKind { WriteMask, WriteEnable, BlendEnable };
struct ColorValues {
    ColorValuesKind kind;
    u32 first;
    std::vector<u32> values;
};
struct BlendEquations {
    u32 first;
    std::vector<VkColorBlendEquationEXT> values;
};
struct LineWidth {
    float value;
};
struct StencilOp {
    VkStencilFaceFlags faces;
    VkStencilOp fail;
    VkStencilOp pass;
    VkStencilOp depth_fail;
    VkCompareOp compare;
};
struct VertexInput {
    std::vector<VkVertexInputBindingDescription2EXT> bindings;
    std::vector<VkVertexInputAttributeDescription2EXT> attributes;
};
struct Draw {
    u32 count;
    u32 instances;
    u32 first;
    u32 first_instance;
};
struct DrawIndexed {
    u32 count;
    u32 instances;
    u32 first;
    u32 vertex_offset;
    u32 first_instance;
};
} // namespace Resolved

class ResolvedCommand {
public:
    using Value =
        std::variant<Resolved::Pipeline, Resolved::DescriptorSets, Resolved::PushDescriptors,
                     Resolved::PushConstants, Resolved::IndexBuffer, Resolved::VertexBuffers,
                     Resolved::DescriptorBuffers, Resolved::DescriptorOffsets, Resolved::Viewports,
                     Resolved::Scissors, Resolved::BlendConstants, Resolved::StencilMask,
                     Resolved::DepthBias, Resolved::DepthBounds, Resolved::Boolean, Resolved::Enum,
                     Resolved::LineStipple, Resolved::PatchControlPoints, Resolved::ColorValues,
                     Resolved::BlendEquations, Resolved::LineWidth, Resolved::StencilOp,
                     Resolved::VertexInput, Resolved::Draw, Resolved::DrawIndexed>;

    ResolvedCommand(Value value_);
    void Replay(vk::CommandBuffer command_buffer) const;
    bool IsDraw() const;
    size_t ByteSize() const;

private:
    Value value;
};

struct SubmissionEpoch {
    u64 tick;
};

struct ResolvedBatch {
    std::shared_ptr<const SubmissionEpoch> epoch;
    u64 sequence;
    VkRenderPass renderpass;
    VkFramebuffer framebuffer;
    u32 subpass;
    std::vector<ResolvedCommand> initial_state;
    std::vector<ResolvedCommand> commands;

    void Replay(vk::CommandBuffer command_buffer) const;
};

using RecordBatches =
    std::function<std::optional<std::vector<VkCommandBuffer>>(std::span<const ResolvedBatch>)>;

enum class InlineFallbackReason : u8 {
    ActiveQuery,
    LateQuery,
    Conditional,
    TransformFeedback,
    IndirectMesh,
    TimestampCapture,
    SerialCommand,
    UnsupportedState,
    Drain,
    NoDraws,
    Budget,
    SingleBatch,
    EncoderUnavailable,
};

class PassRecorder {
public:
    struct Limits {
        size_t state_bytes = 1024 * 1024;
        size_t pass_bytes = 4 * 1024 * 1024;
        size_t replay_bytes = 8 * 1024 * 1024;
        size_t draws_per_batch = 32;
        size_t minimum_batches = 1;
        bool retain_inline_state = true;
    };

    explicit PassRecorder(RecordBatches record_batches = {});
    PassRecorder(RecordBatches record_batches, Limits limits);
    ~PassRecorder();
    PassRecorder(const PassRecorder&) = delete;
    PassRecorder& operator=(const PassRecorder&) = delete;

    void BeginSubmission(u64 tick, vk::CommandBuffer primary);
    vk::CommandBuffer CommandBuffer() const;
    void EndSubmission();
    void MaterializeInline(InlineFallbackReason reason = InlineFallbackReason::SerialCommand);
    void DisableCapture(InlineFallbackReason reason = InlineFallbackReason::UnsupportedState);
    void NoteFallback(InlineFallbackReason reason);
    bool CapturingState() const {
        return !disabled;
    }
    void Record(ResolvedCommand command);
    void BeginPass(const VkRenderPassBeginInfo& begin, VkSubpassContents contents);
    void EndPass();
    void SetQueryActive(bool active);
    void SetTransformFeedbackActive(bool active);
    void SetConditionalRenderingActive(bool active);

private:
    void BeginInline();
    void StopRetainingInlineState(InlineFallbackReason reason);
    void ReplayState() const;
    std::optional<std::vector<ResolvedBatch>> MakeBatches();

    RecordBatches record_batches;
    Limits limits;
    std::unique_ptr<vk::CommandBuffer> primary;
    std::shared_ptr<const SubmissionEpoch> epoch;
    std::vector<ResolvedCommand> state;
    std::vector<ResolvedCommand> commands;
    std::vector<VkClearValue> clears;
    VkRenderPassBeginInfo pass{};
    size_t state_bytes{};
    size_t pass_bytes{};
    size_t replay_bytes{};
    size_t initial_state_count{};
    u64 sequence{};
    u32 active_queries{};
    bool transform_feedback{};
    bool conditional_rendering{};
    bool pass_open{};
    bool inline_pass{};
    bool disabled{};
    std::optional<InlineFallbackReason> fallback_reason;
    InlineFallbackReason disabled_reason{InlineFallbackReason::UnsupportedState};
};

} // namespace Vulkan
