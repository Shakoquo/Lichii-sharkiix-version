// SPDX-License-Identifier: GPL-3.0-or-later
// Graphics pipeline libraries (VK_EXT_graphics_pipeline_library): the four parts of a graphics
// pipeline are compiled once per distinct part and shared by every pipeline that uses the same
// part; a pipeline is then a fast link of four ready parts instead of a full compile.

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <span>
#include <unordered_map>

#include "common/common_types.h"
#include "common/logging.h"
#include "common/thread_worker.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

/// Order-dependent 64-bit hash of plain values (FNV-1a over each value's bytes).
class LibraryKeyHasher {
public:
    explicit LibraryKeyHasher(u64 seed) : hash{0xcbf29ce484222325ULL ^ seed} {}

    template <typename T>
    LibraryKeyHasher& Add(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        for (const unsigned char byte : bytes) {
            hash = (hash ^ byte) * 0x100000001b3ULL;
        }
        return *this;
    }

    u64 Get() const {
        return hash;
    }

private:
    u64 hash;
};

class PipelineLibraryCache {
public:
    static PipelineLibraryCache& Instance() {
        // Never destroyed: libraries left at exit belong to a device that is already gone.
        static auto* const cache = new PipelineLibraryCache;
        return *cache;
    }

    /// Returns the library for key, building it with build() if no thread has yet. A failed
    /// build is remembered as VK_NULL_HANDLE so the caller falls back to a full pipeline.
    VkPipeline GetOrBuild(u64 key, const std::function<vk::Pipeline()>& build) {
        std::shared_ptr<Entry> entry;
        bool builder = false;
        {
            std::scoped_lock lock{mutex};
            auto [it, inserted] = entries.try_emplace(key);
            if (inserted) {
                it->second = std::make_shared<Entry>();
                builder = true;
            }
            entry = it->second;
        }
        if (builder) {
            misses.fetch_add(1, std::memory_order_relaxed);
            vk::Pipeline library;
            try {
                library = build();
            } catch (const vk::Exception& exception) {
                LOG_ERROR(Render_Vulkan, "Pipeline library build failed: {}", exception.what());
            }
            std::scoped_lock lock{entry->mutex};
            entry->library = std::move(library);
            entry->done = true;
            entry->condvar.notify_all();
        } else {
            hits.fetch_add(1, std::memory_order_relaxed);
            std::unique_lock lock{entry->mutex};
            entry->condvar.wait(lock, [&] { return entry->done; });
        }
        const u64 total = hits.load(std::memory_order_relaxed) + misses.load(std::memory_order_relaxed);
        if (total == 1 || total % 500 == 0) {
            LOG_INFO(Render_Vulkan, "Pipeline parts: {} reused, {} compiled",
                     hits.load(std::memory_order_relaxed), misses.load(std::memory_order_relaxed));
        }
        return *entry->library;
    }

    /// Destroys every library; called before the device goes away.
    void Clear() {
        std::scoped_lock lock{mutex};
        if (!entries.empty()) {
            LOG_INFO(Render_Vulkan, "Pipeline parts at exit: {} reused, {} compiled",
                     hits.load(std::memory_order_relaxed), misses.load(std::memory_order_relaxed));
        }
        entries.clear();
    }

private:
    struct Entry {
        std::mutex mutex;
        std::condition_variable condvar;
        bool done = false;
        vk::Pipeline library;
    };

    std::mutex mutex;
    std::unordered_map<u64, std::shared_ptr<Entry>> entries;
    std::atomic<u64> hits{0};
    std::atomic<u64> misses{0};
};

/// An optimized relink of a fast-linked pipeline, built in the background. The fast link is
/// drawn with until `ready`; the pipeline that owns the task sets `cancelled` when it goes away.
struct PipelineOptimizeTask {
    std::mutex mutex;
    bool cancelled = false;
    std::atomic<bool> ready{false};
    std::atomic<bool> queued{false};
    vk::Pipeline optimized;
    /// The relink, queued the first time the pipeline is drawn with.
    std::function<void()> job;
};

/// Low-priority threads that relink fast-linked pipelines with full optimization.
class PipelineOptimizer {
public:
    static PipelineOptimizer& Instance() {
        // Never destroyed, like the library cache: its threads may outlive static destruction.
        static auto* const optimizer = new PipelineOptimizer;
        return *optimizer;
    }

    static bool Enabled() {
        static const bool enabled = [] {
            const char* value = std::getenv("EDEN_NX2_GPL_OPTIMIZE");
            return value == nullptr || value[0] != '0';
        }();
        return enabled;
    }

    void Queue(std::function<void()> job) {
        workers.QueueWork([this, job = std::move(job)] {
            std::shared_lock lock{run_mutex};
            if (stopping.load(std::memory_order_acquire)) {
                return;
            }
            job();
        });
    }

    /// Lets queued jobs run again (a game is starting).
    void Start() {
        stopping.store(false, std::memory_order_release);
    }

    /// Drops queued jobs and waits for running ones (the game's pipelines are going away).
    void Stop() {
        stopping.store(true, std::memory_order_release);
        std::unique_lock lock{run_mutex};
    }

private:
    PipelineOptimizer()
        : workers{std::clamp<size_t>(std::thread::hardware_concurrency() / 4, 1, 4),
                  "VkPipelineOptimize", {}, Common::ThreadPlacement::Background} {}

    std::shared_mutex run_mutex;
    std::atomic<bool> stopping{false};
    Common::ThreadWorker workers;
};

} // namespace Vulkan
