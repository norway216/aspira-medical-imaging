/**
 * @file test_memory_pool.cpp
 * @brief Unit tests for memory pool and frame pool (opaque API only)
 */

#include <aspira/core/core.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

TEST_CASE("Memory pool basic operations", "[core][memory_pool]") {
    aspira_memory_pool* pool = aspira_memory_pool_create(128, 64);
    REQUIRE(pool != nullptr);

    SECTION("allocate and free single block") {
        void* ptr = aspira_memory_pool_alloc(pool);
        REQUIRE(ptr != nullptr);
        REQUIRE(aspira_memory_pool_free_count(pool) == 63);
        REQUIRE(aspira_memory_pool_allocated_count(pool) == 1);

        aspira_memory_pool_free(pool, ptr);
        REQUIRE(aspira_memory_pool_free_count(pool) == 64);
    }

    SECTION("allocate all blocks") {
        std::vector<void*> ptrs;
        for (size_t i = 0; i < 64; i++) {
            void* ptr = aspira_memory_pool_alloc(pool);
            REQUIRE(ptr != nullptr);
            ptrs.push_back(ptr);
        }

        REQUIRE(aspira_memory_pool_alloc(pool) == nullptr);
        REQUIRE(aspira_memory_pool_free_count(pool) == 0);

        for (auto* ptr : ptrs) {
            aspira_memory_pool_free(pool, ptr);
        }
        REQUIRE(aspira_memory_pool_free_count(pool) == 64);
    }

    SECTION("blocks are writable") {
        int* ptr = (int*)aspira_memory_pool_alloc(pool);
        REQUIRE(ptr != nullptr);
        for (int i = 0; i < 50; i++) ptr[i] = i;
        for (int i = 0; i < 50; i++) REQUIRE(ptr[i] == i);
        aspira_memory_pool_free(pool, ptr);
    }

    aspira_memory_pool_free_ptr(pool);
}

TEST_CASE("Memory pool multi-threaded alloc/free", "[core][memory_pool][stress]") {
    aspira_memory_pool* pool = aspira_memory_pool_create(128, 1024);
    REQUIRE(pool != nullptr);

    const int kOperations = 10000;
    std::atomic<size_t> alloc_count{0};
    std::atomic<size_t> free_count{0};

    auto worker = [&]() {
        std::vector<void*> local_ptrs;
        for (int i = 0; i < kOperations; i++) {
            if (i % 3 == 0 && !local_ptrs.empty()) {
                aspira_memory_pool_free(pool, local_ptrs.back());
                local_ptrs.pop_back();
                free_count.fetch_add(1);
            } else {
                void* ptr = aspira_memory_pool_alloc(pool);
                if (ptr) {
                    local_ptrs.push_back(ptr);
                    alloc_count.fetch_add(1);
                }
            }
        }
        for (auto* ptr : local_ptrs) {
            aspira_memory_pool_free(pool, ptr);
            free_count.fetch_add(1);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    REQUIRE(alloc_count.load() == free_count.load());
    REQUIRE(aspira_memory_pool_free_count(pool) == 1024);

    aspira_memory_pool_free_ptr(pool);
}

TEST_CASE("Frame pool operations", "[core][memory_pool][frame_pool]") {
    aspira_frame_pool* pool = aspira_frame_pool_create(16, 128, 256, 1);
    REQUIRE(pool != nullptr);

    SECTION("allocate frame") {
        aspira_frame* frame = aspira_frame_pool_alloc_frame(pool);
        REQUIRE(frame != nullptr);
        REQUIRE(frame->width == 128);
        REQUIRE(frame->height == 256);
        REQUIRE(frame->channels == 1);
        REQUIRE(frame->data != nullptr);

        for (size_t i = 0; i < 128 * 256; i++) frame->data[i] = (float)i;
        REQUIRE(frame->data[0] == 0.0f);
        REQUIRE(frame->data[100] == 100.0f);

        aspira_frame_pool_free_frame(pool, frame);
        REQUIRE(aspira_frame_pool_free_count(pool) == 16);
    }

    SECTION("exhaust frame pool") {
        std::vector<aspira_frame*> frames;
        for (size_t i = 0; i < 16; i++) frames.push_back(aspira_frame_pool_alloc_frame(pool));
        REQUIRE(aspira_frame_pool_alloc_frame(pool) == nullptr);
        for (auto* f : frames) aspira_frame_pool_free_frame(pool, f);
    }

    aspira_frame_pool_free_ptr(pool);
}
