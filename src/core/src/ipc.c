/**
 * @file ipc.c
 * @brief Inter-Process Communication via POSIX shared memory and message queues
 */

#define _POSIX_C_SOURCE 199309L

#include "aspira/core/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Magic number for shared memory validation */
#define ASPIRA_IPC_MAGIC  0x4153504952415049UL  /* "ASPIRAPI" */
#define ASPIRA_IPC_VERSION 1

/* ==========================================================================
 * Shared Memory Channel
 * ========================================================================== */

static size_t calculate_shm_size(uint32_t max_frames, uint32_t width,
                                  uint32_t height, uint32_t channels) {
    size_t header_size = sizeof(aspira_ipc_shm_header);
    header_size = (header_size + ASPIRA_CACHE_LINE_SIZE - 1) &
                  ~(ASPIRA_CACHE_LINE_SIZE - 1);

    size_t desc_slots = max_frames * sizeof(aspira_ipc_frame_desc);
    desc_slots = (desc_slots + ASPIRA_CACHE_LINE_SIZE - 1) &
                 ~(ASPIRA_CACHE_LINE_SIZE - 1);

    size_t frame_data = (size_t)max_frames * width * height * channels * sizeof(float);

    return header_size + desc_slots + frame_data;
}

bool aspira_ipc_producer_create(aspira_ipc_channel* channel,
                                 const char* name,
                                 uint32_t max_frames,
                                 uint32_t frame_width,
                                 uint32_t frame_height,
                                 uint32_t frame_channels) {
    if (!channel || !name || max_frames == 0) return false;

    memset(channel, 0, sizeof(*channel));
    strncpy(channel->name, name, sizeof(channel->name) - 1);

    /* Format POSIX object names */
    snprintf(channel->shm_name, sizeof(channel->shm_name), "/aspira_shm_%s", name);
    snprintf(channel->mq_name, sizeof(channel->mq_name), "/aspira_mq_%s", name);

    size_t shm_size = calculate_shm_size(max_frames, frame_width,
                                          frame_height, frame_channels);
    channel->shm_size = shm_size;
    channel->is_producer = true;

    /* Create shared memory */
    channel->shm_fd = shm_open(channel->shm_name,
                                O_CREAT | O_RDWR | O_EXCL, 0600);
    if (channel->shm_fd < 0) {
        /* Try opening existing (maybe from previous unclean shutdown) */
        shm_unlink(channel->shm_name);
        channel->shm_fd = shm_open(channel->shm_name,
                                    O_CREAT | O_RDWR | O_EXCL, 0600);
        if (channel->shm_fd < 0) {
            return false;
        }
    }

    if (ftruncate(channel->shm_fd, (off_t)shm_size) != 0) {
        close(channel->shm_fd);
        shm_unlink(channel->shm_name);
        return false;
    }

    /* Map shared memory */
    channel->shm_ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, channel->shm_fd, 0);
    if (channel->shm_ptr == MAP_FAILED) {
        close(channel->shm_fd);
        shm_unlink(channel->shm_name);
        return false;
    }

    /* Initialize shared memory header */
    memset(channel->shm_ptr, 0, shm_size);
    channel->header = (aspira_ipc_shm_header*)channel->shm_ptr;

    channel->header->magic = ASPIRA_IPC_MAGIC;
    channel->header->version = ASPIRA_IPC_VERSION;
    channel->header->total_size = shm_size;
    channel->header->max_frames = max_frames;
    channel->header->frame_width = frame_width;
    channel->header->frame_height = frame_height;
    channel->header->frame_channels = frame_channels;
    channel->header->frame_data_size = (size_t)frame_width * frame_height *
                                        frame_channels * sizeof(float);

    /* Calculate offsets */
    size_t header_size = sizeof(aspira_ipc_shm_header);
    header_size = (header_size + ASPIRA_CACHE_LINE_SIZE - 1) &
                  ~(ASPIRA_CACHE_LINE_SIZE - 1);

    channel->header->ring_buffer_offset = header_size;
    channel->header->ring_buffer_capacity = max_frames;

    channel->header->frame_pool_offset = header_size +
        max_frames * sizeof(aspira_ipc_frame_desc);
    channel->header->frame_pool_offset = (channel->header->frame_pool_offset +
        ASPIRA_CACHE_LINE_SIZE - 1) & ~(ASPIRA_CACHE_LINE_SIZE - 1);

    channel->header->frame_pool_size = (size_t)max_frames *
        channel->header->frame_data_size;

    atomic_init(&channel->header->rb_head, 0);
    atomic_init(&channel->header->rb_tail, 0);
    atomic_init(&channel->header->frames_sent, 0);
    atomic_init(&channel->header->frames_received, 0);
    atomic_init(&channel->header->frames_dropped, 0);

    channel->frame_pool = (uint8_t*)channel->shm_ptr +
                          channel->header->frame_pool_offset;

    /* Create message queue for control messages */
    struct mq_attr mq_attr;
    mq_attr.mq_flags = 0;
    mq_attr.mq_maxmsg = 16;
    mq_attr.mq_msgsize = sizeof(aspira_ipc_control_msg);
    mq_attr.mq_curmsgs = 0;

    channel->mq = mq_open(channel->mq_name,
                           O_CREAT | O_RDWR | O_NONBLOCK, 0600, &mq_attr);
    if (channel->mq == (mqd_t)-1) {
        munmap(channel->shm_ptr, shm_size);
        close(channel->shm_fd);
        shm_unlink(channel->shm_name);
        return false;
    }

    return true;
}

bool aspira_ipc_consumer_open(aspira_ipc_channel* channel,
                               const char* name) {
    if (!channel || !name) return false;

    memset(channel, 0, sizeof(*channel));
    strncpy(channel->name, name, sizeof(channel->name) - 1);
    snprintf(channel->shm_name, sizeof(channel->shm_name), "/aspira_shm_%s", name);
    snprintf(channel->mq_name, sizeof(channel->mq_name), "/aspira_mq_%s", name);

    channel->is_producer = false;

    /* Open existing shared memory */
    channel->shm_fd = shm_open(channel->shm_name, O_RDWR, 0600);
    if (channel->shm_fd < 0) {
        return false;
    }

    /* Get size */
    struct stat st;
    if (fstat(channel->shm_fd, &st) != 0) {
        close(channel->shm_fd);
        return false;
    }
    channel->shm_size = (size_t)st.st_size;

    /* Map shared memory */
    channel->shm_ptr = mmap(NULL, channel->shm_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, channel->shm_fd, 0);
    if (channel->shm_ptr == MAP_FAILED) {
        close(channel->shm_fd);
        return false;
    }

    channel->header = (aspira_ipc_shm_header*)channel->shm_ptr;

    /* Validate magic */
    if (channel->header->magic != ASPIRA_IPC_MAGIC) {
        munmap(channel->shm_ptr, channel->shm_size);
        close(channel->shm_fd);
        return false;
    }

    channel->frame_pool = (uint8_t*)channel->shm_ptr +
                          channel->header->frame_pool_offset;

    /* Open message queue */
    channel->mq = mq_open(channel->mq_name, O_RDWR | O_NONBLOCK);
    if (channel->mq == (mqd_t)-1) {
        munmap(channel->shm_ptr, channel->shm_size);
        close(channel->shm_fd);
        return false;
    }

    return true;
}

void aspira_ipc_producer_destroy(aspira_ipc_channel* channel) {
    if (!channel) return;

    if (channel->mq != (mqd_t)-1) {
        mq_close(channel->mq);
        mq_unlink(channel->mq_name);
    }

    if (channel->shm_ptr && channel->shm_ptr != MAP_FAILED) {
        munmap(channel->shm_ptr, channel->shm_size);
    }

    if (channel->shm_fd >= 0) {
        close(channel->shm_fd);
        shm_unlink(channel->shm_name);
    }

    memset(channel, 0, sizeof(*channel));
}

void aspira_ipc_consumer_close(aspira_ipc_channel* channel) {
    if (!channel) return;

    if (channel->mq != (mqd_t)-1) {
        mq_close(channel->mq);
    }

    if (channel->shm_ptr && channel->shm_ptr != MAP_FAILED) {
        munmap(channel->shm_ptr, channel->shm_size);
    }

    if (channel->shm_fd >= 0) {
        close(channel->shm_fd);
    }

    memset(channel, 0, sizeof(*channel));
}

/* ==========================================================================
 * Frame Transfer
 * ========================================================================== */

bool aspira_ipc_send_frame(aspira_ipc_channel* channel,
                            const aspira_ipc_frame_desc* desc,
                            const float* data) {
    if (!channel || !desc || !data) return false;

    aspira_ipc_shm_header* hdr = channel->header;
    uint64_t head = atomic_load_explicit(&hdr->rb_head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&hdr->rb_tail, memory_order_acquire);

    if (head - tail >= hdr->ring_buffer_capacity) {
        atomic_fetch_add_explicit(&hdr->frames_dropped, 1, memory_order_relaxed);
        return false;  /* Buffer full */
    }

    /* Get descriptor slot */
    size_t desc_offset = hdr->ring_buffer_offset +
                         (head & (hdr->ring_buffer_capacity - 1)) *
                         sizeof(aspira_ipc_frame_desc);
    aspira_ipc_frame_desc* slot = (aspira_ipc_frame_desc*)((uint8_t*)channel->shm_ptr + desc_offset);

    /* Get data slot */
    size_t data_offset = (size_t)(head & (hdr->ring_buffer_capacity - 1)) *
                         hdr->frame_data_size;
    uint8_t* data_slot = channel->frame_pool + data_offset;

    /* Copy data and descriptor */
    memcpy(data_slot, data, desc->data_size);

    *slot = *desc;
    slot->data_offset = (uint32_t)(data_slot - (uint8_t*)channel->shm_ptr);

    atomic_store_explicit(&hdr->rb_head, head + 1, memory_order_release);
    atomic_fetch_add_explicit(&hdr->frames_sent, 1, memory_order_relaxed);

    return true;
}

bool aspira_ipc_recv_frame(aspira_ipc_channel* channel,
                            aspira_ipc_frame_desc* desc,
                            float* data) {
    if (!channel || !desc || !data) return false;

    aspira_ipc_shm_header* hdr = channel->header;
    uint64_t tail = atomic_load_explicit(&hdr->rb_tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&hdr->rb_head, memory_order_acquire);

    if (tail >= head) {
        return false;  /* Empty */
    }

    /* Get descriptor slot */
    size_t desc_offset = hdr->ring_buffer_offset +
                         (tail & (hdr->ring_buffer_capacity - 1)) *
                         sizeof(aspira_ipc_frame_desc);
    aspira_ipc_frame_desc* slot = (aspira_ipc_frame_desc*)((uint8_t*)channel->shm_ptr + desc_offset);

    /* Copy descriptor */
    *desc = *slot;

    /* Copy data */
    uint8_t* data_src = (uint8_t*)channel->shm_ptr + slot->data_offset;
    memcpy(data, data_src, desc->data_size);

    atomic_store_explicit(&hdr->rb_tail, tail + 1, memory_order_release);
    atomic_fetch_add_explicit(&hdr->frames_received, 1, memory_order_relaxed);

    return true;
}

/* ==========================================================================
 * Control Messages
 * ========================================================================== */

bool aspira_ipc_send_control(aspira_ipc_channel* channel,
                              const aspira_ipc_control_msg* msg) {
    if (!channel || !msg || channel->mq == (mqd_t)-1) return false;

    int ret = mq_send(channel->mq, (const char*)msg,
                      sizeof(aspira_ipc_control_msg), 0);
    return ret == 0;
}

bool aspira_ipc_recv_control(aspira_ipc_channel* channel,
                              aspira_ipc_control_msg* msg) {
    if (!channel || !msg || channel->mq == (mqd_t)-1) return false;

    ssize_t ret = mq_receive(channel->mq, (char*)msg,
                              sizeof(aspira_ipc_control_msg), NULL);
    return ret == sizeof(aspira_ipc_control_msg);
}

bool aspira_ipc_recv_control_timed(aspira_ipc_channel* channel,
                                    aspira_ipc_control_msg* msg,
                                    uint64_t timeout_ms) {
    if (!channel || !msg || channel->mq == (mqd_t)-1) return false;

    /* Set MQ to blocking for timed receive */
    struct mq_attr old_attr;
    mq_getattr(channel->mq, &old_attr);

    struct mq_attr new_attr = old_attr;
    new_attr.mq_flags &= ~O_NONBLOCK;

    /* Temporarily make it blocking (may fail, which is OK — we try anyway) */
    mq_setattr(channel->mq, &new_attr, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000UL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000UL;
    }

    ssize_t ret = mq_timedreceive(channel->mq, (char*)msg,
                                   sizeof(aspira_ipc_control_msg),
                                   NULL, &ts);

    /* Restore non-blocking */
    mq_setattr(channel->mq, &old_attr, NULL);

    return ret == sizeof(aspira_ipc_control_msg);
}

uint64_t aspira_ipc_available_frames(const aspira_ipc_channel* channel) {
    if (!channel || !channel->header) return 0;

    uint64_t head = atomic_load_explicit(&channel->header->rb_head,
                                          memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&channel->header->rb_tail,
                                          memory_order_acquire);
    return head - tail;
}

void aspira_ipc_get_stats(const aspira_ipc_channel* channel,
                           uint64_t* frames_sent,
                           uint64_t* frames_received,
                           uint64_t* frames_dropped) {
    if (!channel || !channel->header) {
        if (frames_sent) *frames_sent = 0;
        if (frames_received) *frames_received = 0;
        if (frames_dropped) *frames_dropped = 0;
        return;
    }

    if (frames_sent) {
        *frames_sent = atomic_load_explicit(&channel->header->frames_sent,
                                             memory_order_relaxed);
    }
    if (frames_received) {
        *frames_received = atomic_load_explicit(&channel->header->frames_received,
                                                  memory_order_relaxed);
    }
    if (frames_dropped) {
        *frames_dropped = atomic_load_explicit(&channel->header->frames_dropped,
                                                 memory_order_relaxed);
    }
}
