// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "common/range_sets.h"
#include "common/range_sets.inc"

TEST_CASE("Moved cache range sets are empty, reusable and self-assignable", "[range_move]") {
    Common::RangeSet<u64> first;
    first.Add(100, 10);
    Common::RangeSet<u64> second{std::move(first)};
    REQUIRE(first.Empty());
    REQUIRE(second.Intersects(100, 10));
    first.Add(300, 5);
    CHECK_FALSE(first.Intersects(100, 10));
    auto& assigned = (first = std::move(second));
    CHECK(&assigned == &first);
    CHECK(second.Empty());
    CHECK(first.Intersects(100, 10));
    CHECK_FALSE(first.Intersects(300, 5));
    auto& self = first;
    first = std::move(self);
    CHECK(first.Intersects(100, 10));

    Common::OverlapRangeSet<u64> overlapping;
    overlapping.Add(10, 10);
    overlapping.Add(15, 10);
    Common::OverlapRangeSet<u64> moved{std::move(overlapping)};
    CHECK(overlapping.Empty());
    CHECK_FALSE(moved.Empty());
    auto& result = (overlapping = std::move(moved));
    CHECK(&result == &overlapping);
    CHECK(moved.Empty());
    int ranges = 0;
    overlapping.ForEach([&](u64, u64, s32 count) {
        CHECK(count >= 1);
        ++ranges;
    });
    CHECK(ranges == 3);
    auto& overlap_self = overlapping;
    overlapping = std::move(overlap_self);
    CHECK_FALSE(overlapping.Empty());
}
