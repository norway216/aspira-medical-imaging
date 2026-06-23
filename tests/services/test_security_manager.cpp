/**
 * @file test_security_manager.cpp
 * @brief Unit tests for RBAC security manager
 */

#include <aspira/services/security_manager.h>

#include <catch2/catch_test_macros.hpp>

using namespace aspira;

TEST_CASE("Security manager authentication", "[services][security]") {
    SecurityManager sm;

    SECTION("default users exist") {
        REQUIRE(sm.authenticate("admin", "admin_hash"));
        REQUIRE(sm.current_role() == UserRole::ADMIN);
        sm.logout();
    }

    SECTION("technician login") {
        REQUIRE(sm.authenticate("technician", "technician_hash"));
        REQUIRE(sm.current_role() == UserRole::TECHNICIAN);
        REQUIRE(sm.is_authenticated());
    }

    SECTION("doctor login") {
        REQUIRE(sm.authenticate("doctor", "doctor_hash"));
        REQUIRE(sm.current_role() == UserRole::DOCTOR);
    }

    SECTION("invalid credentials") {
        REQUIRE_FALSE(sm.authenticate("technician", "wrong_hash"));
        REQUIRE_FALSE(sm.is_authenticated());
    }

    SECTION("unknown user") {
        REQUIRE_FALSE(sm.authenticate("nonexistent", "hash"));
    }
}

TEST_CASE("Security manager authorization", "[services][security]") {
    SecurityManager sm;

    SECTION("technician permissions") {
        sm.authenticate("technician", "technician_hash");

        REQUIRE(sm.authorize(Permission::VIEW_STUDY));
        REQUIRE(sm.authorize(Permission::START_SCAN));
        REQUIRE(sm.authorize(Permission::STOP_SCAN));
        REQUIRE(sm.authorize(Permission::VIEW_PATIENT));

        /* Technician should NOT have admin permissions */
        REQUIRE_FALSE(sm.authorize(Permission::MANAGE_USERS));
        REQUIRE_FALSE(sm.authorize(Permission::CONFIGURE_SYSTEM));
        REQUIRE_FALSE(sm.authorize(Permission::DELETE_STUDY));
    }

    SECTION("doctor permissions") {
        sm.authenticate("doctor", "doctor_hash");

        REQUIRE(sm.authorize(Permission::CREATE_STUDY));
        REQUIRE(sm.authorize(Permission::MODIFY_STUDY));
        REQUIRE(sm.authorize(Permission::MODIFY_PATIENT));
        REQUIRE(sm.authorize(Permission::EXPORT_DATA));

        /* Doctor should NOT have admin permissions */
        REQUIRE_FALSE(sm.authorize(Permission::MANAGE_USERS));
    }

    SECTION("admin has all permissions") {
        sm.authenticate("admin", "admin_hash");

        REQUIRE(sm.authorize(Permission::MANAGE_USERS));
        REQUIRE(sm.authorize(Permission::CONFIGURE_SYSTEM));
        REQUIRE(sm.authorize(Permission::VIEW_LOGS));
        REQUIRE(sm.authorize(Permission::DELETE_STUDY));
    }

    SECTION("not authenticated = no permissions") {
        REQUIRE_FALSE(sm.authorize(Permission::VIEW_STUDY));
    }
}

TEST_CASE("Security manager user management", "[services][security]") {
    SecurityManager sm;
    sm.authenticate("admin", "admin_hash");

    SECTION("add and remove user") {
        REQUIRE(sm.add_user("new_tech", "hash123", UserRole::TECHNICIAN));
        REQUIRE(sm.authenticate("new_tech", "hash123"));

        sm.authenticate("admin", "admin_hash");
        REQUIRE(sm.remove_user("new_tech"));
        REQUIRE_FALSE(sm.authenticate("new_tech", "hash123"));
    }

    SECTION("change role") {
        REQUIRE(sm.add_user("test_user", "hash", UserRole::TECHNICIAN));
        REQUIRE(sm.change_role("test_user", UserRole::DOCTOR));

        sm.authenticate("test_user", "hash");
        REQUIRE(sm.current_role() == UserRole::DOCTOR);
        REQUIRE(sm.authorize(Permission::CREATE_STUDY));
    }
}
