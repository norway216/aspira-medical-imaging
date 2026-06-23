/**
 * @file ipc.h
 * @brief Inter-Process Communication via POSIX shared memory and message queues
 *
 * Two IPC patterns are supported:
 *
 * 1. Shared Memory Ring Buffer (Data Plane):
 *    - POSIX shm_open + mmap for zero-copy frame data transfer
 *    - Ring buffer stored inside the shared memory region
 *    - Offset-based frame addressing (safe across different address spaces)
 *
 * 2. POSIX Message Queue (Control Plane):
 *    - Small control messages for synchronization
 *    - Frame-ready notifications, start/stop commands
 */

#ifndef ASPIRA_IPC_H
#define ASPIRA_IPC_H

#include <mqueue.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cache line size (must match core.h) */
#ifndef ASPIRA_CACHE_LINE_SIZE
#define ASPIRA_CACHE_LINE_SIZE 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Shared Memory Channel
 * ========================================================================== */

#define ASPIRA_IPC_MAX_NAME_LEN   64
#define ASPIRA_IPC_MAX_FRAMES     128

/**
 * @brief IPC frame descriptor (stored in shared memory ring buffer)
 *
 * Uses offset-based addressing: the data_offset is relative to the
 * shared memory base, making it safe across different process address
 * spaces.
 */
typedef struct {
    uint64_t    frame_id;
    uint64_t    timestamp_ns;
    uint32_t    width;
    uint32_t    height;
    uint32_t    channels;
    uint32_t    data_offset;    /* Offset from shm base to frame data */
    size_t      data_size;
    float       gain;
    float       dyn_range_db;
    uint8_t     flags;
    uint8_t     _pad[3];
} aspira_ipc_frame_desc;

/**
 * @brief Shared memory IPC channel header (at the start of shm region)
 */
typedef struct {
    uint64_t    magic;              /* Magic number for validation */
    uint64_t    version;
    size_t      total_size;         /* Total shm size */
    size_t      frame_pool_offset;  /* Offset to frame data pool */
    size_t      frame_pool_size;    /* Total size of frame data pool */
    size_t      frame_data_size;    /* Size of each frame's data */
    uint32_t    max_frames;
    uint32_t    frame_width;
    uint32_t    frame_height;
    uint32_t    frame_channels;

    /* Lock-free ring buffer for frame descriptors (inside shm) */
    uint64_t    ring_buffer_offset; /* Offset to ring buffer metadata */
    uint64_t    ring_buffer_capacity;
    atomic_uint_least64_t rb_head;  /* Producer index */
    atomic_uint_least64_t rb_tail;  /* Consumer index */

    /* Statistics */
    atomic_uint_least64_t frames_sent;
    atomic_uint_least64_t frames_received;
    atomic_uint_least64_t frames_dropped;
} aspira_ipc_shm_header;

/**
 * @brief IPC channel (combines shared memory + message queue)
 */
typedef struct {
    char        name[ASPIRA_IPC_MAX_NAME_LEN];   /* Channel name */
    char        shm_name[ASPIRA_IPC_MAX_NAME_LEN]; /* POSIX shm name */
    char        mq_name[ASPIRA_IPC_MAX_NAME_LEN];  /* POSIX mq name */

    int         shm_fd;          /* Shared memory file descriptor */
    void*       shm_ptr;         /* mmap'd base address */
    size_t      shm_size;        /* Total shared memory size */

    aspira_ipc_shm_header* header;  /* Pointer to header in shm */
    uint8_t*    frame_pool;         /* Pointer to frame data pool in shm */

    bool        is_producer;     /* True if this end produces frames */

    /* Control message queue */
    mqd_t       mq;              /* POSIX message queue descriptor */
} aspira_ipc_channel;

/* Control message types */
typedef enum {
    ASPIRA_IPC_MSG_FRAME_READY = 1,  /* New frame(s) available */
    ASPIRA_IPC_MSG_START       = 2,  /* Start acquisition */
    ASPIRA_IPC_MSG_STOP        = 3,  /* Stop acquisition */
    ASPIRA_IPC_MSG_SHUTDOWN    = 4,  /* Graceful shutdown */
    ASPIRA_IPC_MSG_ACK         = 5,  /* Acknowledgment */
    ASPIRA_IPC_MSG_ERROR       = 6,  /* Error notification */
} aspira_ipc_msg_type_t;

typedef struct {
    aspira_ipc_msg_type_t type;
    uint64_t    frame_id;
    uint64_t    timestamp_ns;
    int32_t     error_code;
    char        description[64];
} aspira_ipc_control_msg;

/* ==========================================================================
 * IPC API
 * ========================================================================== */

/**
 * @brief Create a producer-side IPC channel (creates shm + mq)
 * @param channel Pointer to uninitialized channel
 * @param name Unique name for this channel
 * @param max_frames Maximum frames in the shared pool
 * @param frame_width Frame width
 * @param frame_height Frame height
 * @param frame_channels Number of channels
 * @return true on success
 */
bool aspira_ipc_producer_create(aspira_ipc_channel* channel,
                                 const char* name,
                                 uint32_t max_frames,
                                 uint32_t frame_width,
                                 uint32_t frame_height,
                                 uint32_t frame_channels);

/**
 * @brief Open a consumer-side IPC channel (opens existing shm + mq)
 * @return true on success
 */
bool aspira_ipc_consumer_open(aspira_ipc_channel* channel,
                               const char* name);

/**
 * @brief Close and cleanup IPC channel (producer side: unlinks shm + mq)
 */
void aspira_ipc_producer_destroy(aspira_ipc_channel* channel);

/**
 * @brief Close IPC channel (consumer side: just closes, no unlink)
 */
void aspira_ipc_consumer_close(aspira_ipc_channel* channel);

/**
 * @brief Write a frame to the shared memory ring buffer (producer)
 * @param desc Frame descriptor (data is copied into shm pool)
 * @param data Raw frame data to copy
 * @return true on success
 */
bool aspira_ipc_send_frame(aspira_ipc_channel* channel,
                            const aspira_ipc_frame_desc* desc,
                            const float* data);

/**
 * @brief Read a frame from the shared memory ring buffer (consumer)
 * @param desc Output frame descriptor
 * @param data Output buffer for frame data (must be at least frame_data_size)
 * @return true on success
 */
bool aspira_ipc_recv_frame(aspira_ipc_channel* channel,
                            aspira_ipc_frame_desc* desc,
                            float* data);

/**
 * @brief Send a control message via message queue
 */
bool aspira_ipc_send_control(aspira_ipc_channel* channel,
                              const aspira_ipc_control_msg* msg);

/**
 * @brief Receive a control message (non-blocking)
 * @return true if message received, false if no message available
 */
bool aspira_ipc_recv_control(aspira_ipc_channel* channel,
                              aspira_ipc_control_msg* msg);

/**
 * @brief Receive a control message (blocking with timeout)
 * @param timeout_ms Timeout in milliseconds, 0 = block forever
 * @return true if message received
 */
bool aspira_ipc_recv_control_timed(aspira_ipc_channel* channel,
                                    aspira_ipc_control_msg* msg,
                                    uint64_t timeout_ms);

/**
 * @brief Get number of frames available in the shared ring buffer (consumer)
 */
uint64_t aspira_ipc_available_frames(const aspira_ipc_channel* channel);

/**
 * @brief Get IPC statistics from the shared header
 */
void aspira_ipc_get_stats(const aspira_ipc_channel* channel,
                           uint64_t* frames_sent,
                           uint64_t* frames_received,
                           uint64_t* frames_dropped);

#ifdef __cplusplus
}
#endif

#endif /* ASPIRA_IPC_H */
