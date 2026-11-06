// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include "common/common_types.h"

namespace Kernel {

inline constexpr u64 MarioTennisFeverProgramId = 0x0400D4201C6EA000ULL;
inline constexpr auto StartupGraceLimit = std::chrono::milliseconds{20};
inline constexpr s64 StartupSleepNanoseconds = 50'000;

constexpr bool NeedsStartupGrace(u64 program_id, s32 caller_core, s32 caller_priority,
                                 s32 worker_core, s32 worker_priority) {
    return program_id == MarioTennisFeverProgramId && caller_core >= 0 &&
           caller_core == worker_core && worker_priority > caller_priority;
}

constexpr bool ShouldWaitForStartup(bool runnable, bool waiting_on_mutex, bool terminating,
                                    std::chrono::steady_clock::duration elapsed) {
    return !terminating && elapsed < StartupGraceLimit && (runnable || waiting_on_mutex);
}

} // namespace Kernel
