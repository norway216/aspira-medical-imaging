/**
 * @file main.cpp
 * @brief Aspira Medical Imaging Framework — System Assembly & Demo
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

#ifdef ASPIRA_HAS_OPENCV
#include <aspira/app/visualizer.h>
#endif

#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

using namespace aspira;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000UL + (uint64_t)ts.tv_nsec;
}

/* ==========================================================================
 * Real-time FPS Calculator (sliding window)
 * ========================================================================== */
struct FpsCalculator {
    static const int WINDOW = 30;  /* frames */
    double history[WINDOW] = {};
    int idx = 0;
    int count = 0;
    uint64_t last_ts = 0;

    void tick(uint64_t now_ns) {
        if (last_ts != 0 && count < WINDOW) {
            double dt_ms = (double)(now_ns - last_ts) / 1.0e6;
            if (dt_ms > 0.0) {
                history[idx % WINDOW] = 1000.0 / dt_ms;  /* instantaneous FPS */
                idx++;
                count++;
            }
        }
        last_ts = now_ns;
    }

    double get() const {
        if (count == 0) return 0.0;
        int n = (count < WINDOW) ? count : WINDOW;
        double sum = 0.0;
        for (int i = 0; i < n; i++) sum += history[i];
        return sum / (double)n;
    }
};

/* ==========================================================================
 * Processing thread: single consumer of SPSC acq queue,
 * single producer to SPSC render queue (preserves SPSC contract)
 * ========================================================================== */
static void processing_thread_fn(
        aspira::RingBuffer* acq_queue,
        aspira::RingBuffer* render_queue,
        aspira::SignalPipeline* signal_pipeline,
        aspira::FramePool* frame_pool,
        std::atomic<uint64_t>* frames_processed,
        std::atomic<uint64_t>* frames_dropped,
        std::atomic<uint64_t>* total_latency_ns,
        FpsCalculator* fps_proc,
        Watchdog* proc_wd,
        const std::atomic<bool>* running) {

    while (running->load()) {
        aspira_frame* frame = nullptr;

        if (!aspira_spsc_pop(acq_queue->native(), &frame) || !frame) {
            /* Queue empty — brief sleep to avoid busy-wait */
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        uint64_t acquire_ts = frame->timestamp_ns;

        signal_pipeline->process_inplace(frame);

        uint64_t proc_ts = get_timestamp_ns();
        total_latency_ns->fetch_add(proc_ts - acquire_ts);
        frames_processed->fetch_add(1);
        fps_proc->tick(proc_ts);
        proc_wd->pet();

        if (!aspira_spsc_push(render_queue->native(), &frame)) {
            aspira_frame_pool_free_frame(frame_pool->native(), frame);
            frames_dropped->fetch_add(1);
        }
    }
}

int main() {
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

    workflow.on_transition([&](WorkflowState from, WorkflowState to) {
        logger.info("Workflow: " + std::to_string((int)from) +
                    " -> " + std::to_string((int)to), "workflow");
    });

    security.authenticate("technician", "technician_hash");
    logger.info("Authenticated as: technician");
    audit.log("technician", AuditAction::USER_LOGIN, "Console login");

    /* ==================================================================
     * 2. Create Memory Pools
     * ================================================================== */
    auto& pipe_cfg = config.pipeline_config();
    auto& probe_cfg = config.probe_config();

    FramePool frame_pool(pipe_cfg.frame_pool_size,
                          probe_cfg.num_lines,
                          probe_cfg.samples_per_line, 1);

    logger.info("Frame pool: " + std::to_string(pipe_cfg.frame_pool_size) +
                " frames, " + std::to_string(probe_cfg.num_lines) + "x" +
                std::to_string(probe_cfg.samples_per_line));

    /* ==================================================================
     * 3. Create Ring Buffers
     * ================================================================== */
    RingBuffer acq_queue(pipe_cfg.acquisition_queue_size, sizeof(aspira_frame*));
    RingBuffer render_queue(pipe_cfg.render_queue_size, sizeof(aspira_frame*));

    logger.info("Ring buffers: acq=" +
                std::to_string(pipe_cfg.acquisition_queue_size) +
                ", render=" + std::to_string(pipe_cfg.render_queue_size));

    /* ==================================================================
     * 4. Create Thread Pool
     * ================================================================== */
    ThreadPool thread_pool(pipe_cfg.thread_pool_size, 256);
    for (size_t i = 0; i < pipe_cfg.thread_pool_size; i++) {
        thread_pool.set_affinity(i, (int)i);
    }
    logger.info("Thread pool: " + std::to_string(pipe_cfg.thread_pool_size) +
                " workers");

    /* ==================================================================
     * 5. Setup Signal Pipeline
     * ================================================================== */
    SignalPipeline signal_pipeline;
    signal_pipeline.add_dc_remove("dc_blocker");
    if (pipe_cfg.enable_envelope) {
        signal_pipeline.add_envelope("envelope_detector");
    }
    signal_pipeline.add_gain(pipe_cfg.gain_db, "gain");
    logger.info("Signal pipeline: " +
                std::to_string(signal_pipeline.filter_count()) + " filters");

    /* ==================================================================
     * 6. Create Data Generator
     * ================================================================== */
    SimulatedProbeConfig gen_probe_cfg = probe_cfg;
    gen_probe_cfg.noise_level = config.sim_config().noise_level;
    DataGenerator generator(gen_probe_cfg);

    generator.add_target({20.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f});
    generator.add_target({40.0f, -5.0f, 0.8f, 1.5f, 10.0f, 90.0f});
    generator.add_target({60.0f, 5.0f, 0.6f, 1.0f, 5.0f, -45.0f});

    logger.info("Data generator: " +
                std::to_string(generator.targets().size()) + " targets");

    /* ==================================================================
     * 7. Double Buffer & UI
     * ================================================================== */
    aspira_frame* front_buf = aspira_frame_pool_alloc_frame(frame_pool.native());
    aspira_frame* back_buf  = aspira_frame_pool_alloc_frame(frame_pool.native());

    DoubleBuffer display_buffer;
    display_buffer.init(front_buf, back_buf);

    SimulatedUI ui;
    ui.set_display_mode(1);
    ui.show_banner();

#ifdef ASPIRA_HAS_OPENCV
    Visualizer viz("Aspira Medical Imaging", 960, 400);
    logger.info("OpenCV visualizer enabled");
#endif

    /* ==================================================================
     * 8. Setup Watchdogs
     * ================================================================== */
    Watchdog acq_wd("acquisition", pipe_cfg.watchdog_timeout_ms);
    Watchdog proc_wd("processing", pipe_cfg.watchdog_timeout_ms);
    Watchdog render_wd("rendering", pipe_cfg.watchdog_timeout_ms);

    workflow.handle_event(WorkflowEvent::SYSTEM_READY);
    workflow.handle_event(WorkflowEvent::START_SCAN);

    /* ==================================================================
     * 9. Pipeline Statistics
     * ================================================================== */
    PipelineStats stats = {};

    std::atomic<uint64_t> frames_acquired{0};
    std::atomic<uint64_t> frames_processed{0};
    std::atomic<uint64_t> frames_displayed{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<uint64_t> total_latency_ns{0};

    FpsCalculator fps_acq, fps_proc, fps_disp;

    /* ==================================================================
     * 10. Dedicated Processing Thread (respects SPSC contract)
     * ================================================================== */
    std::thread proc_thread(processing_thread_fn,
                             &acq_queue, &render_queue,
                             &signal_pipeline, &frame_pool,
                             &frames_processed, &frames_dropped,
                             &total_latency_ns, &fps_proc,
                             &proc_wd, &g_running);

    /* ==================================================================
     * 11. Main Loop
     * ================================================================== */
    auto last_ui_update = std::chrono::steady_clock::now();
    auto last_target_update = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::microseconds(
        (int64_t)(1.0e6 / gen_probe_cfg.frame_rate));

    logger.info("Entering main pipeline loop...");

    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();

        /* --- Acquisition --- */
        uint64_t ts = get_timestamp_ns();
        aspira_frame* new_frame = generator.generate_frame(ts, frame_pool.native());

        if (new_frame) {
            if (!aspira_spsc_push(acq_queue.native(), &new_frame)) {
                aspira_frame_pool_free_frame(frame_pool.native(), new_frame);
                frames_dropped.fetch_add(1);
            } else {
                frames_acquired.fetch_add(1);
                fps_acq.tick(ts);
                acq_wd.pet();
            }
        }

        /* --- Rendering --- */
        aspira_frame* display_frame = nullptr;
        if (aspira_spsc_pop(render_queue.native(), &display_frame) &&
            display_frame) {
            aspira_frame* write_buf = display_buffer.write_begin();
            if (write_buf && write_buf->data && display_frame->data &&
                write_buf->data_size >= display_frame->data_size) {
                memcpy(write_buf->data, display_frame->data,
                       display_frame->data_size);
                write_buf->frame_id = display_frame->frame_id;
                write_buf->timestamp_ns = display_frame->timestamp_ns;
                write_buf->flags = display_frame->flags;

                display_buffer.swap();
                frames_displayed.fetch_add(1);
                fps_disp.tick(get_timestamp_ns());
                render_wd.pet();
            }
            aspira_frame_pool_free_frame(frame_pool.native(), display_frame);
        }

        /* --- UI Update (every ~33ms) --- */
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_ui_update).count();

        if (elapsed >= 33) {
            uint64_t acq = frames_acquired.load();
            uint64_t proc = frames_processed.load();
            uint64_t disp = frames_displayed.load();

            /* Use sliding-window FPS */
            stats.acquisition_fps = fps_acq.get();
            stats.processing_fps  = fps_proc.get();
            stats.display_fps     = fps_disp.get();

            stats.frames_acquired  = acq;
            stats.frames_processed = proc;
            stats.frames_displayed = disp;
            stats.frames_dropped   = frames_dropped.load();

            uint64_t lat_ns = total_latency_ns.load();
            if (proc > 0) {
                stats.avg_frame_latency_us = (double)lat_ns / (double)proc / 1000.0;
            }

            stats.pool_allocations = pipe_cfg.frame_pool_size;
            stats.pool_free = frame_pool.free_count();
            stats.ring_buffer_usage =
                (aspira_spsc_count(acq_queue.native()) * 100) /
                aspira_spsc_capacity(acq_queue.native());
            stats.pipeline_healthy = (acq_wd.is_healthy() && proc_wd.is_healthy() && render_wd.is_healthy());

            ui.update(stats);
            last_ui_update = now;

#ifdef ASPIRA_HAS_OPENCV
            {
                const aspira_frame* f = display_buffer.read();
                if (f && f->data) {
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(1)
                       << "FPS: A=" << stats.acquisition_fps
                       << " P=" << stats.processing_fps
                       << " D=" << stats.display_fps
                       << " | Lat: " << (int)stats.avg_frame_latency_us << "us"
                       << " | Queue: " << aspira_spsc_count(acq_queue.native())
                       << " | Drop: " << stats.frames_dropped
                       << " | " << (stats.pipeline_healthy ? "HEALTHY" : "FAULT");
                    viz.set_status(ss.str());
                    viz.show(f, "");
                }
                int key = viz.wait_key(1);
                if (key == 'q' || key == 'Q' || key == 27) g_running = false;
                if (key == 's' || key == 'S') {
                    const aspira_frame* f2 = display_buffer.read();
                    if (f2 && f2->data) {
                        char path[64];
                        snprintf(path, sizeof(path), "/tmp/aspira_frame_%05lu.pgm",
                                 (unsigned long)f2->frame_id);
                        aspira_export_frame_pgm(f2, path, true);
                    }
                }
            }
#endif
        }

        /* --- Update target positions (every 100ms) --- */
        auto since_target = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_target_update).count();
        if (since_target >= 100) {
            generator.update_targets(0.1f);
            last_target_update = now;
        }

        /* --- Watchdog check --- */
        if (!(acq_wd.is_healthy() && proc_wd.is_healthy() && render_wd.is_healthy())) {
            logger.error("Pipeline watchdog triggered fault!");
            audit.log("system", AuditAction::PIPELINE_FAULT, "Watchdog timeout");
            workflow.handle_event(WorkflowEvent::FAULT_DETECTED);
            break;
        }

        /* --- Console input --- */
        if (ui.has_input()) {
            char c = ui.get_input();
            switch (c) {
            case 'q': case 'Q': g_running = false; break;
            case 'h': case 'H': ui.show_help(); break;
            case '0': ui.set_display_mode(0); break;
            case '1': ui.set_display_mode(1); break;
            case '2': ui.set_display_mode(2); break;
            case 's': case 'S': {
                const aspira_frame* f = display_buffer.read();
                if (f && f->data) ui.print_frame_ascii(f, 80);
                break;
            }
            case 'e': case 'E': {
                const aspira_frame* f = display_buffer.read();
                if (f && f->data) {
                    char path[64];
                    snprintf(path, sizeof(path), "/tmp/aspira_frame_%05lu.pgm",
                             (unsigned long)f->frame_id);
                    if (aspira_export_frame_pgm(f, path, true))
                        std::cout << "\n  Exported: " << path << "\n";
                }
                break;
            }
            default: break;
            }
        }

        /* --- Frame rate limiting --- */
        auto loop_elapsed = std::chrono::steady_clock::now() - loop_start;
        if (loop_elapsed < frame_interval) {
            std::this_thread::sleep_for(frame_interval - loop_elapsed);
        }
    }

    /* ==================================================================
     * 12. Graceful Shutdown
     * ================================================================== */
    logger.info("Shutting down...");
    g_running = false;
    proc_thread.join();

    /* Drain remaining frames from queues and return to pool */
    aspira_frame* leftover = nullptr;
    while (aspira_spsc_pop(acq_queue.native(), &leftover) && leftover) {
        aspira_frame_pool_free_frame(frame_pool.native(), leftover);
    }
    while (aspira_spsc_pop(render_queue.native(), &leftover) && leftover) {
        aspira_frame_pool_free_frame(frame_pool.native(), leftover);
    }

    /* Return display buffer frames to pool */
    aspira_frame_pool_free_frame(frame_pool.native(), front_buf);
    aspira_frame_pool_free_frame(frame_pool.native(), back_buf);

    audit.log("technician", AuditAction::USER_LOGOUT, "Session ended");
    security.logout();
    workflow.handle_event(WorkflowEvent::STOP_SCAN);

    std::cout << "\n═══ Final Statistics ═══\n";
    std::cout << "  Frames Acquired:  " << frames_acquired.load() << "\n";
    std::cout << "  Frames Processed: " << frames_processed.load() << "\n";
    std::cout << "  Frames Displayed: " << frames_displayed.load() << "\n";
    std::cout << "  Frames Dropped:   " << frames_dropped.load() << "\n";

    logger.info("Aspira Medical Imaging Framework shutdown complete.");
    std::cout << "\nGoodbye.\n";
    return 0;
}
