/**
 * @file simple_acquisition.c
 * @brief Minimal C example demonstrating the core API
 *
 * This example shows:
 *  - SPSC ring buffer usage
 *  - Memory pool allocation
 *  - Frame lifecycle
 *  - Simple signal processing (DC removal)
 *
 * Build: gcc -o simple_acquisition simple_acquisition.c -laspira_core -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aspira/core/core.h"

/* Get monotonic timestamp in nanoseconds */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000UL + (uint64_t)ts.tv_nsec;
}

#define WIDTH   64
#define HEIGHT  512
#define N_FRAMES 10

int main(void) {
    printf("=== Aspira Core — Simple Acquisition Example ===\n\n");

    /* 1. Create frame pool */
    aspira_frame_pool pool;
    if (!aspira_frame_pool_init(&pool, 16, WIDTH, HEIGHT, 1)) {
        fprintf(stderr, "Failed to create frame pool\n");
        return 1;
    }
    printf("Frame pool: %u frames of %ux%u\n",
           16U, WIDTH, HEIGHT);

    /* 2. Create SPSC ring buffer */
    aspira_spsc_rb queue;
    if (!aspira_spsc_init(&queue, 16, sizeof(aspira_frame*))) {
        fprintf(stderr, "Failed to create ring buffer\n");
        return 1;
    }
    printf("Ring buffer: capacity %lu\n", (unsigned long)queue.capacity);

    /* 3. Create signal pipeline */
    aspira_pipeline pipeline;
    aspira_pipeline_init(&pipeline);
    aspira_pipeline_add_dc_remove(&pipeline, "dc_remove");
    aspira_pipeline_add_gain(&pipeline, 20.0f, "gain");
    printf("Pipeline: %zu filters\n", aspira_pipeline_filter_count(&pipeline));

    /* 4. Simulate acquisition: generate frames -> push to queue */
    printf("\nSimulating %d frames of acquisition...\n", N_FRAMES);

    for (int i = 0; i < N_FRAMES; i++) {
        /* Allocate frame from pool */
        aspira_frame* frame = aspira_frame_pool_alloc_frame(&pool);
        if (!frame) {
            fprintf(stderr, "Frame pool exhausted!\n");
            break;
        }

        /* Fill with simulated data (simple ramp) */
        for (size_t j = 0; j < (size_t)WIDTH * HEIGHT; j++) {
            frame->data[j] = (float)(j % 256) / 256.0f + 5.0f;  /* +5 DC offset */
        }

        frame->frame_id = i;
        frame->timestamp_ns = now_ns();
        frame->flags = ASPIRA_FRAME_FLAG_VALID | ASPIRA_FRAME_FLAG_NEW;
        frame->modality = ASPIRA_MODALITY_ULTRASOUND;

        /* Push to acquisition queue */
        if (!aspira_spsc_push(&queue, &frame)) {
            fprintf(stderr, "Queue full! Frame %d dropped.\n", i);
            aspira_frame_pool_free_frame(&pool, frame);
        }
    }

    printf("Frames in queue: %lu\n", (unsigned long)aspira_spsc_count(&queue));

    /* 5. Process frames from queue */
    printf("\nProcessing frames...\n");

    aspira_frame* frame = NULL;
    int processed = 0;
    float total_dc_before = 0.0f;
    float total_dc_after = 0.0f;

    while (aspira_spsc_pop(&queue, &frame) && frame) {
        /* Measure DC offset before processing */
        float dc = 0.0f;
        for (size_t j = 0; j < 100; j++) {
            dc += frame->data[j];
        }
        dc /= 100.0f;
        total_dc_before += dc;

        /* Process through pipeline (in-place) */
        aspira_pipeline_process_inplace(&pipeline, frame);

        /* Measure DC offset after processing */
        float dc_after = 0.0f;
        for (size_t j = 0; j < 100; j++) {
            dc_after += frame->data[j];
        }
        dc_after /= 100.0f;
        total_dc_after += dc_after;

        printf("  Frame %lu: DC before=%.2f, DC after=%.4f, gain applied\n",
               (unsigned long)frame->frame_id, (double)dc, (double)dc_after);

        /* Return frame to pool */
        aspira_frame_pool_free_frame(&pool, frame);
        processed++;
    }

    /* 6. Results */
    printf("\n=== Results ===\n");
    printf("Frames processed: %d\n", processed);
    printf("Average DC before: %.2f\n", (double)(total_dc_before / processed));
    printf("Average DC after:  %.4f (should be near 0)\n",
           (double)(total_dc_after / processed));
    printf("Frames in pool: %zu (all returned)\n",
           aspira_frame_pool_free_count(&pool));

    /* Validate */
    if (processed == N_FRAMES) {
        printf("\n✓ All frames processed successfully!\n");
    } else {
        printf("\n✗ Some frames were lost!\n");
        return 1;
    }

    /* 7. Cleanup */
    aspira_pipeline_destroy(&pipeline);
    aspira_spsc_destroy(&queue);
    aspira_frame_pool_destroy(&pool);

    return 0;
}
