// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <latch>
#include <stdexcept>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_parallel_recorder.h"
#include "video_core/renderer_vulkan/vk_resolved_batch.h"

namespace {

VkCommandBuffer Handle(u64 value) {
    return reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(value + 1));
}

std::vector<Vulkan::ResolvedBatch> Batches(u64 tick, u32 count) {
    std::vector<Vulkan::ResolvedBatch> batches(count);
    const auto epoch = std::make_shared<const Vulkan::SubmissionEpoch>(
        Vulkan::SubmissionEpoch{tick});
    for (u32 i = 0; i < count; ++i) {
        batches[i].epoch = epoch;
        batches[i].sequence = i;
    }
    return batches;
}

} // namespace

TEST_CASE("Parallel recording returns guest order despite reverse worker completion",
          "[video_core][parallel_recorder]") {
    std::latch first_started{1};
    std::latch second_finished{1};
    std::atomic<bool> first_saw_second{};
    std::array<std::thread::id, 2> threads;
    Vulkan::ParallelRecordingQueue queue{
        {.workers = 2, .queued_jobs = 1, .secondary_buffers = 2},
        [&](u32 worker, u32, const Vulkan::ResolvedBatch& batch) {
            threads[worker] = std::this_thread::get_id();
            if (batch.sequence == 0) {
                first_started.count_down();
                second_finished.wait();
                first_saw_second = true;
            } else {
                first_started.wait();
                second_finished.count_down();
            }
            return Handle(batch.sequence);
        }};
    const auto result = queue.RecordPass(Batches(7, 2), 6);
    REQUIRE(result);
    CHECK(*result == std::vector<VkCommandBuffer>{Handle(0), Handle(1)});
    CHECK(first_saw_second);
    CHECK(threads[0] != threads[1]);
    CHECK(threads[0] != std::this_thread::get_id());
    CHECK(threads[1] != std::this_thread::get_id());
    CHECK(queue.HighestTick() == 7);
}

TEST_CASE("Parallel recording reserves whole passes and retires by explicit GPU tick",
          "[video_core][parallel_recorder]") {
    std::atomic<u32> calls{};
    std::array<std::thread::id, 2> owners;
    std::atomic<bool> stable_owners{true};
    Vulkan::ParallelRecordingQueue queue{
        {.workers = 2, .queued_jobs = 1, .secondary_buffers = 4},
        [&](u32 worker, u32 slot, const Vulkan::ResolvedBatch&) {
            if (owners[worker] == std::thread::id{}) {
                owners[worker] = std::this_thread::get_id();
            } else if (owners[worker] != std::this_thread::get_id()) {
                stable_owners = false;
            }
            ++calls;
            return Handle(worker * 10 + slot);
        }};
    CHECK_FALSE(queue.RecordPass(Batches(7, 5), 6));
    CHECK(calls == 0);
    const auto first = queue.RecordPass(Batches(7, 2), 6);
    REQUIRE(first);
    CHECK_FALSE(queue.RecordPass(Batches(7, 3), 6));
    CHECK(calls == 2);
    const auto second = queue.RecordPass(Batches(7, 2), 6);
    REQUIRE(second);
    CHECK(*second != *first);
    CHECK_FALSE(queue.RecordPass(Batches(8, 1), 6));
    CHECK(calls == 4);
    const auto reused = queue.RecordPass(Batches(8, 2), 7);
    REQUIRE(reused);
    CHECK(*reused == *first);
    CHECK(calls == 6);
    CHECK(stable_owners);
    CHECK(queue.HighestTick() == 8);
}

TEST_CASE("Parallel recording bounded queue progresses through more jobs than queue slots",
          "[video_core][parallel_recorder]") {
    std::atomic<u32> calls{};
    Vulkan::ParallelRecordingQueue queue{
        {.workers = 2, .queued_jobs = 1, .secondary_buffers = 64},
        [&](u32, u32, const Vulkan::ResolvedBatch& batch) {
            ++calls;
            return Handle(batch.sequence);
        }};
    const auto result = queue.RecordPass(Batches(1, 64), 0);
    REQUIRE(result);
    REQUIRE(result->size() == 64);
    for (u32 i = 0; i < 64; ++i) {
        CHECK((*result)[i] == Handle(i));
    }
    CHECK(calls == 64);
}

TEST_CASE("Parallel recording propagates failures after joining every job",
          "[video_core][parallel_recorder]") {
    std::latch other_started{1};
    std::latch release_other{1};
    std::atomic<bool> other_finished{};
    std::atomic<bool> fail{true};
    Vulkan::ParallelRecordingQueue queue{
        {.workers = 2, .queued_jobs = 1, .secondary_buffers = 2},
        [&](u32, u32, const Vulkan::ResolvedBatch& batch) {
            if (fail && batch.sequence == 0) {
                other_started.wait();
                throw Vulkan::vk::Exception{VK_ERROR_DEVICE_LOST};
            }
            if (fail) {
                other_started.count_down();
                release_other.wait();
                other_finished = true;
            }
            return Handle(batch.sequence);
        }};
    auto result = std::async(std::launch::async, [&] { return queue.RecordPass(Batches(3, 2), 0); });
    other_started.wait();
    CHECK_FALSE(other_finished);
    release_other.count_down();
    REQUIRE_THROWS_AS(result.get(), Vulkan::vk::Exception);
    CHECK(other_finished);
    CHECK(queue.HighestTick() == 0);
    fail = false;
    REQUIRE(queue.RecordPass(Batches(3, 2), 0));
}

TEST_CASE("Parallel recording shutdown drains an active pass and is idempotent",
          "[video_core][parallel_recorder]") {
    std::latch started{1};
    std::latch release{1};
    std::atomic<bool> joined{};
    Vulkan::ParallelRecordingQueue queue{
        {.workers = 1, .queued_jobs = 1, .secondary_buffers = 2},
        [&](u32, u32, const Vulkan::ResolvedBatch& batch) {
            if (batch.sequence == 0) {
                started.count_down();
                release.wait();
            }
            return Handle(batch.sequence);
        }};
    auto result = std::async(std::launch::async, [&] { return queue.RecordPass(Batches(1, 2), 0); });
    started.wait();
    auto stop = std::async(std::launch::async, [&] {
        queue.StopAndJoin();
        joined = true;
    });
    CHECK_FALSE(joined);
    release.count_down();
    REQUIRE(result.get());
    stop.get();
    CHECK(joined);
    queue.StopAndJoin();
    CHECK_THROWS_AS(queue.RecordPass(Batches(2, 1), 1), std::logic_error);
}

TEST_CASE("Parallel recording rejects invalid epochs without dispatching work",
          "[video_core][parallel_recorder]") {
    std::atomic<u32> calls{};
    Vulkan::ParallelRecordingQueue queue{
        {}, [&](u32, u32, const Vulkan::ResolvedBatch&) {
            ++calls;
            return Handle(0);
        }};
    CHECK(queue.RecordPass({}, 0)->empty());
    CHECK_THROWS_AS(queue.RecordPass(Batches(0, 1), 0), std::invalid_argument);
    CHECK_THROWS_AS(queue.RecordPass(Batches(3, 1), 3), std::invalid_argument);
    auto mixed = Batches(3, 2);
    mixed[1].epoch = Batches(4, 1)[0].epoch;
    CHECK_THROWS_AS(queue.RecordPass(mixed, 0), std::invalid_argument);
    mixed[0].epoch.reset();
    CHECK_THROWS_AS(queue.RecordPass(mixed, 0), std::invalid_argument);
    CHECK(calls == 0);
}

TEST_CASE("Parallel recording rejects invalid capacities and null encoder results",
          "[video_core][parallel_recorder]") {
    const auto encoder = [](u32, u32, const Vulkan::ResolvedBatch&) { return Handle(0); };
    CHECK_THROWS_AS((Vulkan::ParallelRecordingQueue{{.workers = 0}, encoder}), std::invalid_argument);
    CHECK_THROWS_AS((Vulkan::ParallelRecordingQueue{{.queued_jobs = 0}, encoder}), std::invalid_argument);
    CHECK_THROWS_AS((Vulkan::ParallelRecordingQueue{{.secondary_buffers = 0}, encoder}),
                    std::invalid_argument);
    CHECK_THROWS_AS((Vulkan::ParallelRecordingQueue{{}, {}}), std::invalid_argument);
    Vulkan::ParallelRecordingQueue queue{
        {}, [](u32, u32, const Vulkan::ResolvedBatch&) -> VkCommandBuffer { return VK_NULL_HANDLE; }};
    CHECK_THROWS_AS(queue.RecordPass(Batches(1, 1), 0), std::runtime_error);
}
