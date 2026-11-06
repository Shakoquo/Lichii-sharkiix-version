// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/thread_startup_policy.h"

using namespace std::chrono_literals;
using namespace Kernel;

TEST_CASE("Startup grace is restricted to MTF same-core lower-priority workers", "[thread_startup]") {
    CHECK(NeedsStartupGrace(MarioTennisFeverProgramId, 0, 44, 0, 47));
    CHECK_FALSE(NeedsStartupGrace(0x0400C3F00006E000ULL, 0, 44, 0, 47));
    CHECK_FALSE(NeedsStartupGrace(0, 0, 44, 0, 47));
    CHECK_FALSE(NeedsStartupGrace(MarioTennisFeverProgramId, 0, 44, 1, 47));
    CHECK_FALSE(NeedsStartupGrace(MarioTennisFeverProgramId, 0, 44, 0, 44));
    CHECK_FALSE(NeedsStartupGrace(MarioTennisFeverProgramId, 0, 44, 0, 43));
    CHECK_FALSE(NeedsStartupGrace(MarioTennisFeverProgramId, -1, 44, -1, 47));
}

TEST_CASE("Startup grace waits through the SDK mutex but stops at other waits", "[thread_startup]") {
    CHECK(ShouldWaitForStartup(true, false, false, 0ns));
    CHECK(ShouldWaitForStartup(false, true, false, 0ns));
    CHECK_FALSE(ShouldWaitForStartup(false, false, false, 0ns));
    CHECK_FALSE(ShouldWaitForStartup(true, false, true, 0ns));
    CHECK_FALSE(ShouldWaitForStartup(false, true, true, 0ns));
}

TEST_CASE("Startup grace has one bounded budget across workers", "[thread_startup]") {
    CHECK(ShouldWaitForStartup(true, false, false, 19'999us));
    CHECK_FALSE(ShouldWaitForStartup(true, false, false, 20ms));
    CHECK_FALSE(ShouldWaitForStartup(false, true, false, 21ms));
    auto elapsed = 0ns;
    size_t sleeps = 0;
    // Model two workers that never block: the second cannot restart the budget.
    for (int worker = 0; worker < 2; ++worker) {
        while (ShouldWaitForStartup(true, false, false, elapsed)) {
            elapsed += std::chrono::nanoseconds{StartupSleepNanoseconds};
            ++sleeps;
        }
    }
    CHECK(sleeps == 400);
    CHECK(elapsed == StartupGraceLimit);
}
