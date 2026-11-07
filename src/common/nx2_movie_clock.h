// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>

#include "common/common_types.h"

// EDEN_NX2_MOVIE_CLOCK=1: per-second counts of video, audio and clock progress.
namespace Common::Nx2MovieClock {

inline bool Enabled() {
    static const bool enabled = std::getenv("EDEN_NX2_MOVIE_CLOCK") != nullptr;
    return enabled;
}

struct Counters {
    std::atomic<u64> video_takes{};
    std::atomic<u64> video_releases{};
    std::atomic<u64> take_to_release_us{}; // sum over releases
    std::atomic<u64> video_last_pts_us{};
    std::atomic<u64> renderer_updates{};
    std::atomic<u64> renderer_passes{};
    std::atomic<u64> voices_dropped{};
    std::atomic<u64> voice_samples{}; // decoded by all voices, at their own rates
    std::atomic<u64> sink_frames[2]{};  // per render stream, sent to the host device
    std::atomic<u64> sink_padded[2]{};  // of those, repeated or silent for lack of data
};

inline Counters& Get() {
    static Counters counters;
    return counters;
}

inline void Add(std::atomic<u64>& counter, u64 value) {
    counter.fetch_add(value, std::memory_order_relaxed);
}

} // namespace Common::Nx2MovieClock
