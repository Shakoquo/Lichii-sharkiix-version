// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2_profile.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

using Tegra::Engines::Maxwell3D;
using Maxwell = Maxwell3D;
using ShadowMode = Maxwell::Regs::ShadowRamControl;

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    Maxwell baseline{memory};
    Maxwell fast{memory};

    Fixture() {
        for (auto* engine : {&baseline, &fast}) {
            engine->BindRasterizer(&rasterizer);
            for (u32 method = 0; method < Maxwell::Regs::NUM_REGS; ++method) {
                engine->dirty.tables[0][method] = static_cast<u8>(method % 256);
                engine->dirty.tables[1][method] = static_cast<u8>((method / 7) % 256);
            }
            engine->dirty.flags.reset();
        }
    }

    void SetMode(ShadowMode mode) {
        for (auto* engine : {&baseline, &fast}) {
            engine->CallMethod(system, MAXWELL3D_REG_INDEX(shadow_ram_control),
                               static_cast<u32>(mode), true);
        }
    }

    void Write(u32 method, u32 argument) {
        REQUIRE_FALSE(baseline.execution_mask[method]);
        auto& counters = VideoCommon::Nx2Profile::t_data.counters;
        const auto before = counters;
        baseline.CallMethod(system, method, argument, true);
        const auto middle = counters;
        static_cast<Tegra::Engines::EngineInterface&>(fast).CallStateMethod(system, method, argument);
        for (size_t i = 0; i < counters.size(); ++i) {
            CHECK(counters[i] - middle[i] == middle[i] - before[i]);
        }
        CHECK(fast.regs.reg_array == baseline.regs.reg_array);
        CHECK(fast.shadow_state.reg_array == baseline.shadow_state.reg_array);
        CHECK(fast.dirty.flags == baseline.dirty.flags);
        CHECK(fast.draw_manager.draw_state.instance_count ==
              baseline.draw_manager.draw_state.instance_count);
        CHECK(fast.draw_manager.draw_state.drawn_instances ==
              baseline.draw_manager.draw_state.drawn_instances);
        CHECK(fast.draw_manager.draw_state.base_instance ==
              baseline.draw_manager.draw_state.base_instance);
    }
};

} // namespace

TEST_CASE("Direct Maxwell state writes match normal methods in every shadow mode",
          "[video_core][maxwell_state_method]") {
    Fixture f;
    constexpr std::array<u32, 5> methods{
        MAXWELL3D_REG_INDEX(global_base_instance_index),
        MAXWELL3D_REG_INDEX(global_base_vertex_index),
        MAXWELL3D_REG_INDEX(clear_color),
        MAXWELL3D_REG_INDEX(blend_color),
        MAXWELL3D_REG_INDEX(scissor_test),
    };
    for (const auto mode : {ShadowMode::Track, ShadowMode::TrackWithFilter,
                            ShadowMode::Passthrough, ShadowMode::Replay}) {
        CAPTURE(mode);
        f.SetMode(mode);
        for (u32 iteration = 0; iteration < 16; ++iteration) {
            for (const u32 method : methods) {
                f.Write(method, iteration * 17 + method);
                f.baseline.dirty.flags.reset();
                f.fast.dirty.flags.reset();
                f.Write(method, iteration * 17 + method);
            }
        }
    }
}

TEST_CASE("Direct Maxwell writes flush deferred draws before changing their state",
          "[video_core][maxwell_state_method]") {
    Fixture f;
    f.SetMode(ShadowMode::Passthrough);
    constexpr u32 method = MAXWELL3D_REG_INDEX(global_base_instance_index);
    for (auto* engine : {&f.baseline, &f.fast}) {
        engine->regs.global_base_instance_index = 19;
        auto& draw = engine->draw_manager.draw_state;
        draw.draw_mode = Maxwell::DrawManager::DrawMode::Instance;
        draw.instance_count = 3;
        engine->regs.vertex_buffer.count = 3;
    }
    f.Write(method, 29);
    CHECK_FALSE(f.fast.draw_manager.HasDeferredDraw());
    CHECK(f.fast.draw_manager.draw_state.base_instance == 19);
    CHECK(f.fast.regs.global_base_instance_index == 29);
    CHECK(f.fast.draw_manager.draw_state.drawn_instances == 3);
}

TEST_CASE("Unchanged direct Maxwell state preserves pending draws when filtering is enabled",
          "[video_core][maxwell_state_method]") {
    Fixture f;
    constexpr u32 method = MAXWELL3D_REG_INDEX(global_base_instance_index);
    for (const auto mode : {ShadowMode::Passthrough, ShadowMode::Replay}) {
        f.SetMode(ShadowMode::Track);
        f.Write(method, 19);
        f.SetMode(mode);
        for (auto* engine : {&f.baseline, &f.fast}) {
            auto& draw = engine->draw_manager.draw_state;
            draw.draw_mode = Maxwell::DrawManager::DrawMode::Instance;
            draw.instance_count = 3;
        }
        f.Write(method, mode == ShadowMode::Replay ? 999 : 19);
        const char* skip_same = std::getenv("EDEN_NX2_SKIP_SAME");
        CHECK(f.fast.draw_manager.HasDeferredDraw() ==
              (skip_same == nullptr || skip_same[0] != '0'));
        f.baseline.draw_manager.DrawDeferred(f.baseline);
        f.fast.draw_manager.DrawDeferred(f.fast);
    }
}

TEST_CASE("Direct Maxwell writes preserve drained sink and shadow replay boundaries",
          "[video_core][maxwell_state_method]") {
    Fixture f;
    constexpr u32 method = MAXWELL3D_REG_INDEX(global_base_instance_index);
    f.SetMode(ShadowMode::Track);
    for (auto* engine : {&f.baseline, &f.fast}) {
        engine->method_sink.emplace_back(method, 12);
        engine->method_sink.emplace_back(method, 17);
        engine->ConsumeSink(f.system);
        CHECK(engine->method_sink.empty());
    }
    f.Write(method, 23);
    CHECK(f.fast.shadow_state.reg_array[method] == 23);
    f.SetMode(ShadowMode::Replay);
    f.Write(method, 999);
    CHECK(f.fast.regs.reg_array[method] == 23);
    CHECK(f.fast.shadow_state.reg_array[method] == 23);
    CHECK(f.baseline.execution_mask[MAXWELL3D_REG_INDEX(shadow_ram_control)]);
    CHECK(f.baseline.execution_mask[MAXWELL3D_REG_INDEX(draw.end)]);
    CHECK(f.baseline.execution_mask[MAXWELL3D_REG_INDEX(const_buffer.buffer)]);
    CHECK(f.baseline.execution_mask[Maxwell::RootTables::LoadMethod]);
}

TEST_CASE("Other engines retain normal CallMethod semantics for state entrypoints",
          "[video_core][maxwell_state_method]") {
    struct Engine final : Tegra::Engines::EngineInterface {
        u32 seen_method{};
        u32 seen_argument{};
        bool last{};
        void CallMethod(Core::System&, u32 method, u32 argument, bool is_last_call) override {
            seen_method = method;
            seen_argument = argument;
            last = is_last_call;
        }
        void CallMultiMethod(Core::System&, u32, const u32*, u32, u32) override {}
    } engine;
    ScopedSystemTestContext context;
    Core::System system;
    engine.CallStateMethod(system, 45, 67);
    CHECK(engine.seen_method == 45);
    CHECK(engine.seen_argument == 67);
    CHECK(engine.last);
}
