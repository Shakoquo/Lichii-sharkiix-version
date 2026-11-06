// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "common/common_types.h"

namespace VideoCommon {

template <typename Segment, typename Id>
void InvalidateMultiRangeSegments(std::span<Segment> segments, Id deleted) {
    for (auto& segment : segments) {
        if (segment.buffer_id == deleted) {
            segment.buffer_id = {};
        }
    }
}

template <typename Segment, typename IsLive>
bool MultiRangeSegmentsLive(std::span<const Segment> segments, IsLive is_live) {
    for (const auto& segment : segments) {
        if (!segment.buffer_id || !is_live(segment.buffer_id)) {
            return false;
        }
    }
    return true;
}

template <typename Segment, typename IsLive, typename Resolve>
bool RefreshMultiRangeSegments(std::span<Segment> segments, IsLive is_live, Resolve resolve) {
    // Finding a later segment can merge away an earlier one. Retry only a bounded number of times.
    for (u32 pass = 0; pass < 16; ++pass) {
        for (auto& segment : segments) {
            if (!segment.buffer_id || !is_live(segment.buffer_id)) {
                segment.buffer_id = resolve(segment.device_addr, segment.size);
            }
        }
        if (MultiRangeSegmentsLive(std::span<const Segment>{segments}, is_live)) {
            return true;
        }
    }
    return false;
}

template <typename Runtime>
bool BindFreshMultiRangeStorage(Runtime& runtime, u64 key, bool is_written) {
    // Submission ticks are not content versions: several writes can share a tick, and graphics
    // writes do not update it. Sparse aliases need no copy; gathered fallbacks must be refreshed.
    runtime.InvalidateMultiRange(key);
    return runtime.BindMultiRangeStorageBuffer(key, is_written);
}

} // namespace VideoCommon
