/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for SPSC and MPMC ring buffers (opaque API)
 */

#include <aspira/core/core.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

TEST_CASE("SPSC ring buffer basic operations", "[core][ring_buffer][spsc]") {
    aspira_spsc_rb* rb = aspira_spsc_create(64, sizeof(int));
    REQUIRE(rb != nullptr);

    SECTION("empty on creation") {
        REQUIRE(aspira_spsc_empty(rb));
        REQUIRE_FALSE(aspira_spsc_full(rb));
        REQUIRE(aspira_spsc_count(rb) == 0);
    }

    SECTION("push and pop single element") {
        int val = 42;
        REQUIRE(aspira_spsc_push(rb, &val));
        REQUIRE(aspira_spsc_count(rb) == 1);
        REQUIRE_FALSE(aspira_spsc_empty(rb));

        int out = 0;
        REQUIRE(aspira_spsc_pop(rb, &out));
        REQUIRE(out == 42);
        REQUIRE(aspira_spsc_empty(rb));
    }

    SECTION("push until full") {
        int val = 1;
        for (uint64_t i = 0; i < 64; i++) {
            REQUIRE(aspira_spsc_push(rb, &val));
        }
        REQUIRE(aspira_spsc_full(rb));
        REQUIRE_FALSE(aspira_spsc_push(rb, &val));
    }

    SECTION("zero-copy front/pop_front") {
        int val = 99;
        aspira_spsc_push(rb, &val);
        const void* front = aspira_spsc_front(rb);
        REQUIRE(front != nullptr);
        REQUIRE(*(const int*)front == 99);
        aspira_spsc_pop_front(rb);
        REQUIRE(aspira_spsc_empty(rb));
    }

    aspira_spsc_free(rb);
}

TEST_CASE("SPSC ring buffer multi-threaded stress", "[core][ring_buffer][spsc][stress]") {
    aspira_spsc_rb* rb = aspira_spsc_create(1024, sizeof(int));
    REQUIRE(rb != nullptr);

    const int kNumItems = 1000000;
    std::atomic<int> produced{0}, consumed{0}, sum_produced{0}, sum_consumed{0};
    std::atomic<bool> done{false};

    std::thread producer([&]() {
        for (int i = 1; i <= kNumItems; i++) {
            while (!aspira_spsc_push(rb, &i)) {}
            sum_produced.fetch_add(i);
            produced.fetch_add(1);
        }
        done = true;
    });

    std::thread consumer([&]() {
        int val;
        while (!done || !aspira_spsc_empty(rb)) {
            if (aspira_spsc_pop(rb, &val)) {
                sum_consumed.fetch_add(val);
                consumed.fetch_add(1);
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(produced.load() == kNumItems);
    REQUIRE(consumed.load() == kNumItems);
    REQUIRE(sum_produced.load() == sum_consumed.load());
    REQUIRE(aspira_spsc_empty(rb));

    aspira_spsc_free(rb);
}

TEST_CASE("MPMC ring buffer basic operations", "[core][ring_buffer][mpmc]") {
    aspira_mpmc_rb* rb = aspira_mpmc_create(64, sizeof(int));
    REQUIRE(rb != nullptr);

    SECTION("empty on creation") {
        REQUIRE(aspira_mpmc_empty(rb));
        REQUIRE_FALSE(aspira_mpmc_full(rb));
    }

    SECTION("enqueue and dequeue single") {
        int val = 42;
        REQUIRE(aspira_mpmc_enqueue(rb, &val));
        int out = 0;
        REQUIRE(aspira_mpmc_dequeue(rb, &out));
        REQUIRE(out == 42);
    }

    SECTION("enqueue until full") {
        int val = 1;
        for (uint64_t i = 0; i < 64; i++) {
            REQUIRE(aspira_mpmc_enqueue(rb, &val));
        }
        REQUIRE_FALSE(aspira_mpmc_enqueue(rb, &val));
    }

    aspira_mpmc_free(rb);
}

TEST_CASE("MPMC ring buffer multi-threaded", "[core][ring_buffer][mpmc][stress]") {
    aspira_mpmc_rb* rb = aspira_mpmc_create(1024, sizeof(int));
    REQUIRE(rb != nullptr);

    const int kNumPerThread = 100000;
    std::atomic<int> total_enqueued{0}, total_dequeued{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < 4; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kNumPerThread; i++) {
                int val = (p << 24) | i;
                while (!aspira_mpmc_enqueue(rb, &val)) {}
                total_enqueued.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < 4; c++) {
        consumers.emplace_back([&]() {
            int val;
            while (total_dequeued.load() < kNumPerThread * 4) {
                if (aspira_mpmc_dequeue(rb, &val)) {
                    total_dequeued.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    REQUIRE(total_enqueued.load() == kNumPerThread * 4);
    REQUIRE(total_dequeued.load() == kNumPerThread * 4);
    REQUIRE(aspira_mpmc_empty(rb));

    aspira_mpmc_free(rb);
}
