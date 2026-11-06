// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace VideoCommon {

template <typename T, size_t N>
void AdvanceUniformCacheHistory(std::array<T, N>& history) {
    static_assert(N > 0);
    // The destination overlaps to the right: forward copy is not valid here.
    std::copy_backward(history.begin(), history.end() - 1, history.end());
    history.front() = 0;
}

} // namespace VideoCommon
