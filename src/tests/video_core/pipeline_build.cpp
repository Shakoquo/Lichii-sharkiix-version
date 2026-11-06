// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/pipeline_build.h"

namespace {
using Vulkan::EmissionBudget;
using Vulkan::PipelineBuild;

bool Pending(std::future<void>& result) {
    return result.wait_for(std::chrono::seconds{0}) != std::future_status::ready;
}
} // namespace

TEST_CASE("Production pipeline build holds a draw without blocking its producer",
          "[video_core][async_draw][pipeline_build]") {
    Common::ThreadWorker workers{1, "PipelineTest"};
    EmissionBudget budget{1};
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    int output = 0;
    PipelineBuild build{&workers, [reservation = std::shared_ptr{budget.TryAcquire(true, true)},
                                   gate, &entered, &output] {
                            entered.set_value();
                            gate.wait();
                            output = 42;
                        }};
    entered.get_future().wait();
    const bool building = !build.IsBuilt();
    const bool saturated = !budget.TryAcquire(true, true);
    auto draw = std::async(std::launch::async, [&] { build.Wait(); });
    const bool held = Pending(draw);
    int producer_progress = 0;
    ++producer_progress;
    release.set_value();
    draw.get();
    REQUIRE(building);
    REQUIRE(saturated);
    REQUIRE(held);
    REQUIRE(producer_progress == 1);
    REQUIRE(output == 42);
    REQUIRE(build.IsBuilt());
    REQUIRE_FALSE(build.Failed());
    REQUIRE(budget.Pending() == 0);
    REQUIRE(budget.TryAcquire(true, true));
}

TEST_CASE("Out of order pipeline completions preserve scheduler draw order",
          "[video_core][async_draw][pipeline_build]") {
    Common::ThreadWorker workers{2, "PipelineTest"};
    std::promise<void> release_first, release_second;
    auto first_gate = release_first.get_future().share();
    auto second_gate = release_second.get_future().share();
    PipelineBuild first{&workers, [first_gate] { first_gate.wait(); }};
    PipelineBuild second{&workers, [second_gate] { second_gate.wait(); }};
    std::vector<int> draws;
    auto scheduler = std::async(std::launch::async, [&] {
        first.Wait();
        draws.push_back(1);
        second.Wait();
        draws.push_back(2);
    });
    release_second.set_value();
    second.Wait();
    const bool first_pending = !first.IsBuilt();
    const bool scheduler_held = Pending(scheduler);
    release_first.set_value();
    scheduler.get();
    REQUIRE(first_pending);
    REQUIRE(scheduler_held);
    REQUIRE(draws == std::vector<int>{1, 2});
}

TEST_CASE("Pipeline worker exceptions release captures and notify every draw waiter",
          "[video_core][async_draw][pipeline_build]") {
    for (const bool standard_exception : {false, true}) {
        Common::ThreadWorker workers{1, "PipelineTest"};
        EmissionBudget budget{1};
        std::promise<void> release, entered;
        auto gate = release.get_future().share();
        PipelineBuild build{&workers, [reservation = std::shared_ptr{budget.TryAcquire(true, true)},
                                       gate, &entered, standard_exception] {
                                entered.set_value();
                                gate.wait();
                                if (standard_exception) {
                                    throw std::runtime_error("emission failed");
                                }
                                throw 42;
                            }};
        entered.get_future().wait();
        auto first = std::async(std::launch::async, [&] { build.Wait(); });
        auto second = std::async(std::launch::async, [&] { build.Wait(); });
        release.set_value();
        first.get();
        second.get();
        REQUIRE(build.IsBuilt());
        REQUIRE(build.Failed());
        REQUIRE(budget.Pending() == 0);
        REQUIRE(budget.TryAcquire(true, true));
    }
}

TEST_CASE("Eviction after an inline build leaves a safe queued callback",
          "[video_core][async_draw][pipeline_build]") {
    Common::ThreadWorker workers{1, "PipelineTest"};
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    workers.QueueWork([gate, &entered] {
        entered.set_value();
        gate.wait();
    });
    entered.get_future().wait();
    int builds = 0;
    auto build = std::make_unique<PipelineBuild>(&workers, [&] { ++builds; });
    build->Wait(true);
    const bool evictable = build->IsBuilt();
    build.reset();
    release.set_value();
    workers.WaitForRequests();
    REQUIRE(evictable);
    REQUIRE(builds == 1);
}

TEST_CASE("Pipeline destruction drains queued work before owner resources disappear",
          "[video_core][async_draw][pipeline_build]") {
    Common::ThreadWorker workers{1, "PipelineTest"};
    EmissionBudget budget{1};
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    workers.QueueWork([gate, &entered] {
        entered.set_value();
        gate.wait();
    });
    entered.get_future().wait();
    auto resource = std::make_unique<int>(0);
    auto build = std::make_unique<PipelineBuild>(
        &workers, [reservation = std::shared_ptr{budget.TryAcquire(true, true)},
                   output = resource.get()] { *output = 42; });
    build.reset();
    const int result = *resource;
    resource.reset();
    release.set_value();
    workers.WaitForRequests();
    REQUIRE(result == 42);
    REQUIRE(budget.Pending() == 0);
}

TEST_CASE("Pipeline destruction waits for a running callback and its cleanup",
          "[video_core][async_draw][pipeline_build]") {
    Common::ThreadWorker workers{1, "PipelineTest"};
    EmissionBudget budget{1};
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    auto resource = std::make_unique<int>(0);
    auto build = std::make_unique<PipelineBuild>(
        &workers, [reservation = std::shared_ptr{budget.TryAcquire(true, true)}, gate, &entered,
                   output = resource.get()] {
            entered.set_value();
            gate.wait();
            *output = 42;
        });
    entered.get_future().wait();
    auto destroy = std::async(std::launch::async, [&] { build.reset(); });
    const bool destruction_held = Pending(destroy);
    release.set_value();
    destroy.get();
    REQUIRE(destruction_held);
    REQUIRE(*resource == 42);
    REQUIRE(budget.Pending() == 0);
}

TEST_CASE("Emission reservations enforce the production fallback and survive budget teardown",
          "[video_core][async_draw][pipeline_build]") {
    auto budget = std::make_unique<EmissionBudget>(2);
    REQUIRE_FALSE(budget->TryAcquire(false, true));
    REQUIRE_FALSE(budget->TryAcquire(true, false));
    auto first = budget->TryAcquire(true, true);
    auto second = budget->TryAcquire(true, true);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE_FALSE(budget->TryAcquire(true, true));
    first.reset();
    REQUIRE(budget->TryAcquire(true, true));
    REQUIRE(budget->Pending() == 1);
    budget.reset();
    second.reset();
}
