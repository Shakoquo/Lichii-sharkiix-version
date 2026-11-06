// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

struct ResolvedBatch;

struct ParallelRecordingLimits {
    u32 workers{2};
    u32 queued_jobs{8};
    u32 secondary_buffers{256};
};

class ParallelRecordingQueue {
public:
    using Encoder = std::function<VkCommandBuffer(u32 worker, u32 slot, const ResolvedBatch&)>;

    ParallelRecordingQueue(ParallelRecordingLimits limits, Encoder encoder);
    ~ParallelRecordingQueue();

    std::optional<std::vector<VkCommandBuffer>> RecordPass(std::span<const ResolvedBatch> batches,
                                                         u64 completed_tick);
    void StopAndJoin();
    u64 HighestTick() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class ParallelRecorder {
public:
    using Limits = ParallelRecordingLimits;

    ParallelRecorder(const vk::Device& logical, const vk::DeviceDispatch& dispatch,
                     u32 graphics_family, Limits limits = {});
    ~ParallelRecorder();

    std::optional<std::vector<VkCommandBuffer>> RecordPass(std::span<const ResolvedBatch> batches,
                                                         u64 completed_tick);
    void StopAndJoin();
    u64 HighestTick() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan
