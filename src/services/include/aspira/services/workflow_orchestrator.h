/**
 * @file workflow_orchestrator.h
 * @brief Scan workflow state machine
 *
 * States: IDLE -> READY -> PREPARING -> SCANNING -> PAUSED -> REVIEWING -> SAVING
 *
 * Events and valid transitions:
 *   IDLE       + SYSTEM_READY   -> READY
 *   READY      + START_SCAN     -> PREPARING
 *   PREPARING  + FRAME_CAPTURED -> SCANNING
 *   SCANNING   + PAUSE_SCAN     -> PAUSED
 *   PAUSED     + RESUME_SCAN    -> SCANNING
 *   SCANNING   + STOP_SCAN      -> REVIEWING
 *   REVIEWING  + REVIEW_COMPLETE -> SAVING
 *   SAVING     + SAVE_COMPLETE  -> READY
 *   ANY        + FAULT_DETECTED -> ERROR
 *   ERROR      + RECOVER        -> IDLE
 */

#ifndef ASPIRA_SERVICES_WORKFLOW_ORCHESTRATOR_H
#define ASPIRA_SERVICES_WORKFLOW_ORCHESTRATOR_H

#include <functional>
#include <string>
#include <vector>

namespace aspira {

enum class WorkflowState {
    IDLE,
    READY,
    PREPARING,
    SCANNING,
    PAUSED,
    ANALYZING,    /* AI segmentation analysis phase */
    REVIEWING,
    SAVING,
    ERROR
};

enum class WorkflowEvent {
    SYSTEM_READY,
    START_SCAN,
    PAUSE_SCAN,
    RESUME_SCAN,
    STOP_SCAN,
    FRAME_CAPTURED,
    START_AI_ANALYSIS,    /* Begin AI segmentation */
    AI_ANALYSIS_COMPLETE, /* AI segmentation finished */
    REVIEW_COMPLETE,
    SAVE_COMPLETE,
    FAULT_DETECTED,
    RECOVER
};

class WorkflowOrchestrator {
public:
    WorkflowOrchestrator();

    /**
     * @brief Process a workflow event, triggering state transition if valid
     * @return The new state after processing
     */
    WorkflowState handle_event(WorkflowEvent event);

    /**
     * @brief Register a callback for state transitions
     * @param cb Called with (old_state, new_state)
     */
    void on_transition(std::function<void(WorkflowState, WorkflowState)> cb);

    WorkflowState current_state() const { return current_state_; }
    const char* state_name() const;
    const char* event_name(WorkflowEvent event) const;

private:
    WorkflowState current_state_ = WorkflowState::IDLE;
    std::vector<std::function<void(WorkflowState, WorkflowState)>> callbacks_;

    struct Transition {
        WorkflowState from;
        WorkflowEvent event;
        WorkflowState to;
    };

    static const Transition kTransitions[];
    static const size_t kNumTransitions;
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_WORKFLOW_ORCHESTRATOR_H */
