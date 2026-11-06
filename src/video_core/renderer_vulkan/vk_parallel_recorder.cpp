// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "common/thread.h"
#include "video_core/renderer_vulkan/vk_parallel_recorder.h"
#include "video_core/host_gpu_profile.h"
#include "video_core/nx2_profile.h"
#include "video_core/renderer_vulkan/vk_resolved_batch.h"

namespace Vulkan {

struct ParallelRecordingQueue::Impl {
    struct Slot {
        u64 retire_tick{};
    };

    struct Pass {
        std::vector<VkCommandBuffer> results;
        size_t completed{};
        std::exception_ptr error;
    };

    struct Job {
        u32 slot;
        size_t index;
        const ResolvedBatch* batch;
        Pass* pass;
    };

    Impl(ParallelRecordingLimits limits_, Encoder encoder_)
        : limits{limits_}, encoder{std::move(encoder_)} {
        if (limits.workers == 0 || limits.queued_jobs == 0 || limits.secondary_buffers == 0 ||
            !encoder) {
            throw std::invalid_argument{"Invalid parallel recording limits or encoder"};
        }
        slots.resize(limits.secondary_buffers);
        jobs.resize(limits.queued_jobs);
        workers.reserve(limits.workers);
        try {
            for (u32 worker = 0; worker < limits.workers; ++worker) {
                workers.emplace_back([this, worker] { Run(worker); });
            }
        } catch (...) {
            StopAndJoin();
            throw;
        }
    }

    ~Impl() {
        StopAndJoin();
    }

    void Run(u32 worker) {
        const auto name = "Vulkan recorder " + std::to_string(worker);
        const auto thread_name = "VkRecorder" + std::to_string(worker);
        Common::SetCurrentThreadName(thread_name.c_str());
        std::unique_lock lock{mutex};
        for (;;) {
            auto next = jobs.end();
            {
                const VideoCommon::HostGpuProfile::Scope wait{
                    VideoCommon::HostGpuProfile::Phase::Wait};
                changed.wait(lock, [&] {
                    next = std::find_if(jobs.begin(), jobs.end(), [&](const auto& job) {
                        return job && job->slot % limits.workers == worker;
                    });
                    return stopping || next != jobs.end();
                });
            }
            if (next == jobs.end()) {
                return;
            }
            const Job job = **next;
            next->reset();
            changed.notify_all();
            lock.unlock();
            VkCommandBuffer result{};
            std::exception_ptr error;
            try {
                const VideoCommon::HostGpuProfile::Scope recording{
                    VideoCommon::HostGpuProfile::Phase::Worker};
                result = encoder(worker, job.slot / limits.workers, *job.batch);
                if (result == VK_NULL_HANDLE) {
                    throw std::runtime_error{"Secondary encoder returned a null command buffer"};
                }
            } catch (...) {
                error = std::current_exception();
            }
            VideoCommon::Nx2Profile::Report(name.c_str());
            lock.lock();
            job.pass->results[job.index] = result;
            if (error && !job.pass->error) {
                job.pass->error = error;
            }
            ++job.pass->completed;
            changed.notify_all();
        }
    }

    std::optional<std::vector<VkCommandBuffer>> RecordPass(std::span<const ResolvedBatch> batches,
                                                         u64 completed_tick) {
        std::unique_lock call_lock{call_mutex};
        std::unique_lock lock{mutex};
        if (stopping) {
            throw std::logic_error{"Parallel recording queue is stopped"};
        }
        if (batches.empty()) {
            return std::vector<VkCommandBuffer>{};
        }
        const auto& first = batches.front();
        if (!first.epoch || first.epoch->tick <= completed_tick) {
            throw std::invalid_argument{"Recording requires an uncompleted submission epoch"};
        }
        const u64 tick = first.epoch->tick;
        for (const auto& batch : batches) {
            if (!batch.epoch || batch.epoch->tick != tick) {
                throw std::invalid_argument{"A recorded pass must belong to one submission epoch"};
            }
        }
        if (batches.size() > slots.size()) {
            return std::nullopt;
        }
        std::vector<u32> reserved;
        reserved.reserve(batches.size());
        for (u32 slot = 0; slot < slots.size() && reserved.size() < batches.size(); ++slot) {
            if (slots[slot].retire_tick <= completed_tick) {
                reserved.push_back(slot);
            }
        }
        if (reserved.size() != batches.size()) {
            return std::nullopt;
        }
        Pass pass{.results = std::vector<VkCommandBuffer>(batches.size()),
                  .completed = 0,
                  .error = {}};
        for (const u32 slot : reserved) {
            slots[slot].retire_tick = tick;
        }
        for (size_t index = 0; index < batches.size(); ++index) {
            auto free = jobs.end();
            {
                const VideoCommon::HostGpuProfile::Scope wait{
                    VideoCommon::HostGpuProfile::Phase::Wait};
                changed.wait(lock, [&] {
                    free = std::find_if(jobs.begin(), jobs.end(), [](const auto& job) { return !job; });
                    return free != jobs.end();
                });
            }
            *free = Job{reserved[index], index, &batches[index], &pass};
            changed.notify_all();
        }
        {
            const VideoCommon::HostGpuProfile::Scope wait{
                VideoCommon::HostGpuProfile::Phase::Wait};
            changed.wait(lock, [&] { return pass.completed == batches.size(); });
        }
        if (pass.error) {
            for (const u32 slot : reserved) {
                slots[slot].retire_tick = 0;
            }
            std::rethrow_exception(pass.error);
        }
        highest_tick = std::max(highest_tick, tick);
        return std::move(pass.results);
    }

    void StopAndJoin() {
        std::scoped_lock call_lock{call_mutex};
        {
            std::scoped_lock lock{mutex};
            stopping = true;
        }
        changed.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    ParallelRecordingLimits limits;
    Encoder encoder;
    std::mutex call_mutex;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<Slot> slots;
    std::vector<std::optional<Job>> jobs;
    std::vector<std::thread> workers;
    u64 highest_tick{};
    bool stopping{};
};

ParallelRecordingQueue::ParallelRecordingQueue(ParallelRecordingLimits limits, Encoder encoder)
    : impl{std::make_unique<Impl>(limits, std::move(encoder))} {}

ParallelRecordingQueue::~ParallelRecordingQueue() = default;

std::optional<std::vector<VkCommandBuffer>> ParallelRecordingQueue::RecordPass(
    std::span<const ResolvedBatch> batches, u64 completed_tick) {
    return impl->RecordPass(batches, completed_tick);
}

void ParallelRecordingQueue::StopAndJoin() {
    impl->StopAndJoin();
}

u64 ParallelRecordingQueue::HighestTick() const {
    std::scoped_lock lock{impl->mutex};
    return impl->highest_tick;
}

struct ParallelRecorder::Impl {
    struct Worker {
        vk::CommandPool pool;
        std::vector<vk::CommandBuffers> buffers;
    };

    Impl(const vk::Device& logical_, const vk::DeviceDispatch& dispatch_, u32 graphics_family_,
         Limits limits)
        : logical{logical_}, dispatch{dispatch_}, graphics_family{graphics_family_},
          workers(limits.workers),
          queue{limits, [this](u32 worker, u32 slot, const ResolvedBatch& batch) {
                    return Encode(worker, slot, batch);
                }} {}

    VkCommandBuffer Encode(u32 worker_index, u32 slot, const ResolvedBatch& batch) {
        auto& worker = workers[worker_index];
        {
            const VideoCommon::HostGpuProfile::Scope pool{
                VideoCommon::HostGpuProfile::Phase::Pool};
            if (!worker.pool) {
                worker.pool = logical.CreateCommandPool({
                    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                             VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                    .queueFamilyIndex = graphics_family,
                });
            }
            while (worker.buffers.size() <= slot) {
                worker.buffers.push_back(worker.pool.Allocate(1, VK_COMMAND_BUFFER_LEVEL_SECONDARY));
            }
        }
        const VkCommandBuffer handle = worker.buffers[slot][0];
        vk::CommandBuffer command_buffer{handle, dispatch};
        const VkCommandBufferInheritanceInfo inheritance{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
            .pNext = nullptr,
            .renderPass = batch.renderpass,
            .subpass = batch.subpass,
            .framebuffer = batch.framebuffer,
            .occlusionQueryEnable = VK_FALSE,
            .queryFlags = 0,
            .pipelineStatistics = 0,
        };
        command_buffer.Begin({
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                     VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
            .pInheritanceInfo = &inheritance,
        });
        batch.Replay(command_buffer);
        command_buffer.End();
        return handle;
    }

    const vk::Device& logical;
    const vk::DeviceDispatch& dispatch;
    u32 graphics_family;
    std::vector<Worker> workers;
    ParallelRecordingQueue queue;
};

ParallelRecorder::ParallelRecorder(const vk::Device& logical, const vk::DeviceDispatch& dispatch,
                                   u32 graphics_family, Limits limits)
    : impl{std::make_unique<Impl>(logical, dispatch, graphics_family, limits)} {}

ParallelRecorder::~ParallelRecorder() = default;

std::optional<std::vector<VkCommandBuffer>> ParallelRecorder::RecordPass(
    std::span<const ResolvedBatch> batches, u64 completed_tick) {
    return impl->queue.RecordPass(batches, completed_tick);
}

void ParallelRecorder::StopAndJoin() {
    impl->queue.StopAndJoin();
}

u64 ParallelRecorder::HighestTick() const {
    return impl->queue.HighestTick();
}

} // namespace Vulkan
