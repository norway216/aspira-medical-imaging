/**
 * @file test_thread_pool.cpp
 * @brief Unit tests for priority thread pool (opaque API)
 */

#include <aspira/core/core.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

static void increment_task(void* arg) {
    auto* counter = static_cast<std::atomic<int>*>(arg);
    counter->fetch_add(1);
}

TEST_CASE("Thread pool basic operations", "[core][thread_pool]") {
    aspira_thread_pool* tp = aspira_thread_pool_create(4, 256);
    REQUIRE(tp != nullptr);

    SECTION("enqueue and execute single task") {
        std::atomic<int> counter{0};
        REQUIRE(aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_PROCESSING,
                                            increment_task, &counter));
        aspira_thread_pool_wait(tp);
        REQUIRE(counter.load() == 1);
    }

    SECTION("enqueue many tasks") {
        std::atomic<int> counter{0};
        const int kNumTasks = 1000;
        for (int i = 0; i < kNumTasks; i++) {
            while (!aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_PROCESSING,
                                               increment_task, &counter)) {
                std::this_thread::yield();
            }
        }
        aspira_thread_pool_wait(tp);
        REQUIRE(counter.load() == kNumTasks);
    }

    SECTION("priority ordering") {
        std::atomic<int> high_count{0}, normal_count{0}, low_count{0};
        for (int i = 0; i < 100; i++) {
            aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_RENDERING,
                                        increment_task, &low_count);
            aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_PROCESSING,
                                        increment_task, &normal_count);
            aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_ACQUISITION,
                                        increment_task, &high_count);
        }
        aspira_thread_pool_wait(tp);
        REQUIRE(high_count.load() == 100);
        REQUIRE(normal_count.load() == 100);
        REQUIRE(low_count.load() == 100);
    }

    aspira_thread_pool_free_ptr(tp);
}

TEST_CASE("Thread pool CPU affinity", "[core][thread_pool][affinity]") {
    aspira_thread_pool* tp = aspira_thread_pool_create(2, 256);
    REQUIRE(tp != nullptr);

    REQUIRE(aspira_thread_pool_set_affinity(tp, 0, 0));
    REQUIRE(aspira_thread_pool_set_affinity(tp, 1, 1));

    std::atomic<int> counter{0};
    for (int i = 0; i < 100; i++) {
        while (!aspira_thread_pool_enqueue(tp, ASPIRA_PRIORITY_PROCESSING,
                                            increment_task, &counter)) {
            std::this_thread::yield();
        }
    }
    aspira_thread_pool_wait(tp);
    REQUIRE(counter.load() == 100);

    aspira_thread_pool_free_ptr(tp);
}
