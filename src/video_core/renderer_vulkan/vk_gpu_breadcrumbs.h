// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <string_view>
#include <thread>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MemoryAllocator;

/// EDEN_NX2_GPU_BREADCRUMBS=1: the GPU writes the number of every draw and dispatch to host
/// memory when it starts and when it finishes (VK_AMD_buffer_marker), so a hang or device loss
/// can name the work the GPU was stuck on, by shader hash. A watchdog thread logs a GPU that
/// stops making progress, and the memory budget every few seconds.
class GpuBreadcrumbs {
public:
    static GpuBreadcrumbs& Get() {
        static GpuBreadcrumbs breadcrumbs;
        return breadcrumbs;
    }

    void Initialize(const Device& device, const MemoryAllocator& allocator);

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled;
    }

    /// Runs on the thread recording commands: marks `work` started and finished on the GPU.
    template <typename Func>
    void Wrap(vk::CommandBuffer cmdbuf, u64 hash, u64 fragment_hash, std::array<u32, 3> size,
              Func&& work) {
        const u32 sequence = recorded.load(std::memory_order_relaxed) + 1;
        items[sequence & ItemMask] = {sequence, hash, fragment_hash, size};
        recorded.store(sequence, std::memory_order_release);
        cmdbuf.WriteBufferMarkerAMD(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, *buffer, 0, sequence);
        work();
        cmdbuf.WriteBufferMarkerAMD(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, *buffer, 4, sequence);
    }

    /// Logs what the GPU was doing: the last work it started and finished and what lies between.
    void Dump(std::string_view reason) const;

private:
    static constexpr u32 ItemCount = 8192;
    static constexpr u32 ItemMask = ItemCount - 1;

    struct Item {
        u32 sequence;
        u64 hash;
        u64 fragment_hash;
        std::array<u32, 3> size;
    };

    void Watch(std::stop_token stop_token);
    void LogMemory(std::string_view reason) const;

    bool enabled = false;
    const Device* device = nullptr;
    vk::Buffer buffer;
    std::atomic<u32> recorded = 0;
    std::array<Item, ItemCount> items{};
    std::jthread watchdog;
};

} // namespace Vulkan
