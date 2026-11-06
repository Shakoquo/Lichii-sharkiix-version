// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::ProgramCaptureState;

constexpr std::array<Instruction, 10> ExitHandlerProgram{{
    {{0x0000734e, 0, 0, 0x000fe400}},
    {{0x00037802, 0x70, 0x00000f00, 0x000fc800}},
    {{0x00007210, 3, 0x07f1e0ff, 0x000fc800}},
    {{0x01017210, 0xff, 0x007fe4ff, 0x000fc800}},
    {{0x00007357, 0, 0, 0x0001e400}},
    {{0x0000894d, 0, 0x03800000, 0x021fea00}},
    {{0x0000794d, 0, 0x03800000, 0x003fea00}},
    {{0x00ff7324, 0xff, 0, 0x0001e200}},
    {{0x00007957, 0, 0, 0x000fe200}},
    {{0x0000794d, 0, 0x03800000, 0x001fea00}},
}};

constexpr auto BananzaExitHandlerProgram = [] {
    std::array<Instruction, 0x740 / 16> code{};
    code.fill(Instruction{{0x00007918, 0, 0, 0x000fc000}});
    std::copy_n(ExitHandlerProgram.begin(), 5, code.begin());
    code[1].words[1] = 0x710;
    code[2].words[3] = 0x000fca00;
    code[3] = Instruction{{0xff017224, 0xff, 0x000e0401, 0x000fc800}};
    code[0x6e0 / 16] = ExitHandlerProgram[6];
    code[0x6f0 / 16] = Instruction{{0xff007322, 0x0003a0ff, 0, 0x0001e400}};
    code[0x700 / 16] = ExitHandlerProgram[6];
    std::copy_n(ExitHandlerProgram.begin() + 7, 3, code.begin() + 0x710 / 16);
    return code;
}();

TEST_CASE("Ampere IMAD exit capture includes the complete Bananza handler",
          "[video_core][ampere]") {
    ProgramCaptureState capture;
    for (u32 index = 0; index < BananzaExitHandlerProgram.size(); ++index) {
        const auto expected = index + 1 == BananzaExitHandlerProgram.size()
                                  ? ProgramCaptureState::Result::Complete
                                  : ProgramCaptureState::Result::Continue;
        REQUIRE(capture.Observe(BananzaExitHandlerProgram[index], index) == expected);
    }
    REQUIRE(sizeof(BananzaExitHandlerProgram) == 0x740);
}

TEST_CASE("Ampere IMAD exit capture requires exact carry fields", "[video_core][ampere]") {
    for (u32 word = 0; word < 3; ++word) {
        for (u32 bit = 0; bit < 32; ++bit) {
            auto code = BananzaExitHandlerProgram;
            code[3].words[word] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index <= 0x6e0 / 16; ++index) {
                const auto expected = index == 0x6e0 / 16
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture bounds the handler destination", "[video_core][ampere]") {
    for (const u32 target : {0U, 0x40U, 0x711U, ProgramCaptureState::MaximumInstructions * 16}) {
        auto code = BananzaExitHandlerProgram;
        code[1].words[1] = target;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 4; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[4], 4) == ProgramCaptureState::Result::InvalidTarget);
    }
}

TEST_CASE("Ampere IMAD exit capture validates extended instruction modes",
          "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto code = BananzaExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index <= 0x6e0 / 16; ++index) {
                const auto expected = index == 0x6e0 / 16
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture ignores scheduling fields", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit = 9; bit < 30; ++bit) {
            auto code = BananzaExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < code.size(); ++index) {
                const auto expected = index + 1 == code.size()
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

TEST_CASE("Ampere IMAD exit capture does not bypass unsupported lowering",
          "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(BananzaExitHandlerProgram);
    REQUIRE_FALSE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.empty());
    REQUIRE(lowered.error_offset == 0);
}

TEST_CASE("Ampere capture includes the static geometry exit handler", "[video_core][ampere]") {
    ProgramCaptureState capture;
    for (u32 index = 0; index < ExitHandlerProgram.size(); ++index) {
        const auto expected = index + 1 == ExitHandlerProgram.size()
                                  ? ProgramCaptureState::Result::Complete
                                  : ProgramCaptureState::Result::Continue;
        REQUIRE(capture.Observe(ExitHandlerProgram[index], index) == expected);
    }
}

TEST_CASE("Ampere capture bounds the static exit destination", "[video_core][ampere]") {
    for (const u32 target : {0U, 0x40U, 0x71U, ProgramCaptureState::MaximumInstructions * 16}) {
        auto code = ExitHandlerProgram;
        code[1].words[1] = target;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 4; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[4], 4) == ProgramCaptureState::Result::InvalidTarget);
    }
}

TEST_CASE("Ampere capture does not guess other exit setup forms", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        auto code = ExitHandlerProgram;
        code[setup].words[0] ^= 1U << 12;
        ProgramCaptureState capture;
        for (u32 index = 0; index < 6; ++index)
            REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
        REQUIRE(capture.Observe(code[6], 6) == ProgramCaptureState::Result::Complete);
    }
}

TEST_CASE("Ampere static exit capture does not bypass unsupported lowering", "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(ExitHandlerProgram);
    REQUIRE_FALSE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.empty());
    REQUIRE(lowered.error_offset == 0);
}

TEST_CASE("Ampere static exit capture validates extended instruction modes", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 30U, 31U}) {
            auto code = ExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < 6; ++index)
                REQUIRE(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
            REQUIRE(capture.Observe(code[6], 6) == ProgramCaptureState::Result::Complete);
        }
    }
}

TEST_CASE("Ampere static exit capture ignores scheduling fields", "[video_core][ampere]") {
    for (u32 setup = 0; setup < 5; ++setup) {
        for (u32 bit = 9; bit < 30; ++bit) {
            auto code = ExitHandlerProgram;
            code[setup].words[3] ^= 1U << bit;
            ProgramCaptureState capture;
            for (u32 index = 0; index < code.size(); ++index) {
                const auto expected = index + 1 == code.size()
                                          ? ProgramCaptureState::Result::Complete
                                          : ProgramCaptureState::Result::Continue;
                REQUIRE(capture.Observe(code[index], index) == expected);
            }
        }
    }
}

} // namespace
