// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "common/bounded_threadsafe_queue.h"

namespace {
// Moving deliberately retains ownership in the source, as valid resource-owning
// types may do. Every constructed instance still requires its own destructor.
struct Tracked {
    static inline std::atomic<int> live{};
    int value;
    explicit Tracked(int value_) : value{value_} { ++live; }
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&& other) noexcept : value{other.value} { ++live; }
    Tracked& operator=(Tracked&& other) noexcept { value = other.value; return *this; }
    ~Tracked() { --live; }
};
}

TEST_CASE("Queue constructs only occupied slots and destroys popped and pending values",
          "[common][queue_lifetime]") {
    REQUIRE(Tracked::live == 0);
    {
        Tracked output{-1};
        {
            Common::SPSCQueue<Tracked, 2> queue;
            CHECK(Tracked::live == 1);
            for (int i = 0; i < 100; ++i) {
                REQUIRE(queue.TryEmplace(i));
                CHECK(Tracked::live == 2);
                REQUIRE(queue.TryPop(output));
                CHECK(output.value == i);
                CHECK(Tracked::live == 1);
            }
            REQUIRE(queue.TryEmplace(101));
            REQUIRE(queue.TryEmplace(102));
            CHECK_FALSE(queue.TryEmplace(103));
            CHECK(Tracked::live == 3);
        }
        CHECK(Tracked::live == 1);
    }
    CHECK(Tracked::live == 0);
}

TEST_CASE("Queue destroys a slot before another thread reuses it",
          "[common][queue_lifetime]") {
    REQUIRE(Tracked::live == 0);
    {
        Common::SPSCQueue<Tracked, 4> queue;
        Tracked output{-1};
        std::jthread producer([&] {
            for (int i = 0; i < 10000; ++i) queue.EmplaceWait(i);
        });
        bool ordered = true;
        for (int i = 0; i < 10000; ++i) {
            queue.PopWait(output);
            ordered &= output.value == i;
        }
        producer.join();
        CHECK(ordered);
        CHECK(Tracked::live == 1);
    }
    CHECK(Tracked::live == 0);
}
