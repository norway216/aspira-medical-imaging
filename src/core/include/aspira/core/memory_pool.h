/**
 * @file memory_pool.h
 * @brief Lock-free memory pool and frame pool for pre-allocated buffers
 *
 * The memory pool uses a lock-free free list (CAS-based stack) for O(1)
 * allocation and deallocation. All memory is pre-allocated at pool creation
 * to avoid heap fragmentation in real-time paths.
 *
 * The frame pool is a specialized memory pool that manages aspira_frame
 * structures along with their data buffers, providing 1:1 frame-to-block
 * mapping for zero-overhead frame lifecycle management.
 */

#ifndef ASPIRA_MEMORY_POOL_H
#define ASPIRA_MEMORY_POOL_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"

/* Cache line size (must match core.h) */
#ifndef ASPIRA_CACHE_LINE_SIZE
#define ASPIRA_CACHE_LINE_SIZE 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Generic Memory Pool (Slab Allocator)
 * ========================================================================== */

/**
 * @brief Lock-free memory pool with fixed-size blocks
 *
 * All blocks are the same size. Allocation/deallocation is O(1) and
 * lock-free via a CAS-based free list stack.
 *
 * Thread safety: Multiple threads can alloc/free concurrently.
 */
typedef struct aspira_memory_pool {
    uint8_t*    memory;           /* Contiguous mmap'd region */
    size_t      block_size;       /* Size of each block (padded to cache line) */
    size_t      num_blocks;       /* Total blocks in the pool */
    size_t      total_size;       /* Total memory size in bytes */

    /* Lock-free free list: stack of free block indices */
    alignas(64) atomic_uint_least64_t stack_top;  /* Index of top free block */
    uint64_t*   next_indices;     /* Array: next[i] = next free index after i */
    bool        owns_memory;      /* Whether we should free() the memory */
} aspira_memory_pool;

/**
 * @brief Initialize a memory pool
 * @param pool Pointer to uninitialized pool
 * @param block_size Size of each block in bytes (will be aligned)
 * @param num_blocks Total number of blocks
 * @return true on success
 */
bool aspira_memory_pool_init(aspira_memory_pool* pool, size_t block_size,
                              size_t num_blocks);

/**
 * @brief Initialize a NUMA-aware memory pool (allocates on specific NUMA node)
 * @param pool Pointer to uninitialized pool
 * @param block_size Size of each block
 * @param num_blocks Total number of blocks
 * @param numa_node NUMA node ID (-1 for default)
 * @return true on success
 */
bool aspira_memory_pool_init_numa(aspira_memory_pool* pool, size_t block_size,
                                   size_t num_blocks, int numa_node);

/**
 * @brief Destroy memory pool and free all resources
 */
void aspira_memory_pool_destroy(aspira_memory_pool* pool);

/**
 * @brief Allocate a block from the pool
 * @return Pointer to block, or NULL if pool exhausted
 */
void* aspira_memory_pool_alloc(aspira_memory_pool* pool);

/**
 * @brief Return a block to the pool
 * @param ptr Pointer previously returned by alloc()
 */
void aspira_memory_pool_free(aspira_memory_pool* pool, void* ptr);

/**
 * @brief Get number of free blocks remaining
 */
size_t aspira_memory_pool_free_count(const aspira_memory_pool* pool);

/**
 * @brief Get number of allocated blocks
 */
size_t aspira_memory_pool_allocated_count(const aspira_memory_pool* pool);

/**
 * @brief Get block size
 */
size_t aspira_memory_pool_block_size(const aspira_memory_pool* pool);

/* ==========================================================================
 * Frame Pool (specialized pool for aspira_frame)
 * ========================================================================== */

/**
 * @brief Frame pool - manages frame structures and their data buffers
 *
 * Each frame in the pool has its data buffer allocated from the same
 * memory region, enabling efficient frame lifecycle management.
 */
typedef struct aspira_frame_pool {
    aspira_memory_pool meta_pool;   /* Pool for aspira_frame structs */
    aspira_memory_pool data_pool;   /* Pool for frame data buffers */
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t frame_channels;
    size_t   frame_data_size;       /* Size of one frame's data buffer */
} aspira_frame_pool;

/**
 * @brief Initialize a frame pool
 * @param pool Pointer to uninitialized frame pool
 * @param num_frames Maximum number of frames in the pool
 * @param frame_width Width in pixels
 * @param frame_height Height in pixels
 * @param frame_channels Number of channels (1 or 3)
 * @return true on success
 */
bool aspira_frame_pool_init(aspira_frame_pool* pool, size_t num_frames,
                             uint32_t frame_width, uint32_t frame_height,
                             uint32_t frame_channels);

/**
 * @brief Destroy frame pool
 */
void aspira_frame_pool_destroy(aspira_frame_pool* pool);

/**
 * @brief Allocate a frame from the pool
 * @return Fully initialized aspira_frame*, or NULL if exhausted
 */
aspira_frame* aspira_frame_pool_alloc_frame(aspira_frame_pool* pool);

/**
 * @brief Return a frame to the pool
 */
void aspira_frame_pool_free_frame(aspira_frame_pool* pool, aspira_frame* frame);

/**
 * @brief Get number of free frames
 */
size_t aspira_frame_pool_free_count(const aspira_frame_pool* pool);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_MEMORY_POOL_H */
