// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace VideoCore::Nx2Debug {

struct FlashConfig {
    u64 generation{};
    std::filesystem::path directory;
    double threshold{};
    double min_luma{};
    u32 max_captures{1};
    std::chrono::steady_clock::time_point deadline;
    bool buffers{};
    bool sampled{};
};

struct FlashCapture {
    u64 id{};
    std::filesystem::path directory;
    u64 trigger_frame{};
    u64 capture_frame{};
    double trigger_white_fraction{};
    std::optional<double> capture_white_fraction;
    bool complete{};
    std::string error;
};

struct FlashStatus {
    FlashConfig config;
    bool armed{};
    std::string reason;
    std::vector<FlashCapture> captures;
    u64 samples{};
    u64 dropped{};
    u64 cpu_checks{};
    u64 gpu_checks{};
    double cpu_total_ms{};
    double cpu_max_ms{};
    double gpu_total_ms{};
    double gpu_max_ms{};
};

double WhiteFraction(std::span<const u8> rgba, double min_luma);
FlashStatus ArmFlash(FlashConfig config);
FlashStatus DisarmFlash();
void ResetFlash();
FlashStatus ReadFlashStatus();
bool FlashEnabled() noexcept;
std::optional<FlashConfig> GetFlashConfig();
void ObserveFlash(u64 generation, u64 source_frame, u64 current_frame, double fraction);
void NoteFlashCost(u64 generation, double cpu_ms, std::optional<double> gpu_ms, bool dropped);
bool FinishFlashCapture(u64 id, const std::string& error);
void FailFlash(u64 generation, std::string error);
void DropFlashSample(u64 generation, u64 frame);

} // namespace VideoCore::Nx2Debug
