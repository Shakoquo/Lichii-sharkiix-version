// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include "common/common_types.h"

namespace Common {

// A translated pointer is valid only within its physical backing page, even when
// the guest GPU mapping uses a larger page. Never memcpy a scalar across it.
template <typename T, typename Translate>
[[nodiscard]] T ReadPagedScalar(u64 address, Translate&& translate) {
    static_assert(std::is_trivially_copyable_v<T>);
    constexpr u64 page_size = 0x1000;
    T value{};
    auto* bytes = reinterpret_cast<u8*>(&value);
    size_t remaining = sizeof(T);
    while (remaining != 0) {
        const size_t count = (std::min)(remaining, size_t{page_size - (address & (page_size - 1))});
        if (const auto* pointer = translate(address)) {
            std::memcpy(bytes, pointer, count);
        }
        remaining -= count;
        bytes += count;
        if (count > (std::numeric_limits<u64>::max)() - address) {
            break; // Do not wrap an invalid trailing range back to address zero.
        }
        address += count;
    }
    return value;
}

template <typename T, typename Translate>
void WritePagedScalar(u64 address, T value, Translate&& translate) {
    static_assert(std::is_trivially_copyable_v<T>);
    constexpr u64 page_size = 0x1000;
    const auto* bytes = reinterpret_cast<const u8*>(&value);
    size_t remaining = sizeof(T);
    while (remaining != 0) {
        const size_t count = (std::min)(remaining, size_t{page_size - (address & (page_size - 1))});
        if (auto* pointer = translate(address)) {
            std::memcpy(pointer, bytes, count);
        }
        remaining -= count;
        bytes += count;
        if (count > (std::numeric_limits<u64>::max)() - address) {
            break;
        }
        address += count;
    }
}

} // namespace Common
