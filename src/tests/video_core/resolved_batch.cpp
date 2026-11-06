// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_resolved_batch.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {
using namespace Vulkan;

template <typename T>
T Handle(uintptr_t value) {
    if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<T>(value);
    else
        return static_cast<T>(value);
}

struct Event {
    std::string name;
    std::vector<u64> values;

    bool operator==(const Event&) const = default;
};

thread_local std::vector<Event>* events;
thread_local const VkClearValue* forwarded_clears;
thread_local const VkViewport* forwarded_viewports;

VKAPI_ATTR void VKAPI_CALL BeginPass(VkCommandBuffer, const VkRenderPassBeginInfo* info,
                                     VkSubpassContents contents) {
    forwarded_clears = info->pClearValues;
    events->push_back({"begin",
                       {static_cast<u64>(contents), info->clearValueCount,
                        info->clearValueCount ? info->pClearValues[0].color.uint32[0] : 0}});
}
VKAPI_ATTR void VKAPI_CALL EndPass(VkCommandBuffer) {
    events->push_back({"end", {}});
}
VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer, u32 count, u32, u32 first, u32) {
    events->push_back({"draw", {count, first}});
}
VKAPI_ATTR void VKAPI_CALL DrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, u32, u32) {
    events->push_back({"indirect", {}});
}
VKAPI_ATTR void VKAPI_CALL Viewports(VkCommandBuffer, u32 first, u32 count,
                                     const VkViewport* values) {
    forwarded_viewports = values;
    Event event{"viewports", {first}};
    for (u32 i = 0; i < count; ++i)
        event.values.push_back(static_cast<u64>(values[i].x));
    events->push_back(std::move(event));
}
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer, VkPipelineLayout,
                                         VkShaderStageFlags stages, u32 offset, u32 size,
                                         const void* data) {
    Event event{"constants", {stages, offset}};
    const auto* bytes = static_cast<const u8*>(data);
    for (u32 i = 0; i < size; ++i)
        event.values.push_back(bytes[i]);
    events->push_back(std::move(event));
}
VKAPI_ATTR void VKAPI_CALL PushDescriptors(VkCommandBuffer, VkDescriptorUpdateTemplate,
                                           VkPipelineLayout, u32, const void* data) {
    u64 word{};
    std::memcpy(&word, data, sizeof(word));
    events->push_back({"descriptors", {word}});
}
VKAPI_ATTR void VKAPI_CALL Pipeline(VkCommandBuffer, VkPipelineBindPoint point, VkPipeline) {
    events->push_back({"pipeline", {static_cast<u64>(point)}});
}
VKAPI_ATTR void VKAPI_CALL Execute(VkCommandBuffer, u32 count, const VkCommandBuffer*) {
    events->push_back({"execute", {count}});
}
VKAPI_ATTR void VKAPI_CALL BeginQuery(VkCommandBuffer, VkQueryPool, u32, VkQueryControlFlags) {
    events->push_back({"query begin", {}});
}
VKAPI_ATTR void VKAPI_CALL EndQuery(VkCommandBuffer, VkQueryPool, u32) {
    events->push_back({"query end", {}});
}
VKAPI_ATTR void VKAPI_CALL StencilReference(VkCommandBuffer, VkStencilFaceFlags faces,
                                            u32 reference) {
    events->push_back({"stencil", {faces, reference}});
}
VKAPI_ATTR void VKAPI_CALL DepthBias(VkCommandBuffer, const VkDepthBiasInfoEXT* info) {
    const auto* representation = static_cast<const VkDepthBiasRepresentationInfoEXT*>(info->pNext);
    events->push_back(
        {"bias",
         {static_cast<u64>(info->depthBiasConstantFactor),
          representation ? static_cast<u64>(representation->depthBiasRepresentation) : 0}});
}
VKAPI_ATTR void VKAPI_CALL VertexBuffers(VkCommandBuffer, u32 first, u32 count, const VkBuffer*,
                                         const VkDeviceSize* offsets, const VkDeviceSize* sizes,
                                         const VkDeviceSize* strides) {
    events->push_back(
        {"vertices", {first, count, offsets[0], sizes ? sizes[0] : 0, strides ? strides[0] : 0}});
}

struct Fixture {
    std::vector<Event> log;
    vk::DeviceDispatch dispatch{};

    Fixture() {
        events = &log;
        forwarded_clears = nullptr;
        forwarded_viewports = nullptr;
        dispatch.vkCmdBeginRenderPass = BeginPass;
        dispatch.vkCmdEndRenderPass = EndPass;
        dispatch.vkCmdDraw = Draw;
        dispatch.vkCmdDrawIndirect = DrawIndirect;
        dispatch.vkCmdSetViewport = Viewports;
        dispatch.vkCmdPushConstants = PushConstants;
        dispatch.vkCmdPushDescriptorSetWithTemplateKHR = PushDescriptors;
        dispatch.vkCmdBindPipeline = Pipeline;
        dispatch.vkCmdExecuteCommands = Execute;
        dispatch.vkCmdBeginQuery = BeginQuery;
        dispatch.vkCmdEndQuery = EndQuery;
        dispatch.vkCmdSetStencilReference = StencilReference;
        dispatch.vkCmdSetDepthBias2EXT = DepthBias;
        dispatch.vkCmdBindVertexBuffers2EXT = VertexBuffers;
    }

    vk::CommandBuffer Native() const {
        return vk::CommandBuffer{Handle<VkCommandBuffer>(1), dispatch};
    }

    static VkRenderPassBeginInfo Pass() {
        return {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = Handle<VkRenderPass>(2),
                .framebuffer = Handle<VkFramebuffer>(3),
                .renderArea = {{0, 0}, {64, 64}}};
    }

    std::vector<std::string> Names() const {
        std::vector<std::string> names;
        for (const auto& event : log)
            names.push_back(event.name);
        return names;
    }
};

TEST_CASE("Resolved batches own descriptor push viewport and clear payloads",
          "[video_core][resolved]") {
    Fixture f;
    std::vector<ResolvedBatch> saved;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            saved.assign(batches.begin(), batches.end());
            return std::nullopt;
        }};
    recorder.BeginSubmission(17, f.Native());
    auto cb = recorder.CommandBuffer();
    auto pass = Fixture::Pass();
    VkClearValue clear{};
    clear.color.uint32[0] = 42;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    cb.BeginRenderPass(pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{.x = 7};
    std::array<u8, 4> constants{1, 2, 3, 4};
    u64 descriptors = 123;
    cb.SetViewport(0, viewport);
    cb.PushConstants(Handle<VkPipelineLayout>(4), VK_SHADER_STAGE_VERTEX_BIT, 0, constants.size(),
                     constants.data());
    cb.PushDescriptorSetWithTemplateKHR(Handle<VkDescriptorUpdateTemplate>(5),
                                        Handle<VkPipelineLayout>(4), 0, &descriptors,
                                        sizeof(descriptors));
    cb.Draw(3, 1, 0, 0);
    clear.color.uint32[0] = 99;
    viewport.x = 99;
    constants.fill(99);
    descriptors = 99;
    REQUIRE(f.log.empty());
    cb.EndRenderPass();
    recorder.EndSubmission();
    REQUIRE(saved.size() == 1);
    REQUIRE(saved[0].epoch->tick == 17);
    REQUIRE(saved[0].renderpass == pass.renderPass);
    REQUIRE(saved[0].framebuffer == pass.framebuffer);
    REQUIRE(f.log[0].values == std::vector<u64>{VK_SUBPASS_CONTENTS_INLINE, 1, 42});
    REQUIRE(f.log[1].values == std::vector<u64>{0, 7});
    REQUIRE(f.log[2].values == std::vector<u64>{VK_SHADER_STAGE_VERTEX_BIT, 0, 1, 2, 3, 4});
    REQUIRE(f.log[3].values == std::vector<u64>{123});
    f.log.clear();
    saved[0].Replay(f.Native());
    REQUIRE(f.log[0].values == std::vector<u64>{0, 7});
    REQUIRE(f.log[2].values == std::vector<u64>{123});
}

TEST_CASE("Resolved batch prefixes preserve overlapping state in original order",
          "[video_core][resolved]") {
    Fixture f;
    std::vector<ResolvedBatch> saved;
    PassRecorder::Limits limits;
    limits.draws_per_batch = 1;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            saved.assign(batches.begin(), batches.end());
            return std::nullopt;
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    std::array<VkViewport, 3> viewports{};
    viewports[0].x = 1;
    viewports[1].x = 2;
    viewports[2].x = 3;
    cb.SetViewport(0, viewports);
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 4);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    VkViewport replacement{.x = 8};
    cb.SetViewport(1, replacement);
    cb.SetStencilReference(VK_STENCIL_FACE_BACK_BIT, 9);
    cb.Draw(3, 1, 0, 0);
    cb.Draw(4, 1, 1, 0);
    cb.EndRenderPass();
    REQUIRE(saved.size() == 2);
    REQUIRE(saved[0].initial_state.size() == 2);
    REQUIRE(saved[1].initial_state.size() == 4);
    REQUIRE(saved[0].sequence < saved[1].sequence);
    f.log.clear();
    saved[1].Replay(f.Native());
    REQUIRE(f.Names() ==
            std::vector<std::string>{"viewports", "stencil", "viewports", "stencil", "draw"});
    REQUIRE(f.log[0].values == std::vector<u64>{0, 1, 2, 3});
    REQUIRE(f.log[2].values == std::vector<u64>{1, 8});
    REQUIRE(f.log[3].values == std::vector<u64>{VK_STENCIL_FACE_BACK_BIT, 9});
}

TEST_CASE("Late unsupported commands downgrade the entire open pass", "[video_core][resolved]") {
    Fixture f;
    u32 calls{};
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++calls;
            return std::nullopt;
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    REQUIRE(f.log.empty());
    cb.DrawIndirect(Handle<VkBuffer>(7), 0, 1, 16);
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "draw", "indirect"});
    cb.Draw(4, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(calls == 0);
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "draw", "indirect", "draw", "end"});
}

TEST_CASE("Inline immutable batches preserve the original serial command trace",
          "[video_core][resolved]") {
    Fixture f;
    u32 producer_calls{};
    const auto produce = [&](vk::CommandBuffer cb) {
        ++producer_calls;
        cb.SetStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 4);
        cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
        cb.SetStencilReference(VK_STENCIL_FACE_BACK_BIT, 9);
        cb.Draw(3, 1, 0, 0);
        cb.Draw(4, 1, 1, 0);
        cb.EndRenderPass();
    };
    produce(f.Native());
    const auto original = f.log;
    f.log.clear();
    producer_calls = 0;
    PassRecorder::Limits limits;
    limits.draws_per_batch = 1;
    PassRecorder recorder{{}, limits};
    recorder.BeginSubmission(1, f.Native());
    produce(recorder.CommandBuffer());
    REQUIRE(producer_calls == 1);
    REQUIRE(f.log == original);
}

TEST_CASE("Draining an open resolved pass does not require its future end",
          "[video_core][resolved]") {
    Fixture f;
    u32 calls{};
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++calls;
            return std::nullopt;
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    recorder.MaterializeInline();
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "draw"});
    recorder.MaterializeInline();
    cb.Draw(4, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(calls == 0);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(5, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(calls == 1);
}

TEST_CASE("Resolved passes inherit active query exclusions", "[video_core][resolved]") {
    Fixture f;
    u32 calls{};
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++calls;
            return std::nullopt;
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginQuery(Handle<VkQueryPool>(1), 0, 0);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    cb.EndQuery(Handle<VkQueryPool>(1), 0);
    REQUIRE(calls == 0);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(calls == 1);
}

TEST_CASE("Secondary execution restores graphics and compute state on the primary",
          "[video_core][resolved]") {
    Fixture f;
    PassRecorder recorder{
        [](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            return std::vector<VkCommandBuffer>(batches.size(), Handle<VkCommandBuffer>(8));
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, Handle<VkPipeline>(1));
    cb.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, Handle<VkPipeline>(2));
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(f.Names() == std::vector<std::string>{"pipeline", "pipeline", "begin", "execute", "end",
                                                  "pipeline", "pipeline"});
    REQUIRE(f.log[2].values[0] == VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    REQUIRE(f.log[5].values[0] == VK_PIPELINE_BIND_POINT_COMPUTE);
    REQUIRE(f.log[6].values[0] == VK_PIPELINE_BIND_POINT_GRAPHICS);
}

TEST_CASE("Resolved history and replay budgets downgrade without losing commands",
          "[video_core][resolved]") {
    Fixture f;
    u32 calls{};
    PassRecorder::Limits limits;
    limits.replay_bytes = 0;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++calls;
            return std::nullopt;
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_BIT, 7);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(calls == 0);
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "stencil", "draw", "end"});
    REQUIRE_FALSE(recorder.CapturingState());
    recorder.EndSubmission();
    recorder.BeginSubmission(2, f.Native());
    REQUIRE(recorder.CapturingState());
}

TEST_CASE("Resolved depth bias and geometry own optional pointer payloads",
          "[video_core][resolved]") {
    Fixture f;
    PassRecorder recorder;
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    VkDepthBiasRepresentationInfoEXT representation{
        .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT,
        .depthBiasRepresentation = VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT};
    cb.SetDepthBias(2, 0, 0, &representation);
    VkBuffer buffer = Handle<VkBuffer>(9);
    VkDeviceSize offset = 16, stride = 32;
    cb.BindVertexBuffers2EXT(2, 1, &buffer, &offset, nullptr, &stride);
    offset = stride = 99;
    representation.depthBiasRepresentation =
        VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT;
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(f.log[1].values == std::vector<u64>{2, VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT});
    REQUIRE(f.log[2].values == std::vector<u64>{2, 1, 16, 0, 32});
}

TEST_CASE("Raw command buffer access materializes and disables the current epoch",
          "[video_core][resolved]") {
    Fixture f;
    PassRecorder recorder;
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    REQUIRE(*cb == Handle<VkCommandBuffer>(1));
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "draw"});
    REQUIRE_FALSE(recorder.CapturingState());
    cb.EndRenderPass();
}

TEST_CASE("Unknown pointer chains downgrade before their direct driver call",
          "[video_core][resolved]") {
    Fixture f;
    u32 callbacks{};
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++callbacks;
            return std::nullopt;
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    VkBaseInStructure unknown{};
    VkDepthBiasRepresentationInfoEXT representation{
        .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT,
        .pNext = &unknown,
        .depthBiasRepresentation = VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT};
    cb.SetDepthBias(2, 0, 0, &representation);
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "draw", "bias"});
    cb.EndRenderPass();
    REQUIRE(callbacks == 0);
    REQUIRE_FALSE(recorder.CapturingState());
}

TEST_CASE("History and pass budgets preserve a complete serial fallback",
          "[video_core][resolved]") {
    Fixture f;
    PassRecorder::Limits limits;
    SECTION("State storage is exhausted") {
        limits.state_bytes = 0;
    }
    SECTION("Pass storage is exhausted") {
        limits.pass_bytes = 0;
    }
    PassRecorder recorder{{}, limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_BIT, 4);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(f.Names() == std::vector<std::string>{"begin", "stencil", "draw", "end"});
    REQUIRE_FALSE(recorder.CapturingState());
}

TEST_CASE("A new submission discards all historical handles", "[video_core][resolved]") {
    Fixture f;
    std::vector<ResolvedBatch> saved;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            saved.assign(batches.begin(), batches.end());
            return std::nullopt;
        }};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, Handle<VkPipeline>(2));
    recorder.EndSubmission();
    recorder.BeginSubmission(2, f.Native());
    cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(saved.size() == 1);
    REQUIRE(saved[0].epoch->tick == 2);
    REQUIRE(saved[0].initial_state.empty());
}

TEST_CASE("Inline fallback counters preserve the first cause and epoch inheritance",
          "[video_core][resolved]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    using Counter = Profile::Counter;
    Profile::TakeCounters();
    Fixture f;
    PassRecorder recorder;
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    const auto begin_draw = [&] {
        cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
        cb.Draw(3, 1, 0, 0);
    };

    begin_draw();
    cb.BeginQuery(Handle<VkQueryPool>(1), 0, 0);
    recorder.MaterializeInline(InlineFallbackReason::Drain);
    cb.EndRenderPass();

    begin_draw();
    cb.EndRenderPass();
    cb.EndQuery(Handle<VkQueryPool>(1), 0);

    begin_draw();
    recorder.MaterializeInline(InlineFallbackReason::Drain);
    cb.DrawIndirect(Handle<VkBuffer>(7), 0, 1, 16);
    cb.EndRenderPass();

    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.EndRenderPass();

    begin_draw();
    (void)*cb;
    recorder.DisableCapture(InlineFallbackReason::TimestampCapture);
    cb.EndRenderPass();
    begin_draw();
    cb.EndRenderPass();

    recorder.EndSubmission();
    recorder.BeginSubmission(2, f.Native());
    cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.EndRenderPass();

    const auto counts = Profile::TakeCounters();
    const u64 unit = Profile::enabled ? 1 : 0;
    REQUIRE(counts[static_cast<size_t>(Counter::InlinePasses)] == 7 * unit);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineLateQuery)] == unit);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineActiveQuery)] == unit);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineDrain)] == unit);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineNoDraws)] == 2 * unit);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineUnsupportedState)] == 2 * unit);
    u64 reasons{};
    for (size_t i = static_cast<size_t>(Counter::InlineActiveQuery); i < counts.size(); ++i) {
        reasons += counts[i];
    }
    REQUIRE(reasons == counts[static_cast<size_t>(Counter::InlinePasses)]);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineIndirectMesh)] == 0);
    REQUIRE(counts[static_cast<size_t>(Counter::InlineTimestampCapture)] == 0);
}

TEST_CASE("Inline callback rejection distinguishes an annotated single batch",
          "[video_core][resolved]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    Profile::TakeCounters();
    bool single_batch{};
    SECTION("The callback identifies a single batch") {
        single_batch = true;
    }
    SECTION("The encoder is unavailable") {}
    Fixture f;
    PassRecorder* active{};
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            if (single_batch) {
                active->NoteFallback(InlineFallbackReason::SingleBatch);
            }
            return std::nullopt;
        }};
    active = &recorder;
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    const auto counts = Profile::TakeCounters();
    const u64 unit = Profile::enabled ? 1 : 0;
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlinePasses)] == unit);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlineSingleBatch)] ==
            (single_batch ? unit : 0));
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlineEncoderUnavailable)] ==
            (single_batch ? 0 : unit));
}

TEST_CASE("Budget fallback reasons persist for later passes without recounting events",
          "[video_core][resolved]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    Profile::TakeCounters();
    Fixture f;
    PassRecorder::Limits limits;
    limits.state_bytes = 0;
    PassRecorder recorder{{}, limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_BIT, 4);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    const auto counts = Profile::TakeCounters();
    const u64 unit = Profile::enabled ? 1 : 0;
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlinePasses)] == 2 * unit);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlineBudget)] == 2 * unit);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::BudgetFallbacks)] == unit);
}

TEST_CASE("Minimum batch preflight rejects short passes before snapshot budgeting",
          "[video_core][resolved]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    Profile::TakeCounters();
    Fixture f;
    u32 callbacks{};
    PassRecorder::Limits limits;
    limits.minimum_batches = 2;
    limits.replay_bytes = 0;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch>) -> std::optional<std::vector<VkCommandBuffer>> {
            ++callbacks;
            return std::nullopt;
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_BIT, 4);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(callbacks == 0);
    REQUIRE(recorder.CapturingState());
    REQUIRE(f.Names() == std::vector<std::string>{"stencil", "begin", "draw", "end"});
    const auto counts = Profile::TakeCounters();
    const u64 unit = Profile::enabled ? 1 : 0;
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::InlineSingleBatch)] == unit);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::ResolvedPasses)] == 0);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::ResolvedBatches)] == 0);
    REQUIRE(counts[static_cast<size_t>(Profile::Counter::BudgetFallbacks)] == 0);
}

TEST_CASE("Production fallback forwards payloads without ownership until a fresh epoch",
          "[video_core][resolved]") {
    Fixture f;
    u32 callbacks{};
    PassRecorder::Limits limits;
    limits.minimum_batches = 2;
    limits.draws_per_batch = 1;
    limits.retain_inline_state = false;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            ++callbacks;
            return std::vector<VkCommandBuffer>(batches.size(), Handle<VkCommandBuffer>(8));
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    SECTION("A late query forces serial recording") {
        cb.BeginQuery(Handle<VkQueryPool>(1), 0, 0);
        cb.EndQuery(Handle<VkQueryPool>(1), 0);
    }
    SECTION("An explicit drain forces serial recording") {
        recorder.MaterializeInline(InlineFallbackReason::Drain);
    }
    SECTION("Conditional rendering forces serial recording") {
        recorder.SetConditionalRenderingActive(true);
        recorder.SetConditionalRenderingActive(false);
    }
    SECTION("Transform feedback forces serial recording") {
        recorder.SetTransformFeedbackActive(true);
        recorder.SetTransformFeedbackActive(false);
    }
    SECTION("Raw command buffer escape forces serial recording") {
        (void)*cb;
    }
    SECTION("An unsupported draw forces serial recording") {
        cb.DrawIndirect(Handle<VkBuffer>(7), 0, 1, 16);
    }
    REQUIRE_FALSE(recorder.CapturingState());
    VkViewport viewport{.x = 7};
    const auto previous_events = f.log.size();
    cb.SetViewport(0, viewport);
    REQUIRE(f.log.size() == previous_events + 1);
    REQUIRE(forwarded_viewports == &viewport);
    cb.EndRenderPass();

    auto pass = Fixture::Pass();
    VkClearValue clear{};
    clear.color.uint32[0] = 42;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    const auto before_pass = f.log.size();
    cb.BeginRenderPass(pass, VK_SUBPASS_CONTENTS_INLINE);
    REQUIRE(f.log.size() == before_pass + 1);
    REQUIRE(forwarded_clears == &clear);
    cb.SetViewport(0, viewport);
    REQUIRE(forwarded_viewports == &viewport);
    cb.Draw(3, 1, 0, 0);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(callbacks == 0);

    recorder.EndSubmission();
    recorder.BeginSubmission(2, f.Native());
    REQUIRE(recorder.CapturingState());
    cb = recorder.CommandBuffer();
    const auto before_capture = f.log.size();
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.Draw(3, 1, 0, 0);
    REQUIRE(f.log.size() == before_capture);
    cb.EndRenderPass();
    REQUIRE(callbacks == 1);
    REQUIRE(recorder.CapturingState());
}

TEST_CASE("Production policy restores parallel state before downgrading the next pass",
          "[video_core][resolved]") {
    Fixture f;
    PassRecorder::Limits limits;
    limits.minimum_batches = 2;
    limits.draws_per_batch = 1;
    limits.retain_inline_state = false;
    PassRecorder recorder{
        [](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            return std::vector<VkCommandBuffer>(batches.size(), Handle<VkCommandBuffer>(8));
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, Handle<VkPipeline>(1));
    cb.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, Handle<VkPipeline>(2));
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(recorder.CapturingState());
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.DrawIndirect(Handle<VkBuffer>(7), 0, 1, 16);
    cb.EndRenderPass();
    REQUIRE_FALSE(recorder.CapturingState());
    REQUIRE(f.Names() == std::vector<std::string>{"pipeline", "pipeline", "begin", "execute", "end",
                                                  "pipeline", "pipeline", "begin", "draw",
                                                  "indirect", "end"});
    REQUIRE(f.log[5].values[0] == VK_PIPELINE_BIND_POINT_COMPUTE);
    REQUIRE(f.log[6].values[0] == VK_PIPELINE_BIND_POINT_GRAPHICS);
}

TEST_CASE("Production policy preserves eligibility after empty and short passes",
          "[video_core][resolved]") {
    Fixture f;
    u32 callbacks{};
    size_t inherited_commands{};
    PassRecorder::Limits limits;
    limits.minimum_batches = 2;
    limits.draws_per_batch = 1;
    limits.retain_inline_state = false;
    PassRecorder recorder{
        [&](std::span<const ResolvedBatch> batches) -> std::optional<std::vector<VkCommandBuffer>> {
            ++callbacks;
            inherited_commands = batches.front().initial_state.size();
            return std::vector<VkCommandBuffer>(batches.size(), Handle<VkCommandBuffer>(8));
        },
        limits};
    recorder.BeginSubmission(1, f.Native());
    auto cb = recorder.CommandBuffer();
    cb.SetStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 4);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.EndRenderPass();
    REQUIRE(recorder.CapturingState());
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.SetStencilReference(VK_STENCIL_FACE_BACK_BIT, 9);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(recorder.CapturingState());
    REQUIRE(callbacks == 0);
    cb.BeginRenderPass(Fixture::Pass(), VK_SUBPASS_CONTENTS_INLINE);
    cb.Draw(3, 1, 0, 0);
    cb.Draw(3, 1, 0, 0);
    cb.EndRenderPass();
    REQUIRE(callbacks == 1);
    REQUIRE(inherited_commands == 2);
    REQUIRE(recorder.CapturingState());
}
} // namespace
