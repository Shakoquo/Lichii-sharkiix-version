// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "common/common_types.h"
#include "common/logging.h"

namespace Kernel::Nx2WaitProfile {

// EDEN_NX2_WAIT_PROFILE=1: where the guest's threads spend time inside SVCs (blocked on events,
// address arbiters, mutexes, IPC to services), by thread, SVC and guest call site, logged every
// 10 s. A thread that the CPU samples show mostly idle is waiting somewhere: this says where.
inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_WAIT_PROFILE");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

struct Key {
    u64 thread_id;
    u32 svc;
    u64 lr;
    std::string what; // event name or handle, for waits and IPC
    bool operator<(const Key& other) const {
        return std::tie(thread_id, svc, lr, what) <
               std::tie(other.thread_id, other.svc, other.lr, other.what);
    }
};

struct Totals {
    u64 ns{};
    u64 count{};
    u64 max_ns{};
};

struct State {
    std::mutex mutex;
    std::map<Key, Totals> entries;
    std::map<u64, u64> thread_stack; // thread id -> stack pointer / 64 KiB (matches the sampler)
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
};

inline State& Get() {
    static State state;
    return state;
}

inline void Report(State& state, std::chrono::steady_clock::time_point now) {
    const double window_ns =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - state.since)
                                .count());
    std::map<u64, u64> per_thread;
    for (const auto& [key, totals] : state.entries) {
        per_thread[key.thread_id] += totals.ns;
    }
    std::vector<std::pair<u64, u64>> threads(per_thread.begin(), per_thread.end());
    std::sort(threads.begin(), threads.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    LOG_INFO(Kernel_SVC, "nx2 wait profile over {:.1f} s ({} threads; least waiting first):",
             window_ns / 1e9, threads.size());
    size_t shown = 0;
    for (const auto& [thread_id, ns] : threads) {
        if (shown++ == 24) {
            break;
        }
        LOG_INFO(Kernel_SVC, "  t{} (stack {:x}): {:.0f}% of the time in SVCs", thread_id,
                 state.thread_stack[thread_id], 100.0 * static_cast<double>(ns) / window_ns);
        std::vector<std::pair<Key, Totals>> rows;
        for (const auto& [key, totals] : state.entries) {
            if (key.thread_id == thread_id) {
                rows.emplace_back(key, totals);
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
        for (size_t i = 0; i < std::min<size_t>(rows.size(), 6); ++i) {
            const auto& [key, totals] = rows[i];
            if (totals.ns * 200 < static_cast<u64>(window_ns)) {
                break; // under 0.5%
            }
            LOG_INFO(Kernel_SVC,
                     "     svc {:#04x} from {:#x} {}: {:.1f}% ({} calls, avg {:.2f} ms, max {:.1f} ms)",
                     key.svc, key.lr, key.what,
                     100.0 * static_cast<double>(totals.ns) / window_ns, totals.count,
                     static_cast<double>(totals.ns) / 1e6 / static_cast<double>(totals.count),
                     static_cast<double>(totals.max_ns) / 1e6);
        }
    }
    state.entries.clear();
    state.since = now;
}

inline void Record(u64 thread_id, u32 svc, u64 lr, u64 sp, std::string what,
                   std::chrono::steady_clock::time_point begin) {
    const auto now = std::chrono::steady_clock::now();
    const u64 ns = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - begin).count());
    State& state = Get();
    std::scoped_lock lock{state.mutex};
    Totals& totals = state.entries[Key{thread_id, svc, lr, std::move(what)}];
    totals.ns += ns;
    ++totals.count;
    totals.max_ns = std::max(totals.max_ns, ns);
    state.thread_stack[thread_id] = sp >> 16;
    if (now - state.since >= std::chrono::seconds{10}) {
        Report(state, now);
    }
}

} // namespace Kernel::Nx2WaitProfile
