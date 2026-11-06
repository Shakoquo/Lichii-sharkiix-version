// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/thread_cpu_clock.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <time.h>
#endif

namespace Common {
namespace {

std::optional<u64> ReadNativeClock(std::intptr_t native_clock) {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(reinterpret_cast<HANDLE>(native_clock), &creation, &exit, &kernel, &user)) {
        return std::nullopt;
    }
    const u64 kernel_ticks = (u64{kernel.dwHighDateTime} << 32) | kernel.dwLowDateTime;
    const u64 user_ticks = (u64{user.dwHighDateTime} << 32) | user.dwLowDateTime;
    return (kernel_ticks + user_ticks) * 100;
#elif defined(__linux__)
    timespec time{};
    if (clock_gettime(static_cast<clockid_t>(native_clock), &time) != 0) {
        return std::nullopt;
    }
    return static_cast<u64>(time.tv_sec) * 1'000'000'000 + static_cast<u64>(time.tv_nsec);
#else
    static_cast<void>(native_clock);
    return std::nullopt;
#endif
}

} // namespace

std::optional<u64> CurrentThreadCpuTimeNs() {
#ifdef _WIN32
    return ReadNativeClock(reinterpret_cast<std::intptr_t>(GetCurrentThread()));
#elif defined(__linux__)
    return ReadNativeClock(CLOCK_THREAD_CPUTIME_ID);
#else
    return std::nullopt;
#endif
}

ThreadCpuClock::ThreadCpuClock(bool enabled) {
    if (!enabled) {
        return;
    }
#ifdef _WIN32
    HANDLE handle{};
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                        THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        native_clock = reinterpret_cast<std::intptr_t>(handle);
        available = true;
    }
#elif defined(__linux__)
    clockid_t clock{};
    if (pthread_getcpuclockid(pthread_self(), &clock) == 0) {
        native_clock = clock;
        available = true;
    }
#endif
}

ThreadCpuClock::~ThreadCpuClock() {
#ifdef _WIN32
    if (available) {
        CloseHandle(reinterpret_cast<HANDLE>(native_clock));
    }
#endif
}

std::optional<u64> ThreadCpuClock::Read() const {
    return available ? ReadNativeClock(native_clock) : std::nullopt;
}

} // namespace Common
