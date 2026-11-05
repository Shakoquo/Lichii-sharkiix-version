// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <unordered_map>
#include <mutex>
#include <optional>

#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/nx2_profile.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/engines/puller.h"
#include "video_core/gpu.h"
#include "video_core/gpu_split.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra::Engines {

namespace {

constexpr u32 Gf100BindClassMask = 0xffff;
constexpr u32 Gf100BindValidMask =  0x1f0000 | Gf100BindClassMask;

// Reads a semaphore word, flushing a GPU-written copy out of the buffer cache first.
u32 ReadSemaphoreWord(DmaPusher& dma_pusher, GPUVAddr address) {
    if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(address);
        device && dma_pusher.rasterizer->MustFlushRegion(*device, sizeof(u32),
                                                          VideoCommon::CacheType::BufferCache)) {
        VideoCommon::GpuSplit::Get().Run(
            [rasterizer = dma_pusher.rasterizer, device = *device] {
                rasterizer->FlushRegion(device, sizeof(u32), VideoCommon::CacheType::BufferCache);
            },
            VideoCommon::DrainSite::SemaphoreFlush);
    }
    return dma_pusher.memory_manager.Read<u32>(address);
}

// With the GPU thread split (gpu_split.h) the rasterizer belongs to the back thread: work that
// only feeds it runs there in order; reads of what it wrote first wait for it (DrainBack).
template <typename Func>
void OnBack(const Func& func) {
    if (auto& split = VideoCommon::GpuSplit::Get(); split.Active()) {
        split.Defer(func);
    } else {
        func();
    }
}

void DrainBack() {
    VideoCommon::GpuSplit::Get().Drain(VideoCommon::DrainSite::Acquire);
}

// Semaphore releases sent to the back whose payload may not be in guest memory yet (the back
// writes it once the host GPU gets there), by device address. Everything goes to the host in
// one ordered stream, so an acquire that such a release satisfies needs no wait: the release
// runs before the commands after the acquire. Front thread only.
std::unordered_map<u64, u32>& QueuedReleases() {
    static std::unordered_map<u64, u32> releases;
    return releases;
}

void NoteQueuedRelease(std::optional<DAddr> device, u32 payload) {
    if (device && VideoCommon::GpuSplit::Get().Active()) {
        QueuedReleases()[*device] = payload;
    }
}

// EDEN_NX2_ACQUIRE_STATS=1: how each semaphore acquire ends, by address, every 2 s:
// queued release, already in memory, after a drain, after the fences, suspended.
struct AcquireStatEntry {
    std::array<u32, 5> counts{};
    u32 target{};
    u32 current{};
    u64 suspended_ns{};
    u64 max_suspended_ns{};
    u32 resumed{};
    u32 latched{};
    u32 channel_mask{};
    u32 source_mask{};
    u32 operation{};
    u32 queued{};
    u32 back{};
};

bool AcquireStatsEnabled() {
    static const bool enabled = std::getenv("EDEN_NX2_ACQUIRE_STATS") != nullptr;
    return enabled;
}

std::unordered_map<u64, AcquireStatEntry>& AcquireStatEntries() {
    static std::unordered_map<u64, AcquireStatEntry> entries;
    return entries;
}

void AcquireResumeStat(std::optional<DAddr> device, u64 ns, bool latched, s32 channel, u32 source) {
    if (!AcquireStatsEnabled()) {
        return;
    }
    AcquireStatEntry& entry = AcquireStatEntries()[device.value_or(0)];
    entry.suspended_ns += ns;
    entry.max_suspended_ns = (std::max)(entry.max_suspended_ns, ns);
    ++entry.resumed;
    entry.latched += latched ? 1 : 0;
    entry.channel_mask |= 1U << (static_cast<u32>(channel) & 31);
    entry.source_mask |= 1U << (source & 31);
}

void AcquireStat(std::optional<DAddr> device, u32 outcome, u32 target, u32 current) {
    if (!AcquireStatsEnabled()) {
        return;
    }
    using Entry = AcquireStatEntry;
    auto& entries = AcquireStatEntries();
    static auto since = std::chrono::steady_clock::now();
    Entry& entry = entries[device.value_or(0)];
    ++entry.counts[outcome];
    entry.target = target;
    entry.current = current;
    const auto now = std::chrono::steady_clock::now();
    if (now - since < std::chrono::seconds{2}) {
        return;
    }
    since = now;
    std::vector<std::pair<u32, u64>> order;
    for (const auto& [address, e] : entries) {
        order.emplace_back(e.counts[2] + e.counts[3] + e.counts[4], address);
    }
    std::sort(order.rbegin(), order.rend());
    LOG_INFO(HW_GPU, "nx2 acquire stats over 2 s ({} addresses): queued/memory/drain/fences/suspended",
             entries.size());
    for (size_t i = 0; i < (std::min<size_t>)(order.size(), 8); ++i) {
        const Entry& e = entries[order[i].second];
        LOG_INFO(HW_GPU, "  {:#x}: {}/{}/{}/{}/{} (last target {:#x}, memory {:#x}); suspended "
                 "{:.1f} ms in all, avg {:.3f} ms, max {:.2f} ms over {} resumes ({} by GPU write, channels {:#x}, sources {:#x}) op {} queued {:#x} back {:#x}",
                 order[i].second, e.counts[0], e.counts[1], e.counts[2], e.counts[3], e.counts[4],
                 e.target, e.current, static_cast<double>(e.suspended_ns) / 1e6,
                 e.resumed ? static_cast<double>(e.suspended_ns) / 1e6 / e.resumed : 0.0,
                 static_cast<double>(e.max_suspended_ns) / 1e6, e.resumed, e.latched,
                 e.channel_mask, e.source_mask, e.operation, e.queued, e.back);
    }
    entries.clear();
}

// The fence manager records into the scheduler and downloads the CPU's writes over GPU data:
// the back's state, which a drained back still uses from its idle hook (it submits).
void ReleaseFences(DmaPusher& dma_pusher) {
    VideoCommon::GpuSplit::Get().Run(
        [rasterizer = dma_pusher.rasterizer] { rasterizer->ReleaseFences(); },
        VideoCommon::DrainSite::FenceRelease);
}

// Before suspending a channel on an acquire: put the pending releases behind a fence and go
// on without waiting for the host GPU to reach it. The fencing thread runs them when it does
// and the suspended channel resumes from the scheduler's polling. Waiting here (ReleaseFences)
// drained the host GPU at every acquire: ~130 a second in Ocarina of Time, 17% of the GPU
// thread asleep and the GPU idle after each one. EDEN_NX2_SYNC_ACQUIRE=1 waits as before.
void ReleaseFencesForAcquire(DmaPusher& dma_pusher) {
    static const bool sync = [] {
        const char* value = std::getenv("EDEN_NX2_SYNC_ACQUIRE");
        return value != nullptr && value[0] == '1';
    }();
    if (sync) {
        ReleaseFences(dma_pusher);
        return;
    }
    VideoCommon::GpuSplit::Get().Run(
        [rasterizer = dma_pusher.rasterizer] { rasterizer->SignalReference(); },
        VideoCommon::DrainSite::FenceRelease);
}

} // Anonymous namespace

void Nx2NoteQueuedRelease(DAddr device, u32 payload) {
    NoteQueuedRelease(device, payload);
}

// Releases the back has put behind a host fence (compute QMD releases), any thread.
std::mutex& BackReleaseMutex() {
    static std::mutex mutex;
    return mutex;
}
std::unordered_map<u64, u32>& BackReleases() {
    static std::unordered_map<u64, u32> releases;
    return releases;
}
bool Nx2BackReleasesEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("EDEN_NX2_RESUME_BACK");
        return v == nullptr || v[0] != '0';
    }();
    return enabled;
}
void Nx2NoteBackAdd(DAddr device, u32 payload, u32 memory_value) {
    // Opt-in (EDEN_NX2_RESUME_ADD=1): OoT Kokiri forest entrance 20-21 fps -> 28-29, but the
    // game flickers (something reads the compute output before the host GPU wrote it).
    static const bool enabled = [] {
        const char* v = std::getenv("EDEN_NX2_RESUME_ADD");
        return v != nullptr && v[0] == '1';
    }();
    if (!enabled || !Nx2BackReleasesEnabled()) {
        return;
    }
    // Adds queued in order: the value after this one is the last queued value (or memory when
    // none is pending) plus the payload.
    std::scoped_lock lk{BackReleaseMutex()};
    auto& releases = BackReleases();
    const auto it = releases.find(device);
    const u32 base = it != releases.end() ? it->second : memory_value;
    releases[device] = base + payload;
}

void Nx2NoteBackRelease(DAddr device, u32 payload) {
    if (!Nx2BackReleasesEnabled()) {
        return;
    }
    std::scoped_lock lk{BackReleaseMutex()};
    BackReleases()[device] = payload;
}

bool Puller::PendingAcquire::Matches(u32 current) const {
    switch (static_cast<GpuSemaphoreOperation>(operation)) {
    case GpuSemaphoreOperation::AcquireEqual:
        return current == value;
    case GpuSemaphoreOperation::AcquireGequal:
        return current >= value;
    case GpuSemaphoreOperation::AcquireMask:
        return (current & value) != 0;
    default:
        return true;
    }
}

bool Puller::CanResumeAcquire(DmaPusher& dma_pusher) {
    if (!pending_acquire) {
        return true;
    }
    // The release may still be queued for the split GPU thread's back half (and the caches
    // ReadSemaphoreWord asks are its state).
    DrainBack();
    const u32 current = ReadSemaphoreWord(dma_pusher, pending_acquire->address);
    if (Nx2SemaphoreWaiters::Get().Latched(this)) {
        if (static std::atomic<u32> rescued{};
            !pending_acquire->Matches(current) && rescued.fetch_add(1) < 64) {
            LOG_INFO(HW_GPU, "Channel {} met semaphore {:#x} value {:#x} at its release (now {:#x})",
                     dma_pusher.channel_state.bind_id, pending_acquire->address,
                     pending_acquire->value, current);
        }
        return true;
    }
    if (pending_acquire->Matches(current)) {
        return true;
    }
    // Another channel queued the release since: the host runs it before what follows (one
    // ordered stream), as the acquire's own fast path assumes.
    static const bool resume_queued = [] {
        const char* v = std::getenv("EDEN_NX2_RESUME_QUEUED");
        return v == nullptr || v[0] != '0';
    }();
    if (resume_queued) {
        if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(pending_acquire->address)) {
            auto& queued = QueuedReleases();
            if (const auto it = queued.find(*device);
                it != queued.end() && pending_acquire->Matches(it->second)) {
                return true;
            }
            if (Nx2BackReleasesEnabled()) {
                std::scoped_lock lk{BackReleaseMutex()};
                auto& back = BackReleases();
                if (const auto it = back.find(*device); it != back.end()) {
                    if (it->second == current) {
                        back.erase(it);
                    } else if (pending_acquire->Matches(it->second)) {
                        return true;
                    }
                }
            }
        }
    }
    if (!pending_acquire->reported &&
        std::chrono::steady_clock::now() - pending_acquire->since > std::chrono::seconds{2}) {
        pending_acquire->reported = true;
        const auto device = dma_pusher.memory_manager.GpuToCpuAddress(pending_acquire->address);
        LOG_WARNING(HW_GPU, "Channel {} waits on semaphore {:#x} (op {}, {:#x}, now {:#x}) for 2 s; "
                    "device {:#x}, buffer cache holds GPU data: {}, words {:08x} {:08x} {:08x} {:08x}",
                    dma_pusher.channel_state.bind_id, pending_acquire->address,
                    pending_acquire->operation, pending_acquire->value, current, device.value_or(0),
                    device && dma_pusher.rasterizer->MustFlushRegion(*device, 16, VideoCommon::CacheType::All),
                    dma_pusher.memory_manager.Read<u32>(pending_acquire->address & ~GPUVAddr{0xf}),
                    dma_pusher.memory_manager.Read<u32>((pending_acquire->address & ~GPUVAddr{0xf}) + 4),
                    dma_pusher.memory_manager.Read<u32>((pending_acquire->address & ~GPUVAddr{0xf}) + 8),
                    dma_pusher.memory_manager.Read<u32>((pending_acquire->address & ~GPUVAddr{0xf}) + 12));
    }
    return false;
}

bool Puller::ResumeAcquire(DmaPusher& dma_pusher) {
    if (!CanResumeAcquire(dma_pusher)) {
        return false;
    }
    // Only a resume from a suspension counts: DmaPusher::Step calls this before every entry, and
    // counting those made the count odd ("suspended") at random, so submit-time snapshots were
    // thrown away for about half of all entries (CommandList::snapshot_suspensions).
    if (pending_acquire) {
        if (AcquireStatsEnabled()) {
            const u32 source = Nx2SemaphoreWaiters::Get().LatchSource(this);
            const bool latched = Nx2SemaphoreWaiters::Get().Latched(this);
            AcquireResumeStat(dma_pusher.memory_manager.GpuToCpuAddress(pending_acquire->address),
                              static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                   std::chrono::steady_clock::now() -
                                                   pending_acquire->since)
                                                   .count()),
                              latched, dma_pusher.channel_state.bind_id, source);
        }
        pending_acquire.reset();
        dma_pusher.suspensions.fetch_add(1, std::memory_order_release);
        Nx2SemaphoreWaiters::Get().Remove(this);
    }
    return true;
}

void Puller::ProcessBindMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    LOG_DEBUG(HW_GPU, "Binding subchannel {} to engine {:#x}", method_call.subchannel, method_call.argument);
    u32 engine = method_call.argument;
    if ((engine & ~Gf100BindClassMask) != 0 && (engine & ~Gf100BindValidMask) == 0) {
        engine &= Gf100BindClassMask;
    }
    // Switch 2 binds Ampere classes; run them on the Maxwell engines (same method layout for
    // most state). Class 0 unbinds the subchannel.
    switch (engine & 0xFFFF) {
    case 0xC797: // AMPERE_B
        engine = static_cast<u32>(EngineID::MAXWELL_B);
        break;
    case 0xC7C0: // AMPERE_COMPUTE_B
        engine = static_cast<u32>(EngineID::KEPLER_COMPUTE_B);
        break;
    case 0xC7B5: // AMPERE_DMA_COPY_B
        engine = static_cast<u32>(EngineID::MAXWELL_DMA_COPY_A);
        break;
    case 0x0:
        if (g_nx2_ampere) {
            LOG_DEBUG(HW_GPU, "Switch 2 unbinds subchannel {}", method_call.subchannel);
            return;
        }
        break;
    default:
        break;
    }

    const auto engine_id = static_cast<EngineID>(engine);
    bound_engines[method_call.subchannel] = engine_id;
    switch (engine_id) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->fermi_2d, method_call.subchannel, EngineTypes::Fermi2D);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_3d, method_call.subchannel, EngineTypes::Maxwell3D);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->kepler_compute, method_call.subchannel, EngineTypes::KeplerCompute);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_dma, method_call.subchannel, EngineTypes::MaxwellDMA);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->kepler_memory, method_call.subchannel, EngineTypes::KeplerMemory);
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->nv01_timer, method_call.subchannel, EngineTypes::Nv01Timer);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine {:04X}", engine_id);
        break;
    }
}

void Puller::ProcessFenceActionMethod(DmaPusher& dma_pusher) {
    switch (regs.fence_action.op) {
    case Puller::FenceOperation::Acquire:
        // UNIMPLEMENTED_MSG("Channel Scheduling pending.");
        // WaitFence(regs.fence_action.syncpoint_id, regs.fence_value);
        OnBack([rasterizer = dma_pusher.rasterizer] { rasterizer->ReleaseFences(); });
        VideoCommon::GpuSplit::Get().NoteSync();
        break;
    case Puller::FenceOperation::Increment:
        OnBack([rasterizer = dma_pusher.rasterizer, id = static_cast<u32>(regs.fence_action.syncpoint_id.Value())] {
            rasterizer->SignalSyncPoint(id);
        });
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented operation {}", regs.fence_action.op.Value());
        break;
    }
}

void Puller::ProcessSemaphoreTriggerMethod(DmaPusher& dma_pusher) {
    const auto semaphoreOperationMask = 0xF;
    const auto op = GpuSemaphoreOperation(regs.semaphore_trigger & semaphoreOperationMask);
    // EDEN_NX2_SEM_WATCH=<gpu address>: log every release/acquire on the 16 bytes there.
    static const GPUVAddr watch = [] {
        const char* value = std::getenv("EDEN_NX2_SEM_WATCH");
        return value ? GPUVAddr{std::strtoull(value, nullptr, 16)} : GPUVAddr{};
    }();
    if (watch != 0 && (regs.semaphore_address.SemaphoreAddress() & ~GPUVAddr{0xf}) == watch) {
        LOG_INFO(HW_GPU, "Semaphore watch: channel {} as {} device {:#x} op {} {:#x} value {:#x} (now {:#x})",
                 dma_pusher.channel_state.bind_id, dma_pusher.memory_manager.GetID(),
                 dma_pusher.memory_manager.GpuToCpuAddress(regs.semaphore_address.SemaphoreAddress()).value_or(0),
                 static_cast<u32>(op),
                 regs.semaphore_address.SemaphoreAddress(), regs.semaphore_sequence,
                 dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress()));
    }
    if (op == GpuSemaphoreOperation::WriteLong) {
        const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
        const u32 payload = regs.semaphore_sequence;
        // RELEASE_SIZE (bit 24) set means a 4-byte release without the timestamp; writing 16
        // bytes there clobbers the words next to it (Switch 2 packs semaphores tightly).
        const auto properties = g_nx2_ampere && (regs.semaphore_trigger & (1U << 24)) != 0
                                    ? VideoCommon::QueryPropertiesFlags::IsAFence
                                    : VideoCommon::QueryPropertiesFlags::HasTimeout;
        OnBack([rasterizer = dma_pusher.rasterizer, sequence_address, properties, payload] {
            rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload, properties, payload, 0);
        });
        NoteQueuedRelease(dma_pusher.memory_manager.GpuToCpuAddress(sequence_address), payload);
        // A front macro's DMA read of these bytes must wait for it.
        if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(sequence_address)) {
            VideoCommon::GpuSplit::Get().NoteMemoryWriterRange(*device, 16);
        } else {
            VideoCommon::GpuSplit::Get().NoteMemoryWriter();
        }
    } else if (g_nx2_ampere) {
        // Switch 2 waits on semaphores the CPU (or another channel) releases, e.g. once it has
        // written the commands this channel runs next: suspend the channel until the value is
        // met instead of reading ahead (approach from changes.patch).
        PendingAcquire acquire{
            .address = regs.semaphore_address.SemaphoreAddress(),
            .value = regs.semaphore_sequence,
            .operation = static_cast<u32>(op),
            .since = std::chrono::steady_clock::now(),
            .reported = false,
        };
        if (op != GpuSemaphoreOperation::AcquireEqual && op != GpuSemaphoreOperation::AcquireGequal &&
            op != GpuSemaphoreOperation::AcquireMask) {
            LOG_ERROR(HW_GPU, "Invalid semaphore operation {}", static_cast<u32>(op));
            return;
        }
        VideoCommon::Nx2Profile::Add(VideoCommon::Nx2Profile::Counter::AcquireChecks, 1);
        // Most acquires wait on a value the CPU (or work the back thread already ran) has
        // written: met in guest memory now, the channel goes on, and the back thread keeps the
        // order of everything queued. Draining first made the front wait for the back thread at
        // every such acquire (a third of the front thread's time in OoT's fields).
        static const bool drain_always = std::getenv("EDEN_NX2_ACQUIRE_DRAIN") != nullptr;
        const auto acquire_device = dma_pusher.memory_manager.GpuToCpuAddress(acquire.address);
        const u32 first_read = dma_pusher.memory_manager.Read<u32>(acquire.address);
        if (!drain_always) {
            auto& queued = QueuedReleases();
            if (acquire_device) {
                if (const auto it = queued.find(*acquire_device); it != queued.end()) {
                    if (first_read == it->second) {
                        queued.erase(it); // landed: guest memory tells from now on
                    } else if (acquire.Matches(it->second)) {
                        AcquireStat(acquire_device, 0, acquire.value, first_read);
                        return;
                    }
                }
            }
            if (acquire.Matches(first_read)) {
                AcquireStat(acquire_device, 1, acquire.value, first_read);
                return;
            }
        }
        DrainBack();
        if (acquire.Matches(ReadSemaphoreWord(dma_pusher, acquire.address))) {
            AcquireStat(acquire_device, 2, acquire.value, first_read);
            return;
        }
        // Registered before the fences run, so a release among them that meets the value counts
        // even if a later one overwrites it.
        auto& waiters = Nx2SemaphoreWaiters::Get();
        if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(acquire.address)) {
            waiters.Add(this, *device, acquire.value, acquire.operation);
        }
        ReleaseFencesForAcquire(dma_pusher);
        if (!waiters.Latched(this) && !acquire.Matches(ReadSemaphoreWord(dma_pusher, acquire.address))) {
            pending_acquire = acquire;
            dma_pusher.suspensions.fetch_add(1, std::memory_order_release);
            VideoCommon::Nx2Profile::Add(VideoCommon::Nx2Profile::Counter::AcquireWaits, 1);
            AcquireStat(acquire_device, 4, acquire.value, first_read);
            if (AcquireStatsEnabled() && acquire_device) {
                AcquireStatEntry& e = AcquireStatEntries()[*acquire_device];
                e.operation = acquire.operation;
                const auto qit = QueuedReleases().find(*acquire_device);
                e.queued = qit != QueuedReleases().end() ? qit->second : 0xffffffffU;
                std::scoped_lock lk{BackReleaseMutex()};
                const auto bit = BackReleases().find(*acquire_device);
                e.back = bit != BackReleases().end() ? bit->second : 0xffffffffU;
            }
        } else {
            waiters.Remove(this);
            AcquireStat(acquire_device, 3, acquire.value, first_read);
        }
    } else {
        DrainBack();
        do {
            const u32 word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
            regs.acquire_source = true;
            regs.acquire_value = regs.semaphore_sequence;
            if (op == GpuSemaphoreOperation::AcquireEqual) {
                regs.acquire_active = true;
                regs.acquire_mode = false;
                if (word != regs.acquire_value) {
                    ReleaseFences(dma_pusher);
                    continue;
                }
            } else if (op == GpuSemaphoreOperation::AcquireGequal) {
                regs.acquire_active = true;
                regs.acquire_mode = true;
                if (word < regs.acquire_value) {
                    ReleaseFences(dma_pusher);
                    continue;
                }
            } else if (op == GpuSemaphoreOperation::AcquireMask) {
                if (word && regs.semaphore_sequence == 0) {
                    ReleaseFences(dma_pusher);
                    continue;
                }
            } else {
                LOG_ERROR(HW_GPU, "Invalid semaphore operation");
            }
        } while (false);
    }
}

void Puller::ProcessSemaphoreRelease(DmaPusher& dma_pusher) {
    const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
    const u32 payload = regs.semaphore_release;
    OnBack([rasterizer = dma_pusher.rasterizer, sequence_address, payload] {
        rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload,
                          VideoCommon::QueryPropertiesFlags::IsAFence, payload, 0);
    });
    if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(sequence_address)) {
        Nx2NoteBackRelease(*device, payload);
    }
}

void Puller::ProcessSemaphoreAcquire(DmaPusher& dma_pusher) {
    DrainBack();
    u32 word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
    const auto value = regs.semaphore_acquire;
    while (word != value) {
        regs.acquire_active = true;
        regs.acquire_value = value;
        ReleaseFences(dma_pusher);
        word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
        // TODO(kemathe73) figure out how to do the acquire_timeout
        regs.acquire_mode = false;
        regs.acquire_source = false;
    }
}

// Ampere SEM_EXECUTE (method 0x1B): operation in bits 2:0 on the semaphore at
// SEM_ADDR_HI:SEM_ADDR_LO (methods 0x18/0x17) with payload SEM_PAYLOAD_LO (0x19).
void Puller::ProcessSemaphoreExecute(DmaPusher& dma_pusher, u32 argument) {
    const GPUVAddr address = (GPUVAddr{regs.reg_array[0x18] & 0x1FFFFFF} << 32) |
                             (regs.reg_array[0x17] & ~3U);
    const u32 payload = regs.reg_array[0x19];
    const u32 operation = argument & 7;
    if (operation == 1) { // RELEASE
        OnBack([rasterizer = dma_pusher.rasterizer, address, payload] {
            rasterizer->Query(address, VideoCommon::QueryType::Payload,
                              VideoCommon::QueryPropertiesFlags::IsAFence, payload, 0);
        });
        NoteQueuedRelease(dma_pusher.memory_manager.GpuToCpuAddress(address), payload);
        if (const auto device = dma_pusher.memory_manager.GpuToCpuAddress(address)) {
            VideoCommon::GpuSplit::Get().NoteMemoryWriterRange(*device, 16);
        } else {
            VideoCommon::GpuSplit::Get().NoteMemoryWriter();
        }
        return;
    }
    // Acquires: check once after flushing pending fences; never block the GPU thread on them.
    DrainBack();
    ReleaseFences(dma_pusher);
    const u32 word = dma_pusher.memory_manager.Read<u32>(address);
    const bool ready = operation == 0   ? word == payload
                       : operation == 2 ? word >= payload
                       : operation == 3 ? static_cast<s32>(word - payload) >= 0
                       : operation == 4 ? (word & payload) != 0
                       : operation == 5 ? (word & payload) == 0
                                        : true;
    if (!ready) {
        static std::atomic<u32> reported{};
        if (reported.fetch_add(1) < 8) {
            LOG_WARNING(HW_GPU, "SEM_EXECUTE op {} at {:#x}: {:#x} vs {:#x} not met, continuing",
                        operation, address, word, payload);
        }
    }
}

/// Calls a GPU puller method.
void Puller::CallPullerMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    static const bool nx2_trace = std::getenv("EDEN_NX2_PULLER_TRACE") != nullptr;
    if (nx2_trace) {
        LOG_INFO(HW_GPU, "nx2 puller sub={} method={:#x} arg={:#x}", method_call.subchannel,
                 method_call.method, method_call.argument);
    }
    regs.reg_array[method_call.method] = method_call.argument;
    const auto method = static_cast<BufferMethods>(method_call.method);

    switch (method) {
    case BufferMethods::BindObject: {
        ProcessBindMethod(dma_pusher, method_call);
        break;
    }
    case BufferMethods::Nop:
    case BufferMethods::SemaphoreAddressHigh:
    case BufferMethods::SemaphoreAddressLow:
    case BufferMethods::SemaphoreSequencePayload:
    case BufferMethods::SyncpointPayload:
    case BufferMethods::WrcacheFlush:
        break;
    case BufferMethods::RefCnt:
        OnBack([rasterizer = dma_pusher.rasterizer] { rasterizer->SignalReference(); });
        break;
    case BufferMethods::SyncpointOperation:
        ProcessFenceActionMethod(dma_pusher);
        break;
    case BufferMethods::WaitForIdle:
        OnBack([rasterizer = dma_pusher.rasterizer] { rasterizer->WaitForIdle(); });
        VideoCommon::GpuSplit::Get().NoteSync();
        break;
    case BufferMethods::SemaphoreOperation: {
        ProcessSemaphoreTriggerMethod(dma_pusher);
        break;
    }
    case BufferMethods::NonStallInterrupt: {
        LOG_ERROR(HW_GPU, "Special puller engine method NonStallInterrupt not implemented");
        break;
    }
    case BufferMethods::MemOpA: {
        LOG_TRACE(HW_GPU, "Memory Operation A");
        break;
    }
    case BufferMethods::MemOpB: {
        // Implement this better.
        OnBack([rasterizer = dma_pusher.rasterizer] { rasterizer->InvalidateGPUCache(); });
        break;
    }
    case BufferMethods::MemOpC:
    case BufferMethods::MemOpD: {
        LOG_TRACE(HW_GPU, "Memory Operation C,D");
        break;
    }
    case BufferMethods::SemaphoreAcquire: {
        // On Ampere host classes 0x17..0x1A are SEM_ADDR_LO/HI and SEM_PAYLOAD_LO/HI.
        if (!g_nx2_ampere) {
            ProcessSemaphoreAcquire(dma_pusher);
        }
        break;
    }
    case BufferMethods::SemaphoreRelease: {
        if (g_nx2_ampere) {
            ProcessSemaphoreExecute(dma_pusher, method_call.argument);
        } else {
            ProcessSemaphoreRelease(dma_pusher);
        }
        break;
    }
    case BufferMethods::Yield: {
        // TODO(Kmather73): Research and implement this method.
        LOG_ERROR(HW_GPU, "Special puller engine method Yield not implemented");
        break;
    }
    default:
        LOG_ERROR(HW_GPU, "Special puller engine method {:X} not implemented", method);
        if (static bool dumped = false; g_nx2_ampere && !dumped) {
            dumped = true;
            dma_pusher.DumpCurrentEntry("unknown host method");
        }
        break;
    }
}

/// Calls a GPU engine method.
static bool Nx2MethodTrace() {
    static const bool trace = std::getenv("EDEN_NX2_METHOD_TRACE") != nullptr;
    return trace;
}

void Puller::CallEngineMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    const EngineID engine = bound_engines[method_call.subchannel];
    if (Nx2MethodTrace()) {
        LOG_INFO(HW_GPU, "nx2 method sub={} engine={:04X} method={:#x} arg={:#x}",
                 method_call.subchannel, static_cast<u32>(engine), method_call.method,
                 method_call.argument);
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU engine multivalue method.
void Puller::CallEngineMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    const EngineID engine = bound_engines[subchannel];
    if (Nx2MethodTrace()) {
        LOG_INFO(HW_GPU, "nx2 multimethod sub={} engine={:04X} method={:#x} amount={} first={:#x}",
                 subchannel, static_cast<u32>(engine), method, amount, base_start[0]);
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU method.
void Puller::CallMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method_call.method, method_call.subchannel);
    ASSERT(method_call.subchannel < bound_engines.size());

    if (ExecuteMethodOnEngine(dma_pusher, method_call.method)) {
        CallEngineMethod(dma_pusher, method_call);
    } else {
        CallPullerMethod(dma_pusher, method_call);
    }
}

/// Calls a GPU multivalue method.
void Puller::CallMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method, subchannel);
    ASSERT(subchannel < bound_engines.size());
    if (ExecuteMethodOnEngine(dma_pusher, method)) {
        CallEngineMultiMethod(dma_pusher, method, subchannel, base_start, amount, methods_pending);
    } else {
        for (u32 i = 0; i < amount; i++) {
            CallPullerMethod(dma_pusher, MethodCall{
                method,
                base_start[i],
                subchannel,
                methods_pending - i,
            });
        }
    }
}

/// Determines where the method should be executed.
[[nodiscard]] bool Puller::ExecuteMethodOnEngine(DmaPusher& dma_pusher, u32 method) {
    const auto buffer_method = BufferMethods(method);
    return buffer_method >= BufferMethods::NonPullerMethods;
}

} // namespace Tegra::Engines
