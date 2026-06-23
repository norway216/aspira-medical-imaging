/**
 * @file security_manager.h
 * @brief Role-Based Access Control (RBAC) for medical imaging system
 *
 * Implements three user roles per the architecture:
 *   TECHNICIAN - operate scans, view studies/patients
 *   DOCTOR     - TECHNICIAN + create/modify studies, export data
 *   ADMIN      - full system access including user management
 */

#ifndef ASPIRA_SERVICES_SECURITY_MANAGER_H
#define ASPIRA_SERVICES_SECURITY_MANAGER_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aspira {

enum class UserRole {
    TECHNICIAN = 0,
    DOCTOR     = 1,
    ADMIN      = 2
};

enum class Permission {
    VIEW_STUDY,
    CREATE_STUDY,
    MODIFY_STUDY,
    DELETE_STUDY,
    START_SCAN,
    STOP_SCAN,
    CONFIGURE_SYSTEM,
    VIEW_PATIENT,
    MODIFY_PATIENT,
    VIEW_LOGS,
    MANAGE_USERS,
    EXPORT_DATA,

    /* Count */
    PERMISSION_COUNT
};

class SecurityManager {
public:
    SecurityManager();

    /**
     * @brief Authenticate a user with username and password hash
     * @return true if credentials are valid
     */
    bool authenticate(const std::string& username,
                      const std::string& password_hash);

    /**
     * @brief Check if current user has a specific permission
     */
    bool authorize(Permission permission) const;

    /**
     * @brief Check if current user has ALL specified permissions
     */
    bool authorize_all(const std::vector<Permission>& permissions) const;

    /**
     * @brief Check if current user has ANY of the specified permissions
     */
    bool authorize_any(const std::vector<Permission>& permissions) const;

    /**
     * @brief Get current user's role
     */
    UserRole current_role() const { return current_role_; }

    /**
     * @brief Get current username
     */
    const std::string& current_user() const { return current_user_; }

    /**
     * @brief Check if anyone is authenticated
     */
    bool is_authenticated() const { return authenticated_; }

    /**
     * @brief Logout current user
     */
    void logout();

    /**
     * @brief Add a user (admin only)
     */
    bool add_user(const std::string& username, const std::string& password_hash,
                  UserRole role);

    /**
     * @brief Remove a user (admin only)
     */
    bool remove_user(const std::string& username);

    /**
     * @brief Change a user's role (admin only)
     */
    bool change_role(const std::string& username, UserRole new_role);

    /**
     * @brief Get permissions for a specific role
     */
    static const std::unordered_set<Permission>&
    role_permissions(UserRole role);

private:
    struct UserInfo {
        std::string password_hash;
        UserRole role;
    };

    std::unordered_map<std::string, UserInfo> users_;
    UserRole current_role_ = UserRole::TECHNICIAN;
    std::string current_user_;
    bool authenticated_ = false;

    void init_default_users();
    static std::unordered_map<UserRole, std::unordered_set<Permission>>
    init_role_permissions();
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_SECURITY_MANAGER_H */
