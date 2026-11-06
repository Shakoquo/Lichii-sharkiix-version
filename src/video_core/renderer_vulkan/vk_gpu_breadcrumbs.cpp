// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/logging.h"
#include "common/thread.h"
#include "video_core/renderer_vulkan/vk_gpu_breadcrumbs.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

namespace {
// Diagnostic builds for players who can only send logs turn this on at build time.
#ifdef LICHII_GPU_BREADCRUMBS_DEFAULT
constexpr bool DefaultEnabled = true;
#else
constexpr bool DefaultEnabled = false;
#endif
} // Anonymous namespace

void GpuBreadcrumbs::Initialize(const Device& device_, const MemoryAllocator& allocator) {
    const char* value = std::getenv("EDEN_NX2_GPU_BREADCRUMBS");
    const bool wanted = value != nullptr ? value[0] != '0' : DefaultEnabled;
    if (!wanted || enabled) {
        return;
    }
    device = &device_;
    LOG_INFO(Render_Vulkan, "GPU: {} | driver {} {:#x} | Vulkan {}.{}.{}", device->GetModelName(),
             device->GetDriverName(), device->GetDriverVersion(),
             VK_API_VERSION_MAJOR(device->ApiVersion()), VK_API_VERSION_MINOR(device->ApiVersion()),
             VK_API_VERSION_PATCH(device->ApiVersion()));
    LogMemory("start");
    if (!device->IsAmdBufferMarkerSupported()) {
        LOG_WARNING(Render_Vulkan, "GPU breadcrumbs need VK_AMD_buffer_marker, which this driver "
                                   "lacks");
        return;
    }
    const VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = 256,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    buffer = allocator.CreateBuffer(ci, MemoryUsage::Download);
    if (buffer.Mapped().size() < 8) {
        LOG_WARNING(Render_Vulkan, "GPU breadcrumbs: no host-visible buffer");
        return;
    }
    std::memset(buffer.Mapped().data(), 0, 8);
    enabled = true;
    watchdog = std::jthread([this](std::stop_token stop_token) { Watch(stop_token); });
    LOG_INFO(Render_Vulkan, "GPU breadcrumbs enabled");
}

void GpuBreadcrumbs::LogMemory(std::string_view reason) const {
    if (device == nullptr) {
        return;
    }
    const auto info = device->GetPhysical().GetMemoryProperties();
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    if (device->CanReportMemoryUsage()) {
        device->GetPhysical().GetMemoryProperties(&budget);
    }
    std::string heaps;
    for (u32 heap = 0; heap < info.memoryProperties.memoryHeapCount; ++heap) {
        const auto& properties = info.memoryProperties.memoryHeaps[heap];
        const bool local = (properties.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        heaps += fmt::format(" | heap {}{}: {} MiB, used {} of budget {} MiB", heap,
                             local ? " (VRAM)" : "", properties.size >> 20,
                             budget.heapUsage[heap] >> 20, budget.heapBudget[heap] >> 20);
    }
    LOG_INFO(Render_Vulkan, "GPU memory ({}){}", reason, heaps);
}

void GpuBreadcrumbs::Watch(std::stop_token stop_token) {
    Common::SetCurrentThreadName("GPUWatchdog");
    using namespace std::chrono_literals;
    u32 last_started = 0;
    u32 last_finished = 0;
    auto unchanged_since = std::chrono::steady_clock::now();
    auto memory_logged = unchanged_since;
    bool reported = false;
    while (!stop_token.stop_requested()) {
        std::this_thread::sleep_for(500ms);
        buffer.Invalidate();
        u32 started = 0;
        u32 finished = 0;
        std::memcpy(&started, buffer.Mapped().data(), sizeof(started));
        std::memcpy(&finished, buffer.Mapped().data() + 4, sizeof(finished));
        const auto now = std::chrono::steady_clock::now();
        if (started != last_started || finished != last_finished) {
            if (reported) {
                LOG_WARNING(Render_Vulkan, "GPU progressing again (finished {})", finished);
            }
            last_started = started;
            last_finished = finished;
            unchanged_since = now;
            reported = false;
        } else if (!reported && started > finished && now - unchanged_since >= 3s) {
            // Work started on the GPU and has not finished for 3 s: a GPU hang.
            reported = true;
            Dump("GPU made no progress for 3 s");
            LogMemory("hang");
        }
        if (now - memory_logged >= 10s) {
            memory_logged = now;
            LogMemory("periodic");
        }
    }
}

void GpuBreadcrumbs::Dump(std::string_view reason) const {
    if (!enabled) {
        return;
    }
    buffer.Invalidate();
    u32 started = 0;
    u32 finished = 0;
    std::memcpy(&started, buffer.Mapped().data(), sizeof(started));
    std::memcpy(&finished, buffer.Mapped().data() + 4, sizeof(finished));
    const u32 recorded_now = recorded.load(std::memory_order_acquire);
    LOG_CRITICAL(Render_Vulkan,
                 "GPU breadcrumbs ({}): recorded {}, last started {}, last finished {}", reason,
                 recorded_now, started, finished);
    // The work the GPU had started but not finished, plus a little context either side.
    const u32 first = finished > 4 ? finished - 4 : 1;
    const u32 last = (std::min)(recorded_now, (std::max)(started, finished) + 4);
    for (u32 sequence = first; sequence <= last && sequence - first < 64; ++sequence) {
        const Item& item = items[sequence & ItemMask];
        if (item.sequence != sequence) {
            continue;
        }
        const char* state = sequence <= finished  ? "done"
                            : sequence <= started ? "RUNNING"
                                                  : "queued";
        LOG_CRITICAL(Render_Vulkan, "  #{} {} shader {:016x} fragment {:016x} size {}x{}x{}",
                     sequence, state, item.hash, item.fragment_hash, item.size[0], item.size[1],
                     item.size[2]);
    }
}

} // namespace Vulkan
