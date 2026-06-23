/**
 * @file workflow_orchestrator.cpp
 * @brief Workflow state machine implementation
 */

#include "aspira/services/workflow_orchestrator.h"

namespace aspira {

const char* WorkflowOrchestrator::state_name() const {
    switch (current_state_) {
    case WorkflowState::IDLE:       return "IDLE";
    case WorkflowState::READY:      return "READY";
    case WorkflowState::PREPARING:  return "PREPARING";
    case WorkflowState::SCANNING:   return "SCANNING";
    case WorkflowState::PAUSED:     return "PAUSED";
    case WorkflowState::REVIEWING:  return "REVIEWING";
    case WorkflowState::SAVING:     return "SAVING";
    case WorkflowState::ERROR:      return "ERROR";
    default: return "UNKNOWN";
    }
}

const char* WorkflowOrchestrator::event_name(WorkflowEvent event) const {
    switch (event) {
    case WorkflowEvent::SYSTEM_READY:     return "SYSTEM_READY";
    case WorkflowEvent::START_SCAN:       return "START_SCAN";
    case WorkflowEvent::PAUSE_SCAN:       return "PAUSE_SCAN";
    case WorkflowEvent::RESUME_SCAN:      return "RESUME_SCAN";
    case WorkflowEvent::STOP_SCAN:        return "STOP_SCAN";
    case WorkflowEvent::FRAME_CAPTURED:   return "FRAME_CAPTURED";
    case WorkflowEvent::REVIEW_COMPLETE:  return "REVIEW_COMPLETE";
    case WorkflowEvent::SAVE_COMPLETE:    return "SAVE_COMPLETE";
    case WorkflowEvent::FAULT_DETECTED:   return "FAULT_DETECTED";
    case WorkflowEvent::RECOVER:          return "RECOVER";
    default: return "UNKNOWN";
    }
}

/* Valid state transitions */
const WorkflowOrchestrator::Transition
WorkflowOrchestrator::kTransitions[] = {
    /* Normal scanning workflow */
    {WorkflowState::IDLE,      WorkflowEvent::SYSTEM_READY,    WorkflowState::READY},
    {WorkflowState::READY,     WorkflowEvent::START_SCAN,      WorkflowState::PREPARING},
    {WorkflowState::PREPARING, WorkflowEvent::FRAME_CAPTURED,  WorkflowState::SCANNING},
    {WorkflowState::SCANNING,  WorkflowEvent::PAUSE_SCAN,      WorkflowState::PAUSED},
    {WorkflowState::PAUSED,    WorkflowEvent::RESUME_SCAN,     WorkflowState::SCANNING},
    {WorkflowState::SCANNING,  WorkflowEvent::STOP_SCAN,       WorkflowState::REVIEWING},
    {WorkflowState::REVIEWING, WorkflowEvent::REVIEW_COMPLETE, WorkflowState::SAVING},
    {WorkflowState::SAVING,    WorkflowEvent::SAVE_COMPLETE,   WorkflowState::READY},

    /* Error handling */
    {WorkflowState::IDLE,      WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::READY,     WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::PREPARING, WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::SCANNING,  WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::PAUSED,    WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::REVIEWING, WorkflowEvent::FAULT_DETECTED,  WorkflowState::ERROR},
    {WorkflowState::ERROR,     WorkflowEvent::RECOVER,         WorkflowState::IDLE},
};

const size_t WorkflowOrchestrator::kNumTransitions =
    sizeof(kTransitions) / sizeof(kTransitions[0]);

WorkflowOrchestrator::WorkflowOrchestrator()
    : current_state_(WorkflowState::IDLE) {}

WorkflowState WorkflowOrchestrator::handle_event(WorkflowEvent event) {
    /* Find matching transition */
    for (size_t i = 0; i < kNumTransitions; i++) {
        if (kTransitions[i].from == current_state_ &&
            kTransitions[i].event == event) {
            WorkflowState old_state = current_state_;
            current_state_ = kTransitions[i].to;

            /* Fire callbacks */
            for (auto& cb : callbacks_) {
                if (cb) cb(old_state, current_state_);
            }

            return current_state_;
        }
    }

    /* No valid transition — stay in current state */
    return current_state_;
}

void WorkflowOrchestrator::on_transition(
    std::function<void(WorkflowState, WorkflowState)> cb) {
    callbacks_.push_back(std::move(cb));
}

} // namespace aspira
