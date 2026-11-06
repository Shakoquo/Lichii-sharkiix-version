// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/bounded_threadsafe_queue.h"

using namespace std::chrono_literals;

TEST_CASE("Timed queue pop preserves FIFO and handles already queued work", "[common][queue_wait]") {
    Common::SPSCQueue<int, 4> queue;
    int output = -1;
    CHECK_FALSE(queue.TryPop(output));
    // Simulate a producer publishing between the consumer's failed TryPop and wait.
    REQUIRE(queue.TryEmplace(11));
    REQUIRE(queue.TryEmplace(22));
    CHECK(queue.PopWaitFor(output, 0ms));
    CHECK(output == 11);
    CHECK(queue.PopWaitFor(output, 0ms));
    CHECK(output == 22);
    CHECK_FALSE(queue.TryPop(output));
}

TEST_CASE("Timed queue pop times out without changing its output", "[common][queue_wait]") {
    Common::SPSCQueue<int, 4> queue;
    int output = 73;
    CHECK_FALSE(queue.PopWaitFor(output, 0ms));
    CHECK_FALSE(queue.PopWaitFor(output, 2ms));
    CHECK(output == 73);
    REQUIRE(queue.TryEmplace(42));
    CHECK(queue.PopWaitFor(output, 1s));
    CHECK(output == 42);
}

TEST_CASE("Timed queue pop wakes on a producer before its timeout", "[common][queue_wait]") {
    Common::SPSCQueue<int, 4> queue;
    std::promise<void> starting;
    auto started = starting.get_future();
    std::promise<bool> completion;
    auto result = completion.get_future();
    std::jthread consumer([&](std::stop_token stop) {
        int output = -1;
        starting.set_value();
        const bool popped = queue.PopWaitFor(output, 10s, stop);
        completion.set_value(popped && output == 42);
    });
    started.wait();
    std::this_thread::sleep_for(5ms);
    const bool pushed = queue.TryEmplace(42);
    const auto status = result.wait_for(2s);
    consumer.request_stop();
    consumer.join();
    CHECK(pushed);
    CHECK(status == std::future_status::ready);
    CHECK(result.get());
}

TEST_CASE("Timed queue pop wakes on cancellation and preserves queued data", "[common][queue_wait]") {
    Common::SPSCQueue<int, 4> queue;
    std::promise<bool> completion;
    auto result = completion.get_future();
    std::jthread consumer([&](std::stop_token stop) {
        int output = 99;
        const bool popped = queue.PopWaitFor(output, 10s, stop);
        completion.set_value(!popped && output == 99);
    });
    consumer.request_stop();
    const auto status = result.wait_for(2s);
    consumer.join();
    CHECK(status == std::future_status::ready);
    CHECK(result.get());

    REQUIRE(queue.TryEmplace(37));
    int output = -1;
    CHECK_FALSE(queue.PopWaitFor(output, 1s, consumer.get_stop_token()));
    CHECK(output == -1);
    CHECK(queue.TryPop(output));
    CHECK(output == 37);
}

TEST_CASE("Timed queue pops release capacity for a blocked producer", "[common][queue_wait]") {
    Common::SPSCQueue<int, 2> queue;
    REQUIRE(queue.TryEmplace(1));
    REQUIRE(queue.TryEmplace(2));
    std::jthread producer([&] { queue.EmplaceWait(3); });
    int output = 0;
    CHECK(queue.PopWaitFor(output, 1s));
    CHECK(output == 1);
    CHECK(queue.PopWaitFor(output, 1s));
    CHECK(output == 2);
    CHECK(queue.PopWaitFor(output, 1s));
    CHECK(output == 3);
    producer.join();
}

TEST_CASE("Timed queue pop supports move-only commands", "[common][queue_wait]") {
    Common::SPSCQueue<std::unique_ptr<int>, 2> queue;
    REQUIRE(queue.TryEmplace(std::make_unique<int>(51)));
    std::unique_ptr<int> output;
    REQUIRE(queue.PopWaitFor(output, 1s));
    REQUIRE(output);
    CHECK(*output == 51);
}
