// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/path_util.h"
#include "common/settings.h"
#include "input_common/drivers/tas_input.h"
#include "input_common/input_poller.h"

namespace {

using InputCommon::TasInput::Tas;
using InputCommon::TasInput::TasState;

struct TasFixture {
    TasFixture() {
        static std::atomic<u64> sequence{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
                    ("tas-input-test-" + std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directory(directory);
        Common::FS::SetEdenPath(Common::FS::EdenPath::TASDir, directory);
        Settings::values.tas_enable = true;
        Settings::values.tas_loop = false;
    }

    ~TasFixture() {
        tas.reset();
        Common::FS::SetEdenPath(Common::FS::EdenPath::TASDir, previous_directory);
        Settings::values.tas_enable = previous_enabled;
        Settings::values.tas_loop = previous_loop;
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    void Write(std::string_view script, size_t player = 0) {
        std::ofstream file(directory / ("script0-" + std::to_string(player + 1) + ".txt"));
        REQUIRE(file.is_open());
        file << script;
        file.close();
        REQUIRE_FALSE(file.fail());
    }

    void Start() {
        tas = std::make_shared<Tas>("tas");
        for (size_t player = 0; player < changes.size(); ++player) {
            const PadIdentifier pad{.port = player};
            for (int button = 0; button < 64; ++button) {
                tas->PreSetButton(pad, button);
                tas->SetCallback({pad, EngineInputType::Button, button,
                                  {[this, player, button] { ++changes[player][button]; }}});
            }
            for (int axis = 0; axis < 4; ++axis) {
                tas->PreSetAxis(pad, axis);
                tas->SetCallback({pad, EngineInputType::Analog, axis,
                                  {[this, player, axis] { ++changes[player][64 + axis]; }}});
            }
        }
        tas->StartStop();
    }

    void RequireActive() const {
        REQUIRE(tas->GetButton({}, 0));
        REQUIRE_FALSE(tas->GetButton({}, 1));
        REQUIRE(tas->GetAxis({}, 0) == 1.0f);
        REQUIRE(tas->GetAxis({}, 1) == -1.0f);
        REQUIRE(tas->GetAxis({}, 2) == -1.0f);
        REQUIRE(tas->GetAxis({}, 3) == 1.0f);
    }

    void RequireNeutral() const {
        REQUIRE_FALSE(tas->GetButton({}, 0));
        REQUIRE_FALSE(tas->GetButton({}, 1));
        for (int axis = 0; axis < 4; ++axis) {
            REQUIRE(tas->GetAxis({}, axis) == 0.0f);
        }
    }

    const std::filesystem::path previous_directory =
        Common::FS::GetEdenPath(Common::FS::EdenPath::TASDir);
    const bool previous_enabled = Settings::values.tas_enable.GetValue();
    const bool previous_loop = Settings::values.tas_loop.GetValue();
    std::filesystem::path directory;
    std::array<std::array<size_t, 68>, InputCommon::TasInput::PLAYER_NUMBER> changes{};
    std::shared_ptr<Tas> tas;
};

constexpr std::string_view ActiveScript =
    "0 KEY_A 32767;-32767 -32767;32767\n"
    "1 KEY_A 32767;-32767 -32767;32767\n"
    "2 KEY_A 32767;-32767 -32767;32767\n"
    "3 KEY_A 32767;-32767 -32767;32767\n";

} // namespace

TEST_CASE("TAS only notifies changed buttons and axes", "[input][tas]") {
    TasFixture fixture;
    fixture.Write("0 KEY_A 32767;-32767 -32767;32767\n"
                  "1 KEY_A 32767;-32767 -32767;32767\n"
                  "2 KEY_B 0;32767 32767;-32767\n");
    fixture.Write("0 KEY_B 0;0 0;0\n", 1);
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(fixture.tas->GetButton(PadIdentifier{.port = 1}, 1));
    REQUIRE(fixture.changes[0][0] == 1);
    REQUIRE(fixture.changes[0][1] == 0);
    for (size_t axis = 64; axis < 68; ++axis) {
        REQUIRE(fixture.changes[0][axis] == 1);
    }
    REQUIRE(fixture.changes[2] == std::array<size_t, 68>{});

    const auto first = fixture.changes;
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(fixture.changes[0] == first[0]);
    REQUIRE_FALSE(fixture.tas->GetButton(PadIdentifier{.port = 1}, 1));
    REQUIRE(fixture.changes[1][1] == 2);

    fixture.tas->UpdateThread();
    REQUIRE_FALSE(fixture.tas->GetButton({}, 0));
    REQUIRE(fixture.tas->GetButton({}, 1));
    REQUIRE(fixture.tas->GetAxis({}, 0) == 0.0f);
    REQUIRE(fixture.tas->GetAxis({}, 1) == 1.0f);
    REQUIRE(fixture.tas->GetAxis({}, 2) == 1.0f);
    REQUIRE(fixture.tas->GetAxis({}, 3) == -1.0f);
    REQUIRE(fixture.changes[0][0] == 2);
    REQUIRE(fixture.changes[0][1] == 1);
    for (size_t axis = 64; axis < 68; ++axis) {
        REQUIRE(fixture.changes[0][axis] == 2);
    }
}

TEST_CASE("TAS clears once when stopped and reapplies on restart", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    fixture.tas->Stop();
    fixture.tas->UpdateThread();
    fixture.RequireNeutral();
    REQUIRE(fixture.changes[0][0] == 2);
    const auto stopped = fixture.changes;
    fixture.tas->UpdateThread();
    REQUIRE(fixture.changes == stopped);
    fixture.tas->StartStop();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(fixture.changes[0][0] == 3);
}

TEST_CASE("TAS clears at script completion and replays after loop or restart", "[input][tas]") {
    TasFixture fixture;
    fixture.Write("0 KEY_A 32767;-32767 -32767;32767\n");
    bool looping = false;
    SECTION("loop") {
        looping = true;
    }
    SECTION("restart") {}
    Settings::values.tas_loop = looping;
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    fixture.tas->UpdateThread();
    fixture.RequireNeutral();
    REQUIRE(std::get<0>(fixture.tas->GetStatus()) ==
            (looping ? TasState::Running : TasState::Stopped));
    REQUIRE(std::get<1>(fixture.tas->GetStatus()) == 0);
    if (!looping) {
        fixture.tas->StartStop();
    }
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(fixture.changes[0][0] == 3);
}

TEST_CASE("TAS reload compares the new script against applied state", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.Write("0 KEY_B 0;0 0;0\n");
    fixture.tas->Reset();
    fixture.tas->UpdateThread();
    REQUIRE(std::get<1>(fixture.tas->GetStatus()) == 1);
    REQUIRE_FALSE(fixture.tas->GetButton({}, 0));
    REQUIRE(fixture.tas->GetButton({}, 1));
    REQUIRE(fixture.changes[0][0] == 2);
    REQUIRE(fixture.changes[0][1] == 1);
    for (int axis = 0; axis < 4; ++axis) {
        REQUIRE(fixture.tas->GetAxis({}, axis) == 0.0f);
        REQUIRE(fixture.changes[0][64 + axis] == 2);
    }
}

TEST_CASE("TAS does not cache writes skipped during input configuration", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    size_t mappings = 0;
    fixture.tas->SetMappingCallback({[&mappings](const InputCommon::MappingData&) { ++mappings; }});
    fixture.tas->BeginConfiguration();
    fixture.tas->UpdateThread();
    fixture.RequireNeutral();
    REQUIRE(mappings == 5);
    fixture.tas->UpdateThread();
    fixture.RequireNeutral();
    REQUIRE(mappings == 10);
    fixture.tas->EndConfiguration();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(mappings == 10);
    const auto applied = fixture.changes;
    fixture.tas->UpdateThread();
    REQUIRE(fixture.changes == applied);
}

TEST_CASE("TAS reapplies unchanged script input after public engine resets", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    fixture.tas->ResetButtonState();
    REQUIRE_FALSE(fixture.tas->GetButton({}, 0));
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    REQUIRE(fixture.changes[0][0] == 3);
    fixture.tas->ResetAnalogState();
    REQUIRE(fixture.tas->GetAxis({}, 0) == 0.0f);
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    for (size_t axis = 64; axis < 68; ++axis) {
        REQUIRE(fixture.changes[0][axis] == 3);
    }
}

TEST_CASE("TAS retries clearing input after configuration ends", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    fixture.tas->UpdateThread();
    fixture.tas->BeginConfiguration();
    fixture.tas->Stop();
    fixture.tas->UpdateThread();
    fixture.RequireActive();
    fixture.tas->EndConfiguration();
    fixture.tas->UpdateThread();
    fixture.RequireNeutral();
    const auto cleared = fixture.changes;
    fixture.tas->UpdateThread();
    REQUIRE(fixture.changes == cleared);
}

TEST_CASE("TAS rebinding force-updates devices from authoritative held input", "[input][tas]") {
    TasFixture fixture;
    fixture.Write(ActiveScript);
    fixture.Start();
    fixture.tas->UpdateThread();
    InputCommon::InputFactory factory{fixture.tas};
    for (int rebind = 0; rebind < 2; ++rebind) {
        std::vector<Common::Input::CallbackStatus> buttons;
        std::vector<Common::Input::CallbackStatus> sticks;
        auto button = factory.Create(Common::ParamPackage{"button:0"});
        auto stick = factory.Create(Common::ParamPackage{"axis_x:0,axis_y:1"});
        button->SetCallback({[&buttons](const auto& status) { buttons.push_back(status); }});
        stick->SetCallback({[&sticks](const auto& status) { sticks.push_back(status); }});
        button->ForceUpdate();
        stick->ForceUpdate();
        REQUIRE(buttons.size() == 1);
        REQUIRE(buttons.back().button_status.value);
        REQUIRE(sticks.size() == 1);
        REQUIRE(sticks.back().stick_status.x.raw_value == 1.0f);
        REQUIRE(sticks.back().stick_status.y.raw_value == -1.0f);
        const auto before = fixture.changes;
        fixture.tas->UpdateThread();
        REQUIRE(fixture.changes == before);
        REQUIRE(buttons.size() == 1);
        REQUIRE(sticks.size() == 1);
    }
}
