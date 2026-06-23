/**
 * @file memory_pool.c
 * @brief Lock-free memory pool and frame pool implementation
 *
 * Uses a CAS-based lock-free stack for the free list. Each free block
 * stores the index of the next free block in its first 8 bytes, forming
 * a singly-linked list of free indices.
 */

#include "aspira/core/memory_pool.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef ASPIRA_HAS_NUMA
#include <numa.h>
#endif

/* Round up to multiple of alignment */
static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/* ==========================================================================
 * Generic Memory Pool
 * ========================================================================== */

bool aspira_memory_pool_init(aspira_memory_pool* pool, size_t block_size,
                              size_t num_blocks) {
    return aspira_memory_pool_init_numa(pool, block_size, num_blocks, -1);
}

bool aspira_memory_pool_init_numa(aspira_memory_pool* pool, size_t block_size,
                                   size_t num_blocks, int numa_node) {
    if (!pool || block_size == 0 || num_blocks == 0) {
        return false;
    }

    /* Align block size to cache line for false-sharing prevention */
    block_size = align_up(block_size, ASPIRA_CACHE_LINE_SIZE);
    size_t total_size = block_size * num_blocks;

    /* Allocate memory */
    void* mem = NULL;
#ifdef ASPIRA_HAS_NUMA
    if (numa_node >= 0 && numa_node < numa_max_node()) {
        mem = numa_alloc_onnode(total_size, numa_node);
        if (!mem) goto fallback;
    } else {
        mem = aligned_alloc(ASPIRA_CACHE_LINE_SIZE, total_size);
    }
fallback:
#else
    (void)numa_node;
    mem = aligned_alloc(ASPIRA_CACHE_LINE_SIZE, total_size);
#endif
    if (!mem) {
        return false;
    }

    /* Allocate free index array */
    uint64_t* next_indices = (uint64_t*)calloc(num_blocks, sizeof(uint64_t));
    if (!next_indices) {
#ifdef ASPIRA_HAS_NUMA
        if (numa_node >= 0) {
            numa_free(mem, total_size);
        } else {
            free(mem);
        }
#else
        free(mem);
#endif
        return false;
    }

    /* Initialize free list stack: blocks 0 -> 1 -> 2 -> ... -> N-1 -> sentinel */
    for (size_t i = 0; i < num_blocks - 1; i++) {
        next_indices[i] = i + 1;
    }
    next_indices[num_blocks - 1] = (uint64_t)-1;  /* Sentinel: end of list */

    pool->memory = (uint8_t*)mem;
    pool->block_size = block_size;
    pool->num_blocks = num_blocks;
    pool->total_size = total_size;
    pool->next_indices = next_indices;
    pool->owns_memory = true;

    /* Stack top points to first free block (index 0) */
    atomic_init(&pool->stack_top, 0);

    return true;
}

void aspira_memory_pool_destroy(aspira_memory_pool* pool) {
    if (!pool) return;

    free(pool->next_indices);
    pool->next_indices = NULL;

    if (pool->owns_memory && pool->memory) {
        free(pool->memory);
        pool->memory = NULL;
    }
    pool->num_blocks = 0;
    pool->block_size = 0;
}

void* aspira_memory_pool_alloc(aspira_memory_pool* pool) {
    if (!pool) return NULL;

    uint64_t top;

    for (;;) {
        top = atomic_load_explicit(&pool->stack_top, memory_order_acquire);
        if (top == (uint64_t)-1) {
            /* Pool exhausted */
            return NULL;
        }

        /* Read next pointer before CAS */
        uint64_t next = pool->next_indices[top];

        /* Try to pop 'top' from the stack */
        if (atomic_compare_exchange_weak_explicit(
                &pool->stack_top, &top, next,
                memory_order_release, memory_order_relaxed)) {
            /* Successfully claimed block at index 'top' */
            pool->next_indices[top] = (uint64_t)-1;  /* Mark as allocated */
            return pool->memory + top * pool->block_size;
        }
        /* CAS failed, another thread popped first — retry */
    }
}

void aspira_memory_pool_free(aspira_memory_pool* pool, void* ptr) {
    if (!pool || !ptr) return;

    /* Calculate block index from pointer */
    uint64_t index = (uint64_t)((uint8_t*)ptr - pool->memory) / pool->block_size;
    if (index >= pool->num_blocks) {
        return;  /* Not our pointer */
    }

    uint64_t old_top;

    for (;;) {
        old_top = atomic_load_explicit(&pool->stack_top, memory_order_acquire);
        pool->next_indices[index] = old_top;

        if (atomic_compare_exchange_weak_explicit(
                &pool->stack_top, &old_top, index,
                memory_order_release, memory_order_relaxed)) {
            break;  /* Successfully pushed back */
        }
    }
}

size_t aspira_memory_pool_free_count(const aspira_memory_pool* pool) {
    if (!pool) return 0;

    /* Count elements in the free list */
    uint64_t top = atomic_load_explicit(&pool->stack_top, memory_order_acquire);
    size_t count = 0;
    while (top != (uint64_t)-1 && count < pool->num_blocks) {
        top = pool->next_indices[top];
        count++;
    }
    return count;
}

size_t aspira_memory_pool_allocated_count(const aspira_memory_pool* pool) {
    if (!pool) return 0;
    return pool->num_blocks - aspira_memory_pool_free_count(pool);
}

size_t aspira_memory_pool_block_size(const aspira_memory_pool* pool) {
    if (!pool) return 0;
    return pool->block_size;
}

/* ==========================================================================
 * Frame Pool
 * ========================================================================== */

bool aspira_frame_pool_init(aspira_frame_pool* pool, size_t num_frames,
                             uint32_t frame_width, uint32_t frame_height,
                             uint32_t frame_channels) {
    if (!pool || num_frames == 0) return false;

    pool->frame_width = frame_width;
    pool->frame_height = frame_height;
    pool->frame_channels = frame_channels;
    pool->frame_data_size = (size_t)frame_width * frame_height *
                            frame_channels * sizeof(float);

    /* Pool for frame metadata */
    if (!aspira_memory_pool_init(&pool->meta_pool, sizeof(aspira_frame),
                                  num_frames)) {
        return false;
    }

    /* Pool for frame data */
    if (!aspira_memory_pool_init(&pool->data_pool, pool->frame_data_size,
                                  num_frames)) {
        aspira_memory_pool_destroy(&pool->meta_pool);
        return false;
    }

    return true;
}

void aspira_frame_pool_destroy(aspira_frame_pool* pool) {
    if (!pool) return;
    aspira_memory_pool_destroy(&pool->meta_pool);
    aspira_memory_pool_destroy(&pool->data_pool);
}

aspira_frame* aspira_frame_pool_alloc_frame(aspira_frame_pool* pool) {
    if (!pool) return NULL;

    aspira_frame* frame = (aspira_frame*)aspira_memory_pool_alloc(&pool->meta_pool);
    if (!frame) return NULL;

    float* data = (float*)aspira_memory_pool_alloc(&pool->data_pool);
    if (!data) {
        aspira_memory_pool_free(&pool->meta_pool, frame);
        return NULL;
    }

    /* Initialize frame */
    aspira_frame_init(frame, pool->frame_width, pool->frame_height,
                      pool->frame_channels, data, pool->frame_data_size);
    frame->_internal = pool;  /* Store pool pointer for free() */

    return frame;
}

void aspira_frame_pool_free_frame(aspira_frame_pool* pool, aspira_frame* frame) {
    if (!pool || !frame) return;

    if (frame->data) {
        aspira_memory_pool_free(&pool->data_pool, frame->data);
        frame->data = NULL;
    }
    aspira_memory_pool_free(&pool->meta_pool, frame);
}

size_t aspira_frame_pool_free_count(const aspira_frame_pool* pool) {
    if (!pool) return 0;
    size_t meta_free = aspira_memory_pool_free_count(&pool->meta_pool);
    size_t data_free = aspira_memory_pool_free_count(&pool->data_pool);
    return meta_free < data_free ? meta_free : data_free;
}
