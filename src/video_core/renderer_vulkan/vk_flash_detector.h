// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;

class FlashDetector {
public:
    FlashDetector(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler);
    ~FlashDetector();
    void Present(VkImage source, VkFormat format, u32 width, u32 height);

private:
    struct Sample {
        vk::Image image;
        vk::Buffer buffer;
        u64 tick{};
        u64 generation{};
        u64 frame{};
        double min_luma{};
        bool pending{};
    };
    const Device& device;
    Scheduler& scheduler;
    std::array<Sample, 8> samples;
    vk::QueryPool queries;
    u32 timestamp_bits{};
    double timestamp_period{};
    u64 frame{};
    std::optional<VkFormat> checked_format;
};
} // namespace Vulkan
