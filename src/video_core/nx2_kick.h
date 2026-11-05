// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>

#include "common/thread.h"

namespace VideoCommon {

/// Wakes the GPU thread while it polls its suspended channels: set when a command is queued,
/// when the fencing thread has run released operations and when a GPU write meets a waited
/// semaphore. It slept 1 ms between polls, half of its time in Ocarina of Time once acquires
/// stopped waiting for the host GPU.
inline Common::Event& Nx2GpuKick() {
    static Common::Event event;
    return event;
}

} // namespace VideoCommon

namespace VideoCommon {

/// The GPU-to-CPU synchronisation of High accuracy while the accuracy setting is Low (which the
/// split GPU thread needs): fences run their operations once the host GPU passed them, payload
/// releases wait for the commands before them and CPU reads of textures the GPU wrote flush.
/// Low's shortcuts let Ocarina of Time read its exposure early (light flashing every ~2 s).
/// EDEN_NX2_ACCURATE_SYNC=0: Low's own behaviour.
inline bool Nx2AccurateSync() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_ACCURATE_SYNC");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

} // namespace VideoCommon
