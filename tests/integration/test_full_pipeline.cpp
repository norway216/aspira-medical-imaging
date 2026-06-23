/**
 * @file test_full_pipeline.cpp
 * @brief End-to-end integration test for the full imaging pipeline
 */

#include <aspira/core/core.h>
#include <aspira/services/module_wrapper.h>
#include <aspira/services/security_manager.h>
#include <aspira/services/workflow_orchestrator.h>
#include <aspira/app/data_generator.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

using namespace aspira;

TEST_CASE("Full pipeline end-to-end test", "[integration][pipeline]") {
    /* ==================================================================
     * Setup
     * ================================================================== */

    /* Probe configuration */
    SimulatedProbeConfig probe_cfg;
    probe_cfg.num_elements = 64;
    probe_cfg.frequency_hz = 5.0e6f;
    probe_cfg.sampling_rate_hz = 40.0e6f;
    probe_cfg.sound_speed_mps = 1540.0f;
    probe_cfg.pitch_mm = 0.3f;
    probe_cfg.samples_per_line = 512;   /* Smaller for fast test */
    probe_cfg.num_lines = 64;           /* Smaller for fast test */
    probe_cfg.frame_rate = 30.0f;
    probe_cfg.noise_level = 0.01f;

    /* Frame pool */
    FramePool frame_pool(32, probe_cfg.num_lines,
                          probe_cfg.samples_per_line, 1);

    /* Ring buffers */
    RingBuffer acq_queue(32, sizeof(aspira_frame*));
    RingBuffer render_queue(32, sizeof(aspira_frame*));

    /* Thread pool */
    ThreadPool thread_pool(2, 64);

    /* Signal pipeline */
    aspira_pipeline* pipeline = aspira_pipeline_create();
    REQUIRE(pipeline != nullptr);
    aspira_pipeline_add_dc_remove(pipeline, "dc");
    aspira_pipeline_add_gain(pipeline, 20.0f, "gain");

    /* Data generator */
    DataGenerator generator(probe_cfg);
    SimulatedTarget t1;
    t1.depth_mm = 30.0f;
    t1.amplitude = 1.0f;
    t1.size_mm = 2.0f;
    t1.velocity_mm_s = 0.0f;
    generator.add_target(t1);

    /* Services */
    SecurityManager security;
    security.authenticate("technician", "technician_hash");

    WorkflowOrchestrator workflow;
    workflow.handle_event(WorkflowEvent::SYSTEM_READY);
    workflow.handle_event(WorkflowEvent::START_SCAN);

    /* ==================================================================
     * Pipeline processing function
     * ================================================================== */

    std::atomic<int> frames_generated{0};
    std::atomic<int> frames_processed{0};
    std::atomic<int> frames_displayed{0};
    std::atomic<int> frames_dropped{0};
    std::atomic<bool> running{true};

    /* Producer thread: generate frames */
    std::thread producer([&]() {
        for (int i = 0; i < 100; i++) {
            uint64_t ts = i * 33333; /* ~30fps timing */
            aspira_frame* frame = generator.generate_frame(ts,
                                                            frame_pool.native());
            if (frame) {
                if (!aspira_spsc_push(acq_queue.native(), &frame)) {
                    aspira_frame_pool_free_frame(frame_pool.native(), frame);
                    frames_dropped.fetch_add(1);
                } else {
                    frames_generated.fetch_add(1);
                }
            }
        }
    });

    /* Consumer/processing thread */
    std::thread consumer([&]() {
        while (running.load() || aspira_spsc_count(acq_queue.native()) > 0) {
            aspira_frame* frame = nullptr;
            if (aspira_spsc_pop(acq_queue.native(), &frame) && frame) {
                /* Process through pipeline */
                aspira_pipeline_process_inplace(pipeline, frame);

                frames_processed.fetch_add(1);

                /* Push to render queue */
                if (!aspira_spsc_push(render_queue.native(), &frame)) {
                    aspira_frame_pool_free_frame(frame_pool.native(), frame);
                    frames_dropped.fetch_add(1);
                }
            }
        }
    });

    /* Display thread */
    std::thread display([&]() {
        while (running.load() || aspira_spsc_count(render_queue.native()) > 0) {
            aspira_frame* frame = nullptr;
            if (aspira_spsc_pop(render_queue.native(), &frame) && frame) {
                /* Verify frame integrity */
                REQUIRE(frame->flags & ASPIRA_FRAME_FLAG_PROCESSED);
                REQUIRE(frame->data != nullptr);

                frames_displayed.fetch_add(1);
                aspira_frame_pool_free_frame(frame_pool.native(), frame);
            }
        }
    });

    /* Wait for producer */
    producer.join();
    running = false;

    /* Wait for consumer and display */
    consumer.join();
    display.join();

    /* ==================================================================
     * Verification
     * ================================================================== */

    REQUIRE(frames_generated.load() == 100);
    REQUIRE(frames_processed.load() == 100);
    REQUIRE(frames_displayed.load() == 100);
    REQUIRE(frames_dropped.load() == 0);

    /* Verify frame pool is full (all frames returned) */
    REQUIRE(frame_pool.free_count() == 32);

    /* Cleanup */
    workflow.handle_event(WorkflowEvent::STOP_SCAN);
    aspira_pipeline_free_ptr(pipeline);
}

TEST_CASE("Data generator produces valid frames", "[integration][generator]") {
    SimulatedProbeConfig cfg;
    cfg.samples_per_line = 256;
    cfg.num_lines = 64;

    DataGenerator generator(cfg);
    generator.add_target({20.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f});

    FramePool pool(8, cfg.num_lines, cfg.samples_per_line, 1);

    /* Generate 10 frames */
    for (int i = 0; i < 10; i++) {
        aspira_frame* frame = generator.generate_frame(i * 33333, pool.native());
        REQUIRE(frame != nullptr);
        REQUIRE(frame->width == cfg.num_lines);
        REQUIRE(frame->height == cfg.samples_per_line);
        REQUIRE(frame->data != nullptr);
        REQUIRE(frame->frame_id == (uint64_t)i);
        REQUIRE(frame->flags & ASPIRA_FRAME_FLAG_VALID);

        /* Verify frame has non-zero data (target should produce signal) */
        bool has_signal = false;
        size_t total = (size_t)cfg.num_lines * cfg.samples_per_line;
        for (size_t j = 0; j < total; j++) {
            if (frame->data[j] != 0.0f) {
                has_signal = true;
                break;
            }
        }
        REQUIRE(has_signal);

        aspira_frame_pool_free_frame(pool.native(), frame);
    }
}
