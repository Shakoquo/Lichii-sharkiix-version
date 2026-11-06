// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "video_core/nx2_debug_control.h"
#include "video_core/nx2_flash_trigger.h"

using namespace VideoCore::Nx2Debug;
using namespace std::chrono_literals;

TEST_CASE("Flash luma sampling counts Rec709 luminance, not alpha", "[flash_trigger]") {
    const std::array<u8, 16> rgba{255,255,255,0, 0,0,0,255, 255,0,0,255, 0,255,0,255};
    REQUIRE(WhiteFraction(rgba, 1) == 0.25);
    REQUIRE(WhiteFraction(rgba, 0.7) == 0.5);
    REQUIRE(WhiteFraction(rgba, 0.2) == 0.75);
    REQUIRE(WhiteFraction(rgba, 0) == 1);
    REQUIRE(WhiteFraction({}, 0.5) == 0);
}

TEST_CASE("Flash captures track delayed triggers and actual captured fractions", "[flash_trigger]") {
    BeginSession();
    FlashConfig config{.directory = "flash-test", .threshold = 0.8, .min_luma = 0.95,
                       .max_captures = 2, .deadline = std::chrono::steady_clock::now() + 60s,
                       .buffers = true, .sampled = false};
    const auto armed = ArmFlash(config);
    REQUIRE(FlashEnabled());
    REQUIRE_THROWS(ArmFlash(config));
    const auto id = armed.config.generation;
    ObserveFlash(id, 1, 3, 0.7);
    REQUIRE_FALSE(TakeCapture());
    ObserveFlash(id, 2, 4, 0.9);
    const auto request = TakeCapture();
    REQUIRE(request);
    REQUIRE(request->buffers);
    REQUIRE_FALSE(request->sampled);
    auto report = ReadFlashStatus();
    REQUIRE(report.captures.size() == 1);
    REQUIRE(report.captures[0].trigger_frame == 2);
    REQUIRE(report.captures[0].capture_frame == 5);
    REQUIRE(report.captures[0].trigger_white_fraction == 0.9);
    ObserveFlash(id, 3, 5, 1);
    REQUIRE_FALSE(TakeCapture());
    FinishCapture(request->id, {});
    ObserveFlash(id, 5, 7, 0.1);
    REQUIRE(ReadFlashStatus().captures[0].capture_white_fraction == 0.1);
    ObserveFlash(id, 4, 7, 1);
    REQUIRE_FALSE(TakeCapture());
    ObserveFlash(id, 6, 8, 0.85);
    const auto second = TakeCapture();
    REQUIRE(second);
    REQUIRE_FALSE(ReadFlashStatus().armed);
    REQUIRE(FlashEnabled());
    FinishCapture(second->id, {});
    ObserveFlash(id, 9, 11, 0.4);
    REQUIRE_FALSE(FlashEnabled());
    REQUIRE(ReadFlashStatus().captures[1].capture_white_fraction == 0.4);
    ObserveFlash(id, 10, 12, 1);
    REQUIRE_FALSE(TakeCapture());
    EndSession();
}

TEST_CASE("Flash timeout disarm generations and missing probes are explicit", "[flash_trigger]") {
    BeginSession();
    FlashConfig config{.directory = "flash-test", .threshold = 0.8, .min_luma = 0.95,
                       .deadline = std::chrono::steady_clock::now() - 1s};
    REQUIRE_FALSE(ArmFlash(config).armed);
    REQUIRE(ReadFlashStatus().reason == "timeout");
    config.deadline = std::chrono::steady_clock::now() + 60s;
    const auto first = ArmFlash(config).config.generation;
    DisarmFlash();
    REQUIRE_FALSE(FlashEnabled());
    const auto second = ArmFlash(config).config.generation;
    ObserveFlash(first, 1, 2, 1);
    REQUIRE_FALSE(TakeCapture());
    ObserveFlash(second, 1, 2, 1);
    auto request = TakeCapture();
    REQUIRE(request);
    DropFlashSample(second, 3);
    FinishCapture(request->id, {});
    REQUIRE_FALSE(ReadFlashStatus().captures[0].error.empty());
    REQUIRE_FALSE(FlashEnabled());
    ResetFlash();
    REQUIRE(ReadFlashStatus().captures.empty());
    REQUIRE_FALSE(FinishFlashCapture(4, ""));
    EndSession();
}

TEST_CASE("Flash telemetry separates CPU costs GPU timestamps and dropped samples", "[flash_trigger]") {
    BeginSession();
    FlashConfig config{.directory = "flash-test", .threshold = 1, .min_luma = 1,
                       .deadline = std::chrono::steady_clock::now() + 60s};
    const u64 id = ArmFlash(config).config.generation;
    NoteFlashCost(id, 0.02, std::nullopt, false);
    NoteFlashCost(id, 0.04, std::nullopt, true);
    NoteFlashCost(id, -1, 0.1, false);
    const auto report = ReadFlashStatus();
    REQUIRE(report.cpu_checks == 2);
    REQUIRE(report.cpu_max_ms == 0.04);
    REQUIRE(report.gpu_checks == 1);
    REQUIRE(report.gpu_total_ms == 0.1);
    REQUIRE(report.dropped == 1);
    EndSession();
}
