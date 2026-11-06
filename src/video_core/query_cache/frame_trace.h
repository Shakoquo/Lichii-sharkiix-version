// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

#include "common/common_types.h"
#include "common/dkb_frame_trace.h"

namespace VideoCommon::QueryTrace {

enum class Event : std::size_t {
    Request,
    Write,
    Discard,
    HostResult,
    CpuCondition,
    VulkanCondition,
    Count,
};

inline u64 Sample(Event event) {
    if (!Common::DkbFrameTrace::Enabled()) {
        return 0;
    }
    static std::array<std::atomic<u64>, static_cast<std::size_t>(Event::Count)> counts{};
    static std::array<Common::DkbFrameTrace::RateLimit, static_cast<std::size_t>(Event::Count)> rates{};
    static std::mutex mutex;
    const u64 count =
        counts[static_cast<std::size_t>(event)].fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 64) {
        return count;
    }
    if (count % 1024 != 0) {
        return 0;
    }
    const std::scoped_lock lock{mutex};
    return rates[static_cast<std::size_t>(event)].Ready(std::chrono::steady_clock::now()) ? count : 0;
}

} // namespace VideoCommon::QueryTrace
