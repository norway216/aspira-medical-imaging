/**
 * @file test_workflow_orchestrator.cpp
 * @brief Unit tests for workflow state machine
 */

#include <aspira/services/workflow_orchestrator.h>

#include <catch2/catch_test_macros.hpp>

using namespace aspira;

TEST_CASE("Workflow orchestrator state transitions", "[services][workflow]") {
    WorkflowOrchestrator wf;

    SECTION("initial state is IDLE") {
        REQUIRE(wf.current_state() == WorkflowState::IDLE);
    }

    SECTION("valid transitions") {
        REQUIRE(wf.handle_event(WorkflowEvent::SYSTEM_READY) == WorkflowState::READY);
        REQUIRE(wf.handle_event(WorkflowEvent::START_SCAN) == WorkflowState::PREPARING);
        REQUIRE(wf.handle_event(WorkflowEvent::FRAME_CAPTURED) == WorkflowState::SCANNING);
        REQUIRE(wf.handle_event(WorkflowEvent::STOP_SCAN) == WorkflowState::ANALYZING);
        REQUIRE(wf.handle_event(WorkflowEvent::AI_ANALYSIS_COMPLETE) == WorkflowState::REVIEWING);
        REQUIRE(wf.handle_event(WorkflowEvent::REVIEW_COMPLETE) == WorkflowState::SAVING);
        REQUIRE(wf.handle_event(WorkflowEvent::SAVE_COMPLETE) == WorkflowState::READY);
    }

    SECTION("pause and resume") {
        wf.handle_event(WorkflowEvent::SYSTEM_READY);
        wf.handle_event(WorkflowEvent::START_SCAN);
        wf.handle_event(WorkflowEvent::FRAME_CAPTURED);

        REQUIRE(wf.handle_event(WorkflowEvent::PAUSE_SCAN) == WorkflowState::PAUSED);
        REQUIRE(wf.handle_event(WorkflowEvent::RESUME_SCAN) == WorkflowState::SCANNING);
    }

    SECTION("invalid transitions are ignored") {
        /* Cannot start scan from IDLE */
        REQUIRE(wf.handle_event(WorkflowEvent::START_SCAN) == WorkflowState::IDLE);

        /* Cannot save from IDLE */
        REQUIRE(wf.handle_event(WorkflowEvent::SAVE_COMPLETE) == WorkflowState::IDLE);
    }

    SECTION("fault recovery") {
        wf.handle_event(WorkflowEvent::SYSTEM_READY);
        wf.handle_event(WorkflowEvent::START_SCAN);
        REQUIRE(wf.handle_event(WorkflowEvent::FAULT_DETECTED) == WorkflowState::ERROR);
        REQUIRE(wf.handle_event(WorkflowEvent::RECOVER) == WorkflowState::IDLE);
    }

    SECTION("fault from any state") {
        REQUIRE(wf.handle_event(WorkflowEvent::FAULT_DETECTED) == WorkflowState::ERROR);
    }
}

TEST_CASE("Workflow orchestrator callbacks", "[services][workflow]") {
    WorkflowOrchestrator wf;

    int callback_count = 0;
    WorkflowState last_from = WorkflowState::IDLE;
    WorkflowState last_to = WorkflowState::IDLE;

    wf.on_transition([&](WorkflowState from, WorkflowState to) {
        callback_count++;
        last_from = from;
        last_to = to;
    });

    wf.handle_event(WorkflowEvent::SYSTEM_READY);

    REQUIRE(callback_count == 1);
    REQUIRE(last_from == WorkflowState::IDLE);
    REQUIRE(last_to == WorkflowState::READY);

    /* Invalid transition should not fire callback */
    wf.handle_event(WorkflowEvent::SAVE_COMPLETE);
    REQUIRE(callback_count == 1);  /* Still 1 */
}

TEST_CASE("Workflow orchestrator state names", "[services][workflow]") {
    WorkflowOrchestrator wf;

    REQUIRE(std::string(wf.state_name()) == "IDLE");
    wf.handle_event(WorkflowEvent::SYSTEM_READY);
    REQUIRE(std::string(wf.state_name()) == "READY");
}
