// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <fmt/format.h>
#include "video_core/nx2_debug_control.h"
#include "video_core/nx2_flash_trigger.h"

namespace VideoCore::Nx2Debug {
namespace {
std::mutex mutex;
FlashStatus status;
std::atomic<bool> enabled{};
u64 generation{};
u64 capture_id{u64{1} << 63};

void Update() {
    if (status.config.generation && std::chrono::steady_clock::now() >= status.config.deadline) {
        if (status.armed) {
            status.reason = "timeout";
        }
        status.armed = false;
        for (auto& capture : status.captures) {
            if (!capture.capture_white_fraction && capture.error.empty()) {
                capture.error = "captured frame measurement timed out";
            }
        }
    }
    const bool awaiting_sample = std::ranges::any_of(status.captures, [](const auto& capture) {
        return !capture.capture_white_fraction && capture.error.empty();
    });
    enabled.store(status.armed || awaiting_sample, std::memory_order_relaxed);
}
}

double WhiteFraction(std::span<const u8> rgba, double min_luma) {
    if (rgba.empty() || rgba.size() % 4 != 0) {
        return 0;
    }
    size_t white{};
    const double threshold = min_luma * 255.0 * 10000.0;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        white += 2126 * rgba[i] + 7152 * rgba[i + 1] + 722 * rgba[i + 2] >= threshold;
    }
    return static_cast<double>(white) / static_cast<double>(rgba.size() / 4);
}

FlashStatus ArmFlash(FlashConfig config) {
    std::scoped_lock lock{mutex};
    if (status.armed || std::ranges::any_of(status.captures, [](const auto& capture) {
            return !capture.complete;
        })) {
        throw std::runtime_error{"flash trigger or its dump is still active; disarm first and wait for the dump"};
    }
    config.generation = ++generation;
    status = {};
    status.config = std::move(config);
    status.armed = true;
    Update();
    return status;
}

FlashStatus DisarmFlash() {
    std::scoped_lock lock{mutex};
    status.armed = false;
    status.reason = "disarmed";
    for (auto& capture : status.captures) {
        if (!capture.capture_white_fraction && capture.error.empty()) {
            capture.error = "disarmed before capture-frame measurement";
        }
    }
    Update();
    return status;
}

void ResetFlash() {
    std::scoped_lock lock{mutex};
    ++generation;
    status = {};
    enabled.store(false, std::memory_order_relaxed);
}

FlashStatus ReadFlashStatus() {
    std::scoped_lock lock{mutex};
    Update();
    return status;
}

bool FlashEnabled() noexcept {
    return enabled.load(std::memory_order_relaxed);
}

std::optional<FlashConfig> GetFlashConfig() {
    std::scoped_lock lock{mutex};
    Update();
    return enabled.load(std::memory_order_relaxed) ? std::optional{status.config} : std::nullopt;
}

void ObserveFlash(u64 session, u64 source_frame, u64 current_frame, double fraction) {
    std::optional<CaptureRequest> request;
    {
        std::scoped_lock lock{mutex};
        if (session != status.config.generation) {
            return;
        }
        Update();
        ++status.samples;
        for (auto& capture : status.captures) {
            if (capture.capture_frame == source_frame) {
                capture.capture_white_fraction = fraction;
            }
        }
        const bool busy = std::ranges::any_of(status.captures, [](const auto& capture) {
            return !capture.complete || (!capture.capture_white_fraction && capture.error.empty());
        });
        const bool fresh = status.captures.empty() || source_frame > status.captures.back().capture_frame;
        if (status.armed && !busy && fresh && fraction >= status.config.threshold) {
            const u64 id = ++capture_id;
            const auto directory = status.config.directory /
                                   fmt::format("capture-{:03}", status.captures.size() + 1);
            status.captures.push_back({.id = id, .directory = directory,
                                      .trigger_frame = source_frame,
                                      .capture_frame = current_frame + 1,
                                      .trigger_white_fraction = fraction});
            request = CaptureRequest{.id = id, .directory = directory,
                                     .sampled = status.config.sampled, .buffers = status.config.buffers};
            if (status.captures.size() >= status.config.max_captures) {
                status.armed = false;
                status.reason = "capture_limit";
            }
        }
        Update();
    }
    if (request) {
        RequestCapture(std::move(*request));
    }
}

void NoteFlashCost(u64 session, double cpu_ms, std::optional<double> gpu_ms, bool dropped) {
    std::scoped_lock lock{mutex};
    if (session != status.config.generation) {
        return;
    }
    if (cpu_ms >= 0) {
        ++status.cpu_checks;
        status.cpu_total_ms += cpu_ms;
        status.cpu_max_ms = (std::max)(status.cpu_max_ms, cpu_ms);
    }
    if (gpu_ms) {
        ++status.gpu_checks;
        status.gpu_total_ms += *gpu_ms;
        status.gpu_max_ms = (std::max)(status.gpu_max_ms, *gpu_ms);
    }
    status.dropped += dropped;
}

bool FinishFlashCapture(u64 id, const std::string& error) {
    if (!(id & (u64{1} << 63))) {
        return false;
    }
    std::scoped_lock lock{mutex};
    for (auto& capture : status.captures) {
        if (capture.id == id) {
            capture.complete = true;
            if (!error.empty()) {
                capture.error = error;
            }
        }
    }
    Update();
    return true;
}

void FailFlash(u64 session, std::string error) {
    std::scoped_lock lock{mutex};
    if (session != status.config.generation) {
        return;
    }
    status.armed = false;
    status.reason = error;
    for (auto& capture : status.captures) {
        if (!capture.capture_white_fraction) {
            capture.error = error;
        }
    }
    Update();
}

void DropFlashSample(u64 session, u64 frame) {
    std::scoped_lock lock{mutex};
    if (session != status.config.generation) {
        return;
    }
    for (auto& capture : status.captures) {
        if (capture.capture_frame == frame) {
            capture.error = "captured frame probe dropped: GPU readback ring full";
        }
    }
    Update();
}

} // namespace VideoCore::Nx2Debug
