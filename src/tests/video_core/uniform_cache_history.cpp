// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/buffer_cache/uniform_cache_history.h"

TEST_CASE("Uniform cache history shifts overlapping samples backward",
          "[video_core][uniform_history]") {
    std::array<unsigned, 5> history{1, 2, 3, 4, 5};
    VideoCommon::AdvanceUniformCacheHistory(history);
    CHECK(history == std::array<unsigned, 5>{0, 1, 2, 3, 4});
    history.front() = 9;
    VideoCommon::AdvanceUniformCacheHistory(history);
    CHECK(history == std::array<unsigned, 5>{0, 9, 1, 2, 3});
    for (int i = 0; i < 5; ++i) VideoCommon::AdvanceUniformCacheHistory(history);
    CHECK(history == std::array<unsigned, 5>{});
    std::array<unsigned, 1> single{7};
    VideoCommon::AdvanceUniformCacheHistory(single);
    CHECK(single.front() == 0);
}
