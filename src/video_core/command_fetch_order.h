// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace VideoCommon {

constexpr bool CanUseSubmittedCommands(bool resumed, bool has_snapshot, bool gpu_written,
                                       bool safe_read_dirty) {
    return !resumed && has_snapshot && !gpu_written && !safe_read_dirty;
}

template <typename Drain, typename WasWritten, typename Flush>
bool SynchronizeCommandFetch(Drain&& drain, WasWritten&& was_written, Flush&& flush) {
    // A queued writer can mark the page for the first time. Check only after it has run.
    drain();
    if (!was_written()) {
        return false;
    }
    flush();
    return true;
}

} // namespace VideoCommon
