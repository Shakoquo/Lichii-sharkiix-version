// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string_view>

#include "common/thread_cpu_clock.h"

namespace Common::DkbCpuTrace {

enum class Role : size_t {
    GuestCore0,
    GuestCore1,
    GuestCore2,
    GuestCore3,
    GuestCore4,
    GuestCore5,
    GuestSingleCore,
    GpuCommand,
    GpuBack,
    VulkanWorker,
    Count,
};

inline constexpr size_t GuestCoreCount = static_cast<size_t>(Role::GuestSingleCore);
inline constexpr size_t RoleCount = static_cast<size_t>(Role::Count);
inline constexpr std::array<std::string_view, RoleCount> RoleNames{
    "guest_core_0_host", "guest_core_1_host", "guest_core_2_host", "guest_core_3_host",
    "guest_core_4_host", "guest_core_5_host", "guest_single_core_host", "gpu_command",
    "gpu_back", "vulkan_worker",
};

struct ThreadSample {
    u64 generation{};
    bool active{};
    std::optional<u64> cpu_ns;
};

struct Snapshot {
    std::chrono::steady_clock::time_point wall_time;
    std::array<ThreadSample, RoleCount> threads;
};

constexpr std::optional<u64> CpuTimeDelta(const ThreadSample& before, const ThreadSample& after) {
    if (!before.active || !after.active || before.generation != after.generation ||
        !before.cpu_ns || !after.cpu_ns || *after.cpu_ns < *before.cpu_ns) {
        return std::nullopt;
    }
    return *after.cpu_ns - *before.cpu_ns;
}

Snapshot ReadSnapshot();

class Registration {
public:
    explicit Registration(Role role);
    ~Registration();
    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;

private:
    ThreadCpuClock clock;
    Role role;
    bool registered{};
};

} // namespace Common::DkbCpuTrace
