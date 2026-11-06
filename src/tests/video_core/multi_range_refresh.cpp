// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/slot_vector.h"
#include "video_core/buffer_cache/multi_range_refresh.h"

namespace {

struct Segment {
    u32 buffer_id;
    u64 device_addr;
    u32 size;
};

// Host-side model of the sparse alias / gathered fallback protocol. No Vulkan device is created.
struct Runtime {
    std::array<u32, 4> source{1, 2, 3, 4};
    std::array<u32, 4> gathered{};
    bool dirty{true};
    bool sparse{};
    u32 copies{};
    u64 invalidated_key{};

    void InvalidateMultiRange(u64 key) {
        dirty = true;
        invalidated_key = key;
    }

    bool BindMultiRangeStorageBuffer(u64 key, bool written) {
        CHECK(key == 123);
        if (written && !sparse) {
            return false;
        }
        if (!sparse && dirty) {
            gathered = source;
            dirty = false;
            ++copies;
        }
        return true;
    }

    const auto& Read() const {
        return sparse ? source : gathered;
    }
};

} // namespace

TEST_CASE("Gathered buffers refresh even when source handles and submission ticks stay unchanged",
          "[video_core][multi_range_refresh]") {
    Runtime runtime;
    REQUIRE(runtime.BindMultiRangeStorageBuffer(123, false));
    REQUIRE(runtime.Read() == runtime.source);
    runtime.source = {5, 6, 7, 8};
    // The old cached bind misses a GPU write that did not change its content hash.
    REQUIRE(runtime.BindMultiRangeStorageBuffer(123, false));
    REQUIRE(runtime.Read() != runtime.source);
    for (u32 write = 0; write < 100; ++write) {
        runtime.source[write % 4] = 1000 + write;
        REQUIRE(VideoCommon::BindFreshMultiRangeStorage(runtime, 123, false));
        REQUIRE(runtime.Read() == runtime.source);
    }
    REQUIRE(runtime.copies == 101);
    REQUIRE(runtime.invalidated_key == 123);
}

TEST_CASE("Refreshing sparse aliases adds no copies and preserves bind failure",
          "[video_core][multi_range_refresh]") {
    Runtime runtime;
    REQUIRE_FALSE(VideoCommon::BindFreshMultiRangeStorage(runtime, 123, true));
    REQUIRE(runtime.copies == 0);
    runtime.sparse = true;
    for (u32 write = 0; write < 100; ++write) {
        runtime.source[write % 4] = write;
        REQUIRE(VideoCommon::BindFreshMultiRangeStorage(runtime, 123, true));
        REQUIRE(runtime.Read() == runtime.source);
    }
    REQUIRE(runtime.copies == 0);
}

TEST_CASE("Deleted segment IDs are forgotten before a slot can be reused",
          "[video_core][multi_range_refresh]") {
    std::vector<Segment> graphics{{1, 0x1000, 16}, {2, 0x2000, 16}};
    std::vector<Segment> compute{{2, 0x3000, 16}};
    VideoCommon::InvalidateMultiRangeSegments(std::span{graphics}, 2U);
    VideoCommon::InvalidateMultiRangeSegments(std::span{compute}, 2U);
    REQUIRE(graphics[0].buffer_id == 1);
    REQUIRE(graphics[1].buffer_id == 0);
    REQUIRE(compute[0].buffer_id == 0);
    // Even if slot 2 is now occupied by an unrelated buffer, the old references stay invalid.
    const auto live = [](u32 id) { return id == 1 || id == 2; };
    REQUIRE_FALSE(VideoCommon::MultiRangeSegmentsLive(std::span<const Segment>{graphics}, live));
    REQUIRE_FALSE(VideoCommon::MultiRangeSegmentsLive(std::span<const Segment>{compute}, live));
    std::vector<u64> resolved;
    REQUIRE(VideoCommon::RefreshMultiRangeSegments(
        std::span{graphics}, live, [&](u64 address, u32 size) {
            resolved.push_back(address);
            CHECK(size == 16);
            return 2U;
        }));
    REQUIRE(resolved == std::vector<u64>{0x2000});
}

TEST_CASE("A merge while resolving a later segment re-resolves earlier segments",
          "[video_core][multi_range_refresh]") {
    std::vector<Segment> segments{{1, 0x1000, 16}, {0, 0x2000, 16}};
    std::array<bool, 4> live{false, true, false, true};
    u32 resolves{};
    REQUIRE(VideoCommon::RefreshMultiRangeSegments(
        std::span{segments}, [&](u32 id) { return live.at(id); },
        [&](u64 address, u32) {
            ++resolves;
            if (address == 0x2000) {
                live[1] = false;
                VideoCommon::InvalidateMultiRangeSegments(std::span{segments}, 1U);
            }
            return 3U;
        }));
    REQUIRE(resolves == 2);
    REQUIRE(segments[0].buffer_id == 3);
    REQUIRE(segments[1].buffer_id == 3);
}

TEST_CASE("Unresolvable multi-range segments have bounded retries and fail validation",
          "[video_core][multi_range_refresh]") {
    std::vector<Segment> segments{{0, 0x1000, 16}};
    u32 resolves{};
    const auto never_live = [](u32) { return false; };
    REQUIRE_FALSE(VideoCommon::RefreshMultiRangeSegments(
        std::span{segments}, never_live, [&](u64, u32) {
            ++resolves;
            return 0U;
        }));
    REQUIRE(resolves == 16);
    REQUIRE_FALSE(VideoCommon::MultiRangeSegmentsLive(std::span<const Segment>{segments}, never_live));
    segments.clear();
    REQUIRE(VideoCommon::RefreshMultiRangeSegments(
        std::span{segments}, never_live, [](u64, u32) { return 0U; }));
}

TEST_CASE("Real buffer slots validate zero, deletion, reuse and out-of-bounds IDs",
          "[video_core][multi_range_refresh]") {
    Common::SlotVector<u32> slots;
    REQUIRE_FALSE(slots.contains(Common::SlotId{}));
    REQUIRE_FALSE(slots.contains(Common::SlotId{10000}));
    const auto id = slots.insert(42U);
    REQUIRE(id.Value() == 0);
    REQUIRE(slots.contains(id));
    struct RealSegment {
        Common::SlotId buffer_id;
        u64 device_addr;
        u32 size;
    };
    std::vector<RealSegment> segments{{id, 0x1000, 16}};
    const auto live = [&](Common::SlotId slot) { return slots.contains(slot); };
    REQUIRE(VideoCommon::MultiRangeSegmentsLive(std::span<const RealSegment>{segments}, live));
    VideoCommon::InvalidateMultiRangeSegments(std::span{segments}, id);
    slots.erase(id);
    REQUIRE_FALSE(slots.contains(id));
    const auto reused = slots.insert(99U);
    REQUIRE(reused == id);
    REQUIRE(slots.contains(reused));
    REQUIRE_FALSE(VideoCommon::MultiRangeSegmentsLive(std::span<const RealSegment>{segments}, live));
}
