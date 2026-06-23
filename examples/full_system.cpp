/**
 * @file full_system.cpp
 * @brief Full C++ framework demonstration
 */

#include <aspira/core/core.h>
#include <aspira/services/module_wrapper.h>
#include <aspira/services/security_manager.h>
#include <aspira/services/logging_service.h>
#include <aspira/services/workflow_orchestrator.h>
#include <aspira/app/data_generator.h>

#include <iostream>
#include <thread>
#include <chrono>

using namespace aspira;
using namespace std::chrono_literals;

int main() {
    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   Aspira Full System Demo (C++)      ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    try {
        /* 1. Services */
        LoggingService logger;
        logger.set_json_format(false);
        logger.info("Starting full system demo...");

        SecurityManager security;
        security.authenticate("doctor", "doctor_hash");
        logger.info("Authenticated as doctor");

        WorkflowOrchestrator workflow;
        workflow.on_transition([&](WorkflowState from, WorkflowState to) {
            std::cout << "  [Workflow] " << (int)from << " -> " << (int)to << "\n";
        });

        workflow.handle_event(WorkflowEvent::SYSTEM_READY);
        workflow.handle_event(WorkflowEvent::START_SCAN);

        /* 2. Probe config */
        SimulatedProbeConfig probe_cfg;
        probe_cfg.samples_per_line = 512;
        probe_cfg.num_lines = 64;

        /* 3. Memory pool */
        FramePool frame_pool(32, probe_cfg.num_lines,
                              probe_cfg.samples_per_line, 1);
        logger.info("Frame pool: 32 frames");

        /* 4. Ring buffers */
        RingBuffer acq_queue(32, sizeof(aspira_frame*));
        RingBuffer render_queue(32, sizeof(aspira_frame*));

        /* 5. Thread pool */
        ThreadPool thread_pool(2, 128);
        thread_pool.set_affinity(0, 0);
        thread_pool.set_affinity(1, 1);
        logger.info("Thread pool: 2 workers with CPU affinity");

        /* 6. Signal pipeline */
        SignalPipeline pipeline;
        pipeline.add_dc_remove();
        pipeline.add_envelope();
        pipeline.add_gain(30.0f);
        logger.info("Pipeline: " + std::to_string(pipeline.filter_count()) +
                    " filters");

        /* 7. Data generator */
        DataGenerator generator(probe_cfg);
        generator.add_target({20.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f});
        generator.add_target({40.0f, -5.0f, 0.8f, 1.5f, 10.0f, 90.0f});
        generator.add_target({60.0f, 5.0f, 0.6f, 1.0f, 5.0f, -45.0f});
        logger.info("Generator: 3 targets configured");

        /* 8. Run pipeline for a few frames */
        const int kFrames = 50;
        std::cout << "\nRunning " << kFrames << " frames through pipeline...\n";

        int processed = 0;
        for (int i = 0; i < kFrames; i++) {
            /* Generate frame */
            uint64_t ts = i * 33333;
            aspira_frame* frame = generator.generate_frame(ts,
                                                            frame_pool.native());
            if (!frame) break;

            /* Push to acquisition queue */
            if (!aspira_spsc_push(acq_queue.native(), &frame)) {
                aspira_frame_pool_free_frame(frame_pool.native(), frame);
            }

            /* Process one frame from queue */
            aspira_frame* proc_frame = nullptr;
            if (aspira_spsc_pop(acq_queue.native(), &proc_frame) && proc_frame) {
                pipeline.process_inplace(proc_frame);
                aspira_frame_pool_free_frame(frame_pool.native(), proc_frame);
                processed++;
            }

            /* Update targets every 10 frames */
            if (i % 10 == 0) {
                generator.update_targets(0.333f);
            }

            if (i % 10 == 0) {
                std::cout << "  Frame " << i << "/" << kFrames
                          << " (queue: " << aspira_spsc_count(acq_queue.native())
                          << ")\n";
            }
        }

        /* 9. Results */
        std::cout << "\n=== Results ===\n";
        std::cout << "Frames generated:  " << kFrames << "\n";
        std::cout << "Frames processed:  " << processed << "\n";
        std::cout << "Frames in pool:    " << frame_pool.free_count() << "\n";
        std::cout << "Workflow state:    " << workflow.state_name() << "\n";
        std::cout << "User role:         "
                  << (security.current_role() == UserRole::DOCTOR ? "Doctor" : "Other")
                  << "\n";

        /* 10. Cleanup */
        workflow.handle_event(WorkflowEvent::STOP_SCAN);
        security.logout();
        logger.info("Demo completed successfully.");

        std::cout << "\n✓ Full system demo passed!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
