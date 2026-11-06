// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include "common/common_types.h"

namespace VideoCommon::NativeIndirectReset {

// Bananza's pointer-list reset kernel, captured in the pre-upstream fork.
inline constexpr std::array<u32, 44> Program{
    0xff007321, 0x0002fcff, 0x00000000, 0x000e2400, 0x00007a11, 0x00004400, 0x078018ff,
    0x001fca00, 0xff017624, 0x00004500, 0x000e06ff, 0x000fcc00, 0x00007381, 0x00000000,
    0x001eab00, 0x000ea200, 0x00037802, 0x00000001, 0x00000f00, 0x000fca00, 0x00007386,
    0x00000403, 0x0010a900, 0x004fe800, 0x00007386, 0x000000ff, 0x0010a900, 0x000fe800,
    0x00007386, 0x000008ff, 0x0010a900, 0x000fe800, 0x00007386, 0x00000cff, 0x0010a900,
    0x000fe800, 0x00007386, 0x000010ff, 0x0010a900, 0x000fe200, 0x0000794d, 0x00000000,
    0x03800000, 0x000fea00,
};
inline constexpr std::array<u32, 5> Record{0, 1, 0, 0, 0};

inline bool Matches(std::span<const u32> code) {
    return std::ranges::equal(code, Program);
}

struct ListRange {
    u64 address;
    size_t bytes;
};

inline std::optional<ListRange> GetListRange(u64 list, u32 list_size, u32 first, u32 count) {
    const u64 entries = list_size / sizeof(u64);
    // Refuse oversized draws, rather than silently resetting only a prefix.
    if (!list || count == 0 || count > (1U << 20) || first > entries || count > entries - first) {
        return std::nullopt;
    }
    const u64 offset = u64{first} * sizeof(u64);
    const u64 bytes = u64{count} * sizeof(u64);
    if (list > (std::numeric_limits<u64>::max)() - offset - bytes) {
        return std::nullopt;
    }
    return ListRange{list + offset, static_cast<size_t>(bytes)};
}

} // namespace VideoCommon::NativeIndirectReset
