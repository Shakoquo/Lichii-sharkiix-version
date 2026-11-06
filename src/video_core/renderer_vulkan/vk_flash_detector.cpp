// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include "video_core/nx2_flash_trigger.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_flash_detector.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {
namespace {
constexpr u32 Width = 64;
constexpr u32 Height = 36;
constexpr VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
constexpr VkImageSubresourceLayers Layers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

VkImageMemoryBarrier Barrier(VkImage image, VkAccessFlags src, VkAccessFlags dst,
                             VkImageLayout old_layout, VkImageLayout new_layout) {
    return {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src, .dstAccessMask = dst,
            .oldLayout = old_layout, .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image, .subresourceRange = Range};
}
}

FlashDetector::FlashDetector(const Device& device_, MemoryAllocator& allocator, Scheduler& scheduler_)
    : device{device_}, scheduler{scheduler_} {
    if (!(device.GetPhysical().GetFormatProperties(Format).optimalTilingFeatures &
          VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        throw std::runtime_error{"flash detector RGBA8 blit destination unsupported"};
    }
    for (auto& sample : samples) {
        sample.image = allocator.CreateImage({
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = Format,
            .extent = {Width, Height, 1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED});
        sample.buffer = CreateWrappedBuffer(allocator, Width * Height * 4, MemoryUsage::Download);
    }
    const auto physical = device.GetPhysical();
    timestamp_bits = physical.GetQueueFamilyProperties()[device.GetGraphicsFamily()].timestampValidBits;
    timestamp_period = physical.GetProperties().limits.timestampPeriod;
    if (timestamp_bits > 0 && timestamp_bits <= 64) {
        queries = device.GetLogical().CreateQueryPool({
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 16});
    }
}

FlashDetector::~FlashDetector() {
    scheduler.Finish();
}

void FlashDetector::Present(VkImage source, VkFormat format, u32 width, u32 height) {
    using namespace VideoCore::Nx2Debug;
    ++frame;
    const auto begin = std::chrono::steady_clock::now();
    const auto config = GetFlashConfig();
    if (!config) {
        return;
    }
    scheduler.GetMasterSemaphore().Refresh();
    for (u32 i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        if (!sample.pending || !scheduler.IsFree(sample.tick)) {
            continue;
        }
        std::optional<double> gpu_ms;
        if (queries) {
            std::array<u64, 2> stamps{};
            const auto result = device.GetLogical().GetQueryResults(
                *queries, i * 2, 2, sizeof(stamps), stamps.data(), sizeof(u64), VK_QUERY_RESULT_64_BIT);
            if (result != VK_SUCCESS) {
                continue;
            }
            const u64 mask = timestamp_bits == 64 ? ~u64{} : (u64{1} << timestamp_bits) - 1;
            gpu_ms = static_cast<double>((stamps[1] - stamps[0]) & mask) * timestamp_period / 1e6;
        }
        sample.buffer.Invalidate();
        const auto data = sample.buffer.Mapped();
        ObserveFlash(sample.generation, sample.frame, frame,
                     WhiteFraction({reinterpret_cast<const u8*>(data.data()), Width * Height * 4},
                                   sample.min_luma));
        NoteFlashCost(sample.generation, -1, gpu_ms, false);
        sample.pending = false;
    }
    if (checked_format != format) {
        if (!(device.GetPhysical().GetFormatProperties(format).optimalTilingFeatures &
              VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
            FailFlash(config->generation, "present format does not support blit sampling");
            return;
        }
        checked_format = format;
    }
    auto slot = std::ranges::find_if(samples, [](const auto& sample) { return !sample.pending; });
    const bool dropped = slot == samples.end();
    if (!dropped) {
        const u32 query = static_cast<u32>(slot - samples.begin()) * 2;
        slot->generation = config->generation;
        slot->frame = frame;
        slot->min_luma = config->min_luma;
        slot->tick = scheduler.CurrentTick();
        slot->pending = true;
        const VkImage image = *slot->image;
        const VkBuffer buffer = *slot->buffer;
        const VkQueryPool pool = queries ? *queries : VK_NULL_HANDLE;
        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([source, image, buffer, pool, query, width, height](vk::CommandBuffer cmdbuf) {
            if (pool) {
                cmdbuf.ResetQueryPool(pool, query, 2);
            }
            const std::array before{
                Barrier(source, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
                Barrier(image, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, nullptr, nullptr, before);
            if (pool) {
                cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_TRANSFER_BIT, pool, query);
            }
            const VkImageBlit blit{
                .srcSubresource = Layers,
                .srcOffsets = {{0, 0, 0}, {static_cast<s32>(width), static_cast<s32>(height), 1}},
                .dstSubresource = Layers,
                .dstOffsets = {{0, 0, 0}, {Width, Height, 1}}};
            cmdbuf.BlitImage(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, blit, VK_FILTER_NEAREST);
            const auto ready = Barrier(image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, ready);
            const VkBufferImageCopy copy{.imageSubresource = Layers, .imageExtent = {Width, Height, 1}};
            cmdbuf.CopyImageToBuffer(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, copy);
            const auto restore = Barrier(source, VK_ACCESS_TRANSFER_READ_BIT,
                                         VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            const VkMemoryBarrier host{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                       .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                       .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT |
                                   VK_PIPELINE_STAGE_HOST_BIT, 0, host, nullptr, restore);
            if (pool) {
                cmdbuf.WriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, query + 1);
            }
        });
    }
    if (dropped) {
        DropFlashSample(config->generation, frame);
    }
    NoteFlashCost(config->generation,
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count(),
                  std::nullopt, dropped);
}
} // namespace Vulkan
