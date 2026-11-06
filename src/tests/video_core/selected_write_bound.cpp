// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/buffer_cache_base.h"

TEST_CASE("Run-time selected writes end before the next higher base",
          "[video_core][selected_write_bound]") {
    using VideoCommon::SelectedWriteExtent;
    constexpr u64 size = 0x800000;
    constexpr u64 alignment = 0x40;
    const std::array<GPUVAddr, 4> bases{0x1000000, 0x1004000, 0x1002000, 0xff0000};
    REQUIRE(SelectedWriteExtent(0x1000000, size, bases, alignment) == 0x2000 + alignment);
    REQUIRE(SelectedWriteExtent(0x1002000, size, bases, alignment) == 0x2000 + alignment);
    REQUIRE(SelectedWriteExtent(0x1004000, size, bases, alignment) == size);
    REQUIRE(SelectedWriteExtent(0xff0000, size, bases, alignment) == 0x10000 + alignment);
    REQUIRE(SelectedWriteExtent(0x1000000, 0x1000, bases, alignment) == 0x1000);
    REQUIRE(SelectedWriteExtent(0x1000000, size, {}, alignment) == size);
}
