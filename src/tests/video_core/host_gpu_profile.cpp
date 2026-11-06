// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "video_core/host_gpu_profile.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {
using VideoCommon::HostGpuProfile::Accounting;
using VideoCommon::HostGpuProfile::Phase;

VKAPI_ATTR void VKAPI_CALL BindVertexBuffers(VkCommandBuffer, u32, u32, const VkBuffer*,
                                             const VkDeviceSize*) {}

VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer, u32, u32, u32, u32) {}

VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue, u32, const VkSubmitInfo*, VkFence) {
    return VK_SUCCESS;
}

TEST_CASE("Vulkan wrappers account driver calls rather than forwarding helpers",
          "[video_core][profile]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    Profile::accounting.TakeSamples();
    Vulkan::vk::DeviceDispatch dispatch{};
    dispatch.vkCmdBindVertexBuffers = BindVertexBuffers;
    dispatch.vkCmdDraw = Draw;
    dispatch.vkQueueSubmit = Submit;
    const Vulkan::vk::CommandBuffer command{VK_NULL_HANDLE, dispatch};
    const Vulkan::vk::Queue queue{VK_NULL_HANDLE, dispatch};
    command.BindVertexBuffer(0, VK_NULL_HANDLE, 0);
    command.Draw(3, 1, 0, 0);
    REQUIRE(queue.Submit(nullptr) == VK_SUCCESS);
    const auto samples = Profile::accounting.TakeSamples();
    REQUIRE(samples[static_cast<size_t>(Phase::Encode)].calls == (Profile::enabled ? 2 : 0));
    REQUIRE(samples[static_cast<size_t>(Phase::Submit)].calls == (Profile::enabled ? 1 : 0));
}

TEST_CASE("Host GPU phases exclude nested work", "[video_core][profile]") {
    Accounting accounting;
    const auto resolve = accounting.Begin(Phase::Resolve, 100);
    const auto enqueue = accounting.Begin(Phase::Enqueue, 110);
    accounting.End(enqueue, 130);
    const auto wait = accounting.Begin(Phase::Wait, 140);
    accounting.End(wait, 150);
    accounting.End(resolve, 200);
    const auto samples = accounting.TakeSamples();
    REQUIRE(samples[static_cast<size_t>(Phase::Resolve)].nanoseconds == 70);
    REQUIRE(samples[static_cast<size_t>(Phase::Enqueue)].nanoseconds == 20);
    REQUIRE(samples[static_cast<size_t>(Phase::Wait)].nanoseconds == 10);
    for (const auto phase : {Phase::Resolve, Phase::Enqueue, Phase::Wait}) {
        REQUIRE(samples[static_cast<size_t>(phase)].calls == 1);
    }
    for (const auto& sample : accounting.TakeSamples()) {
        REQUIRE(sample.nanoseconds == 0);
        REQUIRE(sample.calls == 0);
    }
}

TEST_CASE("Host GPU phases separate encoding submission and callback work",
          "[video_core][profile]") {
    Accounting accounting;
    const auto worker = accounting.Begin(Phase::Worker, 0);
    const auto first = accounting.Begin(Phase::Encode, 5);
    accounting.End(first, 15);
    const auto second = accounting.Begin(Phase::Encode, 20);
    accounting.End(second, 30);
    const auto submit = accounting.Begin(Phase::Submit, 35);
    accounting.End(submit, 85);
    accounting.End(worker, 100);
    const auto samples = accounting.TakeSamples();
    REQUIRE(samples[static_cast<size_t>(Phase::Encode)].nanoseconds == 20);
    REQUIRE(samples[static_cast<size_t>(Phase::Encode)].calls == 2);
    REQUIRE(samples[static_cast<size_t>(Phase::Submit)].nanoseconds == 50);
    REQUIRE(samples[static_cast<size_t>(Phase::Worker)].nanoseconds == 30);
}

TEST_CASE("Host GPU phases handle recursive categories and adjacent intervals",
          "[video_core][profile]") {
    Accounting accounting;
    const auto outer = accounting.Begin(Phase::Wait, 10);
    const auto inner = accounting.Begin(Phase::Wait, 20);
    accounting.End(inner, 30);
    accounting.End(outer, 50);
    const auto next = accounting.Begin(Phase::Wait, 60);
    accounting.End(next, 70);
    const auto samples = accounting.TakeSamples();
    REQUIRE(samples[static_cast<size_t>(Phase::Wait)].nanoseconds == 50);
    REQUIRE(samples[static_cast<size_t>(Phase::Wait)].calls == 3);
}

TEST_CASE("Host GPU phase accounting is thread local", "[video_core][profile]") {
    namespace Profile = VideoCommon::HostGpuProfile;
    Profile::accounting.TakeSamples();
    const auto token = Profile::accounting.Begin(Phase::Resolve, 10);
    u64 worker_ns{};
    std::jthread worker{[&] {
        const auto work = Profile::accounting.Begin(Phase::Encode, 0);
        Profile::accounting.End(work, 100);
        worker_ns =
            Profile::accounting.TakeSamples()[static_cast<size_t>(Phase::Encode)].nanoseconds;
    }};
    worker.join();
    Profile::accounting.End(token, 20);
    const auto samples = Profile::accounting.TakeSamples();
    REQUIRE(worker_ns == 100);
    REQUIRE(samples[static_cast<size_t>(Phase::Resolve)].nanoseconds == 10);
    REQUIRE(samples[static_cast<size_t>(Phase::Encode)].nanoseconds == 0);
}
} // namespace
