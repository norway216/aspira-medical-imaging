/**
 * @file main.cpp
 * @brief Aspira Medical Imaging Framework — System Assembly & Demo
 *
 * This main entry point wires together all modules into a running
 * medical imaging pipeline demonstration:
 *
 *   DataGenerator → SPSC Acquisition Queue → ThreadPool Processing
 *   → Signal Pipeline (DC Remove + Envelope) → SPSC Render Queue
 *   → Double Buffer → SimulatedUI Console Display
 *
 * The pipeline runs at ~30 fps with simulated ultrasound data.
 * All architectural patterns are active: lock-free queues, memory
 * pools, priority thread pool, watchdog monitoring, RBAC security,
 * audit logging, and structured logging.
 */

#include <aspira/core/core.h>
#include <aspira/services/module_wrapper.h>
#include <aspira/services/logging_service.h>
#include <aspira/services/security_manager.h>
#include <aspira/services/audit_logger.h>
#include <aspira/services/workflow_orchestrator.h>
#include <aspira/services/study_manager.h>
#include <aspira/services/patient_manager.h>
#include <aspira/app/data_generator.h>
#include <aspira/app/config_manager.h>
#include <aspira/app/simulated_ui.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <thread>

using namespace aspira;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

/* Get monotonic timestamp in nanoseconds */
static uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000UL + (uint64_t)ts.tv_nsec;
}

int main() {
    /* Install signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ==================================================================
     * 1. Initialize Services
     * ================================================================== */

    LoggingService logger("/tmp/aspira.log");
    logger.set_console_output(true);
    logger.set_json_format(false);
    logger.info("Aspira Medical Imaging Framework starting...");

    ConfigManager config;
    config.load("config/default.json");

    SecurityManager security;
    AuditLogger audit("/tmp/aspira_audit.log");
    StudyManager studies;
    PatientManager patients;
    WorkflowOrchestrator workflow;

    /* Register workflow transition listener */
    workflow.on_transition([&](WorkflowState from, WorkflowState to) {
        logger.info(std::string("Workflow: ") + " -> ",
                      "workflow");
        (void)from; (void)to;
    });

    /* ==================================================================
     * 2. Authenticate (simulated)
     * ================================================================== */

    security.authenticate("technician", "technician_hash");
    logger.info("Authenticated as: technician (role: Technician)");
    audit.log("technician", AuditAction::USER_LOGIN, "Console login");

    /* ==================================================================
     * 3. Create Memory Pools
     * ================================================================== */

    auto pipe_cfg = config.pipeline_config();
    auto probe_cfg = config.probe_config();

    /* Frame pool for the pipeline */
    aspira::FramePool frame_pool(
        pipe_cfg.frame_pool_size,
        probe_cfg.num_lines,
        probe_cfg.samples_per_line,
        1 /* channels */);

    logger.info("Frame pool created: " +
                std::to_string(pipe_cfg.frame_pool_size) + " frames, " +
                std::to_string(probe_cfg.num_lines) + "x" +
                std::to_string(probe_cfg.samples_per_line));

    /* ==================================================================
     * 4. Create Ring Buffers
     * ================================================================== */

    /* Acquisition queue: data generator -> processing */
    aspira::RingBuffer acq_queue(pipe_cfg.acquisition_queue_size,
                                  sizeof(aspira_frame*));

    /* Render queue: processing -> display */
    aspira::RingBuffer render_queue(pipe_cfg.render_queue_size,
                                     sizeof(aspira_frame*));

    logger.info("Ring buffers created: acq=" +
                std::to_string(pipe_cfg.acquisition_queue_size) +
                ", render=" + std::to_string(pipe_cfg.render_queue_size));

    /* ==================================================================
     * 5. Create Thread Pool
     * ================================================================== */

    aspira::ThreadPool thread_pool(pipe_cfg.thread_pool_size, 256);

    /* Set CPU affinity for workers */
    for (size_t i = 0; i < pipe_cfg.thread_pool_size; i++) {
        thread_pool.set_affinity(i, (int)i);
    }

    logger.info("Thread pool created: " +
                std::to_string(pipe_cfg.thread_pool_size) + " workers");

    /* ==================================================================
     * 6. Setup Signal Pipeline
     * ================================================================== */

    SignalPipeline signal_pipeline;

    /* DC removal to eliminate offset */
    signal_pipeline.add_dc_remove("dc_blocker");

    /* Envelope detection for B-mode imaging */
    if (pipe_cfg.enable_envelope) {
        signal_pipeline.add_envelope("envelope_detector");
    }

    /* Gain */
    signal_pipeline.add_gain(pipe_cfg.gain_db, "gain");

    logger.info("Signal pipeline: " +
                std::to_string(signal_pipeline.filter_count()) + " filters");

    /* ==================================================================
     * 7. Create Data Generator
     * ================================================================== */

    probe_cfg.noise_level = config.sim_config().noise_level;
    DataGenerator generator(probe_cfg);

    /* Add simulation targets */
    SimulatedTarget t1;
    t1.depth_mm = 20.0f;
    t1.lateral_mm = 0.0f;
    t1.amplitude = 1.0f;
    t1.size_mm = 2.0f;
    t1.velocity_mm_s = 0.0f;
    generator.add_target(t1);

    SimulatedTarget t2;
    t2.depth_mm = 40.0f;
    t2.lateral_mm = -5.0f;
    t2.amplitude = 0.8f;
    t2.size_mm = 1.5f;
    t2.velocity_mm_s = 10.0f;
    t2.direction_deg = 90.0f;
    generator.add_target(t2);

    SimulatedTarget t3;
    t3.depth_mm = 60.0f;
    t3.lateral_mm = 5.0f;
    t3.amplitude = 0.6f;
    t3.size_mm = 1.0f;
    t3.velocity_mm_s = 5.0f;
    t3.direction_deg = -45.0f;
    generator.add_target(t3);

    logger.info("Data generator: " + std::to_string(generator.targets().size()) +
                " targets, " + std::to_string(probe_cfg.num_elements) +
                " elements");

    /* ==================================================================
     * 8. Create Double Buffer & UI
     * ================================================================== */

    /* Allocate two frames for double buffering */
    aspira_frame* front_buf = aspira_frame_pool_alloc_frame(frame_pool.native());
    aspira_frame* back_buf = aspira_frame_pool_alloc_frame(frame_pool.native());

    DoubleBuffer display_buffer;
    display_buffer.init(front_buf, back_buf);

    SimulatedUI ui;
    ui.set_display_mode(0);
    ui.show_banner();

    /* ==================================================================
     * 9. Setup Watchdogs
     * ================================================================== */

    WatchdogManager wd_manager(200);  /* Check every 200ms */

    Watchdog acq_wd("acquisition", pipe_cfg.watchdog_timeout_ms);
    Watchdog proc_wd("processing", pipe_cfg.watchdog_timeout_ms);
    Watchdog render_wd("rendering", pipe_cfg.watchdog_timeout_ms);

    wd_manager.register_watchdog(&acq_wd);
    wd_manager.register_watchdog(&proc_wd);
    wd_manager.register_watchdog(&render_wd);

    /* ==================================================================
     * 10. Workflow: Start
     * ================================================================== */

    workflow.handle_event(WorkflowEvent::SYSTEM_READY);
    workflow.handle_event(WorkflowEvent::START_SCAN);

    /* ==================================================================
     * 11. Pipeline Statistics
     * ================================================================== */

    PipelineStats stats = {};

    std::atomic<uint64_t> frames_acquired{0};
    std::atomic<uint64_t> frames_processed{0};
    std::atomic<uint64_t> frames_displayed{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<uint64_t> total_latency_ns{0};

    /* ==================================================================
     * 12. Processing Task (thread pool worker)
     * ================================================================== */

    auto processing_task = [&]() {
        aspira_frame* frame = nullptr;

        /* Pop from acquisition queue */
        if (!aspira_spsc_pop(acq_queue.native(), &frame) || !frame) {
            return;
        }

        uint64_t acquire_ts = frame->timestamp_ns;

        /* Process through signal pipeline (in-place) */
        signal_pipeline.process_inplace(frame);

        uint64_t proc_ts = get_timestamp_ns();
        total_latency_ns.fetch_add(proc_ts - acquire_ts);

        frames_processed.fetch_add(1);

        /* Push to render queue */
        if (!aspira_spsc_push(render_queue.native(), &frame)) {
            /* Render queue full — return frame to pool */
            aspira_frame_pool_free_frame(frame_pool.native(), frame);
            frames_dropped.fetch_add(1);
        }
    };

    /* ==================================================================
     * 13. Main Loop
     * ================================================================== */

    auto last_ui_update = std::chrono::steady_clock::now();
    auto last_target_update = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::microseconds(
        (int64_t)(1.0e6 / probe_cfg.frame_rate));

    logger.info("Entering main pipeline loop...");

    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();

        /* --- Acquisition (simulated) --- */
        uint64_t ts = get_timestamp_ns();
        aspira_frame* new_frame = generator.generate_frame(ts, frame_pool.native());

        if (new_frame) {
            if (!aspira_spsc_push(acq_queue.native(), &new_frame)) {
                /* Queue full — drop frame */
                aspira_frame_pool_free_frame(frame_pool.native(), new_frame);
                frames_dropped.fetch_add(1);
            } else {
                frames_acquired.fetch_add(1);
                acq_wd.pet();
            }
        }

        /* --- Processing (dispatch to thread pool) --- */
        uint64_t pending = aspira_spsc_count(acq_queue.native());
        while (pending > 0 &&
               thread_pool.pending() < pipe_cfg.thread_pool_size * 2) {
            thread_pool.enqueue([&processing_task]() { processing_task(); },
                                 ASPIRA_PRIORITY_PROCESSING);
            proc_wd.pet();
            pending--;
        }

        /* --- Rendering (pop from render queue to double buffer) --- */
        aspira_frame* display_frame = nullptr;
        if (aspira_spsc_pop(render_queue.native(), &display_frame) &&
            display_frame) {
            /* Copy to back buffer and swap */
            aspira_frame* write_buf = display_buffer.write_begin();
            if (write_buf && write_buf->data && display_frame->data) {
                memcpy(write_buf->data, display_frame->data,
                       display_frame->data_size);
                write_buf->frame_id = display_frame->frame_id;
                write_buf->timestamp_ns = display_frame->timestamp_ns;
                write_buf->flags = display_frame->flags;

                display_buffer.swap();
                frames_displayed.fetch_add(1);
                render_wd.pet();
            }
            /* Return processed frame to pool */
            aspira_frame_pool_free_frame(frame_pool.native(), display_frame);
        }

        /* --- UI Update (every ~33ms for ~30fps) --- */
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_ui_update).count();

        if (elapsed >= 33) {
            /* Update stats */
            auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - std::chrono::steady_clock::time_point()).count();
            stats.uptime_ms = (uint64_t)total_ms;

            uint64_t acq = frames_acquired.load();
            uint64_t proc = frames_processed.load();
            uint64_t disp = frames_displayed.load();

            stats.acquisition_fps = (double)acq / ((double)total_ms / 1000.0);
            stats.processing_fps = (double)proc / ((double)total_ms / 1000.0);
            stats.display_fps = (double)disp / ((double)total_ms / 1000.0);

            stats.frames_acquired = acq;
            stats.frames_processed = proc;
            stats.frames_displayed = disp;
            stats.frames_dropped = frames_dropped.load();

            uint64_t lat_ns = total_latency_ns.load();
            if (proc > 0) {
                stats.avg_frame_latency_us = (double)lat_ns / (double)proc / 1000.0;
            }

            stats.pool_allocations = disp + acq; /* Approximation */
            stats.pool_free = disp;
            stats.ring_buffer_usage =
                (aspira_spsc_count(acq_queue.native()) * 100) /
                aspira_spsc_capacity(acq_queue.native());
            stats.pipeline_healthy = wd_manager.healthy();

            ui.update(stats);
            last_ui_update = now;
        }

        /* --- Update target positions (motion simulation) --- */
        auto since_target = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_target_update).count();
        if (since_target >= 100) {
            generator.update_targets(0.1f);  /* 100ms step */
            last_target_update = now;
        }

        /* --- Check watchdog --- */
        if (!wd_manager.healthy()) {
            logger.error("Pipeline watchdog triggered fault!");
            audit.log("system", AuditAction::PIPELINE_FAULT, "Watchdog timeout");
            workflow.handle_event(WorkflowEvent::FAULT_DETECTED);
            break;
        }

        /* --- Handle user input --- */
        if (ui.has_input()) {
            char c = ui.get_input();
            switch (c) {
            case 'q':
            case 'Q':
                g_running = false;
                break;
            case 'h':
            case 'H':
                ui.show_help();
                break;
            case '0':
                ui.set_display_mode(0);
                break;
            case '1':
                ui.set_display_mode(1);
                break;
            case '2':
                ui.set_display_mode(2);
                break;
            case 's':
            case 'S':
                /* Show frame ASCII art */
                {
                    const aspira_frame* f = display_buffer.read();
                    if (f && f->data) {
                        ui.print_frame_ascii(f, 80);
                    }
                }
                break;
            default:
                break;
            }
        }

        /* --- Frame rate limiting --- */
        auto loop_elapsed = std::chrono::steady_clock::now() - loop_start;
        if (loop_elapsed < frame_interval) {
            std::this_thread::sleep_for(frame_interval - loop_elapsed);
        }
    }

    /* ==================================================================
     * 14. Graceful Shutdown
     * ================================================================== */

    logger.info("Shutting down...");

    g_running = false;

    /* Wait for thread pool to drain */
    thread_pool.wait();

    /* Cleanup display buffers */
    aspira_frame_pool_free_frame(frame_pool.native(), front_buf);
    aspira_frame_pool_free_frame(frame_pool.native(), back_buf);

    /* Logout */
    audit.log("technician", AuditAction::USER_LOGOUT, "Session ended");
    security.logout();

    /* Workflow final transition */
    workflow.handle_event(WorkflowEvent::STOP_SCAN);

    /* Print final stats */
    std::cout << "\n\n";
    std::cout << "═══ Final Statistics ═══\n";
    std::cout << "  Frames Acquired:  " << frames_acquired.load() << "\n";
    std::cout << "  Frames Processed: " << frames_processed.load() << "\n";
    std::cout << "  Frames Displayed: " << frames_displayed.load() << "\n";
    std::cout << "  Frames Dropped:   " << frames_dropped.load() << "\n";
    std::cout << "  Audit Records:    " << audit.record_count() << "\n";
    std::cout << "  Studies:          " << studies.study_count() << "\n";
    std::cout << "  Patients:         " << patients.patient_count() << "\n";

    logger.info("Aspira Medical Imaging Framework shutdown complete.");
    std::cout << "\nGoodbye.\n";

    return 0;
}
