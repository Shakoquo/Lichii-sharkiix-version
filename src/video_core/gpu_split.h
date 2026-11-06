// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include <type_traits>

#include "common/common_types.h"
#include "video_core/engines/engine_interface.h"
#include "video_core/split_write_tracker.h"

namespace Core {
class System;
}

namespace Tegra {
class MemoryManager;
}

namespace Tegra::Engines {
class EngineInterface;
}

namespace VideoCommon {

/// Why the split's front waited for its back (EDEN_NX2_GPU_PROFILE counts each).
enum class DrainSite : u32 {
    Other,
    FetchDrain,     ///< EDEN_NX2_FETCH_DRAIN: before every entry
    Acquire,        ///< A semaphore acquire reads the value the back may release
    SemaphoreFlush, ///< The acquired semaphore is in the buffer cache
    FenceRelease,   ///< The puller's acquires release the pending fences
    ListWriters,    ///< GPU-written command lists: the dispatches and copies sent before them
    ListSync,       ///< Command lists on draw-written pages: the work before the last host sync
    ListFlush,      ///< Command lists on GPU-written pages: drain and flush the pages
    MacroDmaRead,   ///< The front's macros read memory (MME DMA)
    MacroDmaWrites, ///< The same, waiting only for the queued work that may write it
    MacroDmaFlush,  ///< Those words are in the buffer cache
    GpSyncFence,    ///< A GP entry's sync bit (sync_memory_operations)
    Map,            ///< GPU memory maps before later command reads
    FrontStart,     ///< The front takes an engine's macros over
    FrontVerify,    ///< EDEN_NX2_FRONT_VERIFY
    MacroDmaCheck,  ///< EDEN_NX2_CMDPAGE_DIAG: a macro's memory read repeated after a full drain
    Count,
};

inline constexpr std::array<const char*, static_cast<size_t>(DrainSite::Count)> drain_site_names{
    "other",       "fetch drain",     "acquire",    "semaphore flush", "fence release",
    "list writers", "list sync",      "list flush", "macro dma read",  "macro dma writes",
    "macro dma flush",
    "gp sync fence", "map",           "front start", "front verify",  "macro dma check",
};

// Splits the GPU thread in two (NX2_GPU_THREAD_SPLIT.md). The GPU thread (the front) takes the
// CPU's commands, parses command lists and runs the puller; engine method calls and everything
// behind them (register state, macros, draws, the rasterizer and its caches) run in order on a
// second thread (the back), fed through a single-producer single-consumer ring. Work the front
// would do on back-owned state is deferred into the same ring; the front waits for the back to
// catch up (Drain) only where it needs a result from that state, e.g. a semaphore it acquires.
// On for Switch 2 titles with the 3D and compute engines' macros on the front, against copies of
// the registers they read (Maxwell3D and KeplerCompute Front*), which sends the writes they make.
// EDEN_NX2_SPLIT=1 keeps the macros on the back; EDEN_NX2_SPLIT=0 runs everything on one thread.
class GpuSplit {
public:
    static GpuSplit& Get() {
        return instance;
    }

    /// Whether the front sends work to the back (front thread only).
    [[nodiscard]] bool Active() const {
        return active;
    }
    /// Whether the front runs the engines' macros (EDEN_NX2_SPLIT=2).
    [[nodiscard]] bool FrontMacros() const {
        return front_macros;
    }

    /// EDEN_NX2_MACRO_SINK=1 (default off): the state writes the front's 3D macros make
    /// (registers outside execution_mask) go to the back's method sink like the command list's
    /// own state writes, instead of one CallMethod each; the back applies its sink, in order,
    /// before the engine's next executable method.
    [[nodiscard]] static bool MacroSink() {
        return macro_sink;
    }
    /// EDEN_NX2_COMPUTE_SINK=1 (default off): native compute writes that only store their value
    /// (native_plain_methods), from macros or the command list, are dropped when the front's
    /// register copy shows the back holds the value already, else sent batched (SinkMethod).
    [[nodiscard]] static bool ComputeSink() {
        return compute_sink;
    }
    /// EDEN_NX2_RING_PAIRS=1 (default off): the state writes that would go to the back's method
    /// sink (the command list's 3D state writes; the 3D macros' with EDEN_NX2_MACRO_SINK, the
    /// compute ones with EDEN_NX2_COMPUTE_SINK) go as (method, value) pairs in Pairs blocks
    /// (PairWrite), which the back applies in one loop per block (EngineInterface::ApplyPairs),
    /// after its method sink.
    [[nodiscard]] static bool RingPairs() {
        return ring_pairs;
    }

    /// The front's writes that took a batched path (EDEN_NX2_MACRO_SINK, EDEN_NX2_RING_PAIRS),
    /// logged every 10 s.
    enum class Batched : u32 {
        MacroSunk,   ///< 3D macro state writes sent to the method sink (or as pairs)
        ComputeSunk, ///< Compute state writes sent to the method sink (or as pairs)
        ComputeSame, ///< Compute state writes dropped: the register held the value
        Pairs,       ///< Writes sent as pairs (any engine, any source)
        PairBlocks,  ///< Pairs blocks opened
        Count,
    };
    void CountBatched(Batched counter, u64 amount = 1) {
        batched[static_cast<size_t>(counter)] += amount;
    }

    /// Whether the calling thread is the back.
    [[nodiscard]] static bool OnBackThread() {
        return on_back_thread;
    }
    /// Marks the calling thread as the GPU thread (the front, or the only one without the split).
    static void MarkGpuThread() {
        on_gpu_thread = true;
    }
    /// Whether the calling thread owns the Vulkan scheduler: the back while it runs, else the
    /// GPU thread. Other threads (CPU cores, services) must not record or submit work.
    [[nodiscard]] static bool OwnsScheduler() {
        return Get().BackRunning() ? on_back_thread : on_gpu_thread;
    }
    /// Called on the back before it sleeps waiting for work (the rasterizer submits deferred work).
    void SetIdleHook(void (*hook)(void*), void* argument) {
        idle_hook = hook;
        idle_argument = argument;
    }
    /// Any thread: whether the back thread runs (false before it starts and once it stopped).
    [[nodiscard]] bool BackRunning() const {
        return back_running.load(std::memory_order_acquire);
    }
    /// Any thread: has the back call the idle hook soon, between entries or at once if it sleeps.
    void RequestIdleHook() {
        hook_requested.store(true, std::memory_order_seq_cst);
        wake.fetch_add(1, std::memory_order_seq_cst);
        wake.notify_one();
    }

    /// Starts the back thread once the split is wanted (EDEN_NX2_SPLIT=1, a Switch 2 title,
    /// commands read without cache flushes). Called by the front between commands.
    void MaybeStart(Core::System& system);
    void Stop();

    /// An engine method write, executed as DmaPusher would (method sink, ConsumeSink, call).
    /// `segment`: the engine's current_dma_segment for the call, when the engine reads it.
    /// `effect`: what the entry may write (SplitWriteTracker; unknown unless the caller knows).
    using Effect = SplitWriteTracker::Effect;
    void Method(Tegra::Engines::EngineInterface* engine, u32 method, u32 argument, bool is_last,
                const GPUVAddr* segment, Effect effect = Effect::Unknown) {
        SelectEngine(engine);
        const size_t words = segment != nullptr ? 2 : 1;
        u64* const entry = Reserve(words);
        entry[0] = static_cast<u64>(Kind::Method) | (is_last ? IsLast : 0) |
                   (segment != nullptr ? HasSegment : 0) | (u64{method & 0xffff} << 8) |
                   (u64{argument} << 32);
        if (segment != nullptr) {
            entry[1] = *segment;
        }
        Commit(words, effect);
        if (engine->memory_write_mask[method]) {
            NoteMethodWrite(engine, method);
        }
    }
    void MultiMethod(Tegra::Engines::EngineInterface* engine, u32 method, const u32* words,
                     u32 amount, u32 methods_pending, const GPUVAddr* segment,
                     Effect effect = Effect::Unknown);
    /// A write made by a macro: CallMethod without the method sink, as the engines' macros do.
    void MacroWrite(Tegra::Engines::EngineInterface* engine, u32 method, u32 argument,
                    Effect effect = Effect::Unknown) {
        SelectEngine(engine);
        u64* const entry = Reserve(1);
        entry[0] = static_cast<u64>(Kind::Method) | IsLast | Direct | (u64{method & 0xffff} << 8) |
                   (u64{argument} << 32);
        Commit(1, effect);
        if (engine->memory_write_mask[method]) {
            NoteMethodWrite(engine, method);
        }
    }
    /// EDEN_NX2_MACRO_SINK: a write that only stores a register's value, to an engine whose
    /// execution_mask has the register (native compute): the back puts it in the engine's method
    /// sink tagged EngineInterface::SplitPlainTag, applied before the engine's next other write.
    void SinkMethod(Tegra::Engines::EngineInterface* engine, u32 method, u32 argument) {
        SelectEngine(engine);
        u64* const entry = Reserve(1);
        entry[0] = static_cast<u64>(Kind::Method) | IsLast | Sink | (u64{method & 0xffff} << 8) |
                   (u64{argument} << 32);
        Commit(1, Effect::None);
    }
    /// EDEN_NX2_RING_PAIRS: a write that only stores a register's value (no execution_mask side
    /// effect; native compute: native_plain_methods), appended to the open Pairs block. A block
    /// is open only until anything else is reserved or the ring is published (Reserve, Publish),
    /// so published entries never change and a block never crosses the ring's end.
    void PairWrite(Tegra::Engines::EngineInterface* engine, u32 method, u32 argument) {
        SelectEngine(engine);
        const u64 pair = u64{method & 0xffff} | (u64{argument} << 32);
        const size_t index = write_position & RingMask;
        if (pairs_header != nullptr && index != 0 && pairs_count < MaxPairs &&
            write_position + 1 - consumed_cache <= RingWords) [[likely]] {
            ring[index] = pair;
            ++pairs_count;
            *pairs_header = static_cast<u64>(Kind::Pairs) | (u64{pairs_count} << 32);
            CountBatched(Batched::Pairs);
            Commit(1, Effect::None);
            return;
        }
        u64* const entry = Reserve(2);
        entry[0] = static_cast<u64>(Kind::Pairs) | (u64{1} << 32);
        entry[1] = pair;
        pairs_header = entry;
        pairs_count = 1;
        CountBatched(Batched::Pairs);
        CountBatched(Batched::PairBlocks);
        Commit(2, Effect::None);
    }

    /// Runs `func` on the back after everything sent before it. Must be trivially copyable.
    template <typename Func>
    void Defer(const Func& func, Effect effect = Effect::Unknown) {
        static_assert(std::is_trivially_copyable_v<Func> && alignof(Func) <= alignof(u64));
        constexpr size_t payload = (sizeof(Func) + sizeof(u64) - 1) / sizeof(u64);
        u64* const entry = Reserve(2 + payload);
        entry[0] = static_cast<u64>(Kind::Call) | (u64{payload} << 8);
        entry[1] = reinterpret_cast<u64>(&CallThunk<Func>);
        std::memcpy(entry + 2, &func, sizeof(Func));
        Commit(2 + payload, effect);
    }
    void DeferFunction(std::function<void()>&& func);
    /// Runs `func` on the back and waits for it: for work on back-owned state, such as a buffer
    /// cache download that records into the Vulkan scheduler. A drained back still submits from
    /// its idle hook, so the front never touches that state itself.
    template <typename Func>
    void Run(const Func& func, DrainSite site = DrainSite::Other) {
        if (on_back_thread || !active) {
            func();
            return;
        }
        Defer(func);
        Drain(site);
    }

    /// EDEN_NX2_SPLIT_CHECK=1 (debug): reports, with its stack, a thread other than the back
    /// using back-owned state (the Vulkan scheduler) while the back runs entries.
    static inline bool check_owner{};
    static void CheckOwner(const char* where);

    /// Makes what was sent visible to the back.
    void Publish();
    /// Waits until the back executed everything sent so far.
    void Drain(DrainSite site = DrainSite::Other);

    /// Compute or copy work was just sent: what can write GPU-written command lists.
    void NoteWriter() {
        last_writer_position = write_position;
    }
    /// Waits until the back executed the compute and copy work sent so far (not what came after).
    /// Returns whether any of it was still queued.
    bool WaitForWriters();
    /// A host-level wait was just sent (a syncpoint acquire, a host WFI, a GP entry with its sync
    /// bit; semaphore acquires drain): the guest's GPU work before it is complete before the
    /// commands after it are fetched.
    void NoteSync() {
        last_sync_position = write_position;
    }
    /// Waits until the back executed the work sent before the last NoteSync. Returns whether any
    /// of it was still queued.
    bool WaitForSync();
    /// Deferred work that can write memory was just sent (puller semaphore releases, maps).
    void NoteMemoryWrite() {
        last_memory_write_position = write_position;
        last_memory_write_method = DeferredWrite;
    }
    /// A method that writes memory was just sent; the method is kept for the GPU profile.
    void NoteMethodWrite(const Tegra::Engines::EngineInterface* engine, u32 method) {
        previous_memory_write_position = last_memory_write_position;
        previous_memory_write_method = last_memory_write_method;
        last_memory_write_position = write_position;
        // Engines all of whose methods write (compute, DMA, inline-to-memory, 2D) mark method 0.
        last_memory_write_method = engine->memory_write_mask[0] ? OtherEngineWrite : method;
    }
    /// The method just sent writes only [begin, end): a macro memory read waits for it only where
    /// they overlap. Without this call its write counts for every read. EDEN_NX2_RANGED_WAITS=0
    /// keeps every write counting.
    /// `data` (optional): the words it writes there, in address space `space`, which a macro
    /// memory read of them takes without waiting (ForwardRead).
    void RangeLastWrite(GPUVAddr begin, GPUVAddr end, const u32* data = nullptr, size_t space = 0);
    /// For a macro memory read of `words` words at `begin` in address space `space`: when the
    /// newest work that may write them is one constant buffer upload still queued for the back,
    /// copies its words to `out` and returns true; the read then needs no wait.
    /// EDEN_NX2_FORWARD_READS=0 turns it off.
    bool ForwardRead(size_t space, GPUVAddr begin, u32 words, u32* out);
    /// For the front's macro memory reads (MME DMA) of [begin, end): waits until the back executed
    /// the methods that write memory in command order (memory_write_mask, NoteMemoryWrite; ranged
    /// writes only where they overlap), the compute and copy work and the work before the last
    /// host-level wait, all sent so far.
    bool WaitForMemoryWriters(DrainSite site, GPUVAddr begin, GPUVAddr end);
    /// Whether WaitForMemoryWriters(begin, end) would wait now: work that may write the range is
    /// still queued for the back.
    bool HasQueuedMemoryWriters(GPUVAddr begin, GPUVAddr end);
    /// A semaphore release that writes `payload` at device address `device` (from GPU address
    /// `address`) was just sent (a puller release, a 3D payload report).
    void NoteRelease(u64 device, GPUVAddr address, u32 payload);
    /// For a semaphore acquire of `device`: the payload a release still queued for the back writes
    /// there, if nothing sent after it may write the word. The back runs the acquire's channel
    /// after that release, so an acquire it meets needs no drain. EDEN_NX2_ACQUIRE_QUEUED=0 off.
    /// `miss` (when no payload): 1 no release recorded, 2 it already ran, 3 a later write.
    std::optional<u32> QueuedReleasePayload(u64 device, GPUVAddr address, u32* miss = nullptr);
    /// The entry just sent also writes [address, address + size) of `memory`, noted by device
    /// page (other mappings may alias it, and the caches keep state per page).
    void NoteWrite(const Tegra::MemoryManager& memory, GPUVAddr address, u64 size);
    /// The entry just sent writes guest memory the front can't tell.
    void NoteUnknownWrite() {
        writes.Entry(Effect::Unknown, write_position);
    }
    /// Waits for the queued functions (GPU memory maps among them) before the front translates
    /// addresses, then drops the pending writes the back has run. Returns the back's position.
    u64 SettleWrites();
    /// Ring position the back must pass to execute possible writers of [begin, end).
    /// Deferred fence writes can still be pending afterwards.
    [[nodiscard]] u64 ReadTarget(u64 begin, u64 end) const {
        return writes.ReadTarget(begin, end);
    }
    [[nodiscard]] const SplitWriteTracker& Writes() const {
        return writes;
    }
    /// Waits until the back consumed `position` (not what came after). Returns whether it had
    /// not yet.
    bool WaitUntil(u64 position, DrainSite site) {
        return WaitFor((std::min)(position, write_position), site);
    }

private:
    friend struct GpuSplitTestAccess;

    enum class Kind : u64 {
        Wrap,   ///< The rest of the ring is unused: continue at its start
        Engine, ///< The engine the next methods go to: word 1
        Method, ///< Bits 8-23 method, 32-63 argument; flags IsLast, HasSegment (word 1)
        Multi,  ///< Bits 8-23 method, 32-63 amount; word 1 methods pending, [segment], words
        Call,   ///< Bits 8-31 payload size in words; word 1 thunk, then the payload
        Pairs,  ///< Bits 32-63 count; then one word per write: bits 0-15 method, 32-63 value
    };
    static constexpr u64 IsLast = 1 << 4;
    static constexpr u64 HasSegment = 1 << 5;
    static constexpr u64 Direct = 1 << 6; ///< Method: call it even if not executable (macros)
    static constexpr u64 Sink = 1 << 7;   ///< Method: into the method sink, tagged (SinkMethod)

    // Read when the split first starts (MaybeStart), after main() applied nx2_switches.txt: a
    // static initialiser would run before it.
    static inline bool macro_sink{};
    static inline bool compute_sink{};
    static inline bool ring_pairs{};
    static constexpr u32 MaxPairs = 4096;

    static constexpr size_t RingWords = size_t{1} << 20; // 8 MiB
    static constexpr size_t RingMask = RingWords - 1;
    static constexpr u32 MaxMultiWords = 8192;
    static constexpr size_t PublishEvery = 256;

    template <typename Func>
    static void CallThunk(const u64* payload) {
        (*reinterpret_cast<const Func*>(payload))();
    }

    /// Room for an entry of `words` at the write position (never across the ring's end).
    u64* Reserve(size_t words) {
        pairs_header = nullptr; // What follows ends the open Pairs block
        const size_t index = write_position & RingMask;
        if (index + words <= RingWords && write_position + words - consumed_cache <= RingWords)
            [[likely]] {
            return &ring[index];
        }
        return ReserveSlow(words);
    }
    u64* ReserveSlow(size_t words);
    void Commit(size_t words, Effect effect) {
        write_position += words;
        writes.Entry(effect, write_position);
        if (write_position - published_local >= publish_every) {
            Publish();
        }
    }
    void WaitForSpace(size_t words);
    void SelectEngine(Tegra::Engines::EngineInterface* engine) {
        if (engine != front_engine) [[unlikely]] {
            SwitchEngine(engine);
        }
    }
    void SwitchEngine(Tegra::Engines::EngineInterface* engine);
    bool WaitFor(u64 target, DrainSite site);

    /// One step of a front wait for the back to consume `target`.
    void Backoff(u32 spins, u64 target);
    /// After the back stored `consumed`: wakes a front sleeping in Backoff.
    void NotifyFront() {
        if (front_waiting.load(std::memory_order_seq_cst)) {
            front_wake.fetch_add(1, std::memory_order_seq_cst);
            front_wake.notify_one();
        }
    }

    void BackLoop(std::stop_token stop_token);
    /// Runs the entry at `entry` (of the `words` published before the ring's end), returns its size.
    size_t Execute(const u64* entry, size_t words);

    Core::System* system{};
    bool active{};
    bool front_macros{};
    std::unique_ptr<u64[]> ring;

    // Front side.
    u64 write_position{};
    u64 last_writer_position{};
    u64 last_sync_position{};
    u64 last_memory_write_position{};
    static constexpr u32 DeferredWrite = ~0U;
    static constexpr u32 OtherEngineWrite = ~0U - 1;
    /// The 3D method behind last_memory_write_position, or one of the two values above.
    u32 last_memory_write_method{DeferredWrite};
    u64 previous_memory_write_position{};
    u32 previous_memory_write_method{DeferredWrite};
    /// Writes with a known range (RangeLastWrite) the back may not have run yet, oldest first.
    struct RangedWrite {
        u64 position;
        GPUVAddr begin;
        GPUVAddr end;
        u32 method;
        size_t space;           ///< Address space of `data`; 0 when the words are not known
        std::vector<u32> data;  ///< The words written over [begin, end), when known
    };
    static constexpr size_t MaxRangedWrites = 256;
    std::deque<RangedWrite> ranged_writes;
    struct QueuedRelease {
        u64 position;
        u64 device;
        u32 payload;
    };
    std::array<QueuedRelease, 64> queued_releases{};
    /// Drops consumed ranged writes; returns the position a memory read of [begin, end) waits
    /// for and the method behind it.
    u64 MemoryWriterTarget(u64 done, GPUVAddr begin, GPUVAddr end, u32& target_method);
    u64 last_function_position{};
    SplitWriteTracker writes;
    /// NoteWrite's last translation: memory manager, GPU page, device page, the function
    /// position it was made at (a later function may remap).
    const Tegra::MemoryManager* note_memory{};
    GPUVAddr note_page{~GPUVAddr{0}};
    u64 note_device{};
    u64 note_function{};
    u64 published_local{};
    u64 consumed_cache{};
    /// Words between publishes: small right after a drain, while the back has nothing to do.
    size_t publish_every{PublishEvery};
    Tegra::Engines::EngineInterface* front_engine{};
    /// The open Pairs block's head (PairWrite), and its count.
    u64* pairs_header{};
    u32 pairs_count{};
    std::array<u64, static_cast<size_t>(Batched::Count)> batched{};
    std::chrono::steady_clock::time_point batched_since{};
    u32 publishes{};
    void ReportBatched();

    // Back side.
    Tegra::Engines::EngineInterface* back_engine{};

    /// Whether the back is done with the Vulkan scheduler until it gets more work: set after its
    /// idle hook ran, cleared before it executes entries.
    [[nodiscard]] bool BackQuiet() const {
        return back_quiet.load(std::memory_order_acquire) ||
               !back_running.load(std::memory_order_acquire);
    }

    static GpuSplit instance;
    static inline thread_local bool on_back_thread{};
    static inline thread_local bool on_gpu_thread{};
    void (*idle_hook)(void*){};
    void* idle_argument{};
    alignas(64) std::atomic<bool> hook_requested{};
    std::atomic<bool> back_running{};
    std::atomic<bool> back_quiet{true};

    // 128 bytes: x86 spatial prefetchers pull 64-byte lines in pairs. back_waiting and wake are
    // always touched together by both threads, so they share a block.
    alignas(128) std::atomic<u64> published{};
    alignas(128) std::atomic<u64> consumed{};
    /// A position the front waits for (WaitFor): the back stores `consumed` once past it.
    alignas(128) std::atomic<u64> wait_target{~u64{0}};
    alignas(128) std::atomic<bool> back_waiting{};
    std::atomic<u32> wake{};
    /// The front sleeps in Backoff (#161: instead of yielding).
    alignas(128) std::atomic<bool> front_waiting{};
    std::atomic<u32> front_wake{};
    std::jthread back_thread;
};

} // namespace VideoCommon
