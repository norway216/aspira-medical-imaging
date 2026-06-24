/**
 * @file security_manager.cpp
 * @brief RBAC security manager implementation
 */

#include "aspira/services/security_manager.h"

namespace aspira {

/* Static role-permission mapping */
std::unordered_map<UserRole, std::unordered_set<Permission>>
SecurityManager::init_role_permissions() {
    std::unordered_map<UserRole, std::unordered_set<Permission>> map;

    /* Technician */
    map[UserRole::TECHNICIAN] = {
        Permission::VIEW_STUDY,
        Permission::START_SCAN,
        Permission::STOP_SCAN,
        Permission::VIEW_PATIENT,
        Permission::RUN_AI_SEGMENTATION,
    };

    /* Doctor */
    map[UserRole::DOCTOR] = {
        Permission::VIEW_STUDY,
        Permission::CREATE_STUDY,
        Permission::MODIFY_STUDY,
        Permission::START_SCAN,
        Permission::STOP_SCAN,
        Permission::VIEW_PATIENT,
        Permission::MODIFY_PATIENT,
        Permission::EXPORT_DATA,
    };

    /* Admin — all permissions */
    std::unordered_set<Permission> admin_perms;
    for (int i = 0; i < static_cast<int>(Permission::PERMISSION_COUNT); i++) {
        admin_perms.insert(static_cast<Permission>(i));
    }
    map[UserRole::ADMIN] = std::move(admin_perms);

    return map;
}

const std::unordered_set<Permission>&
SecurityManager::role_permissions(UserRole role) {
    static const auto map = init_role_permissions();
    static const std::unordered_set<Permission> empty_set;
    auto it = map.find(role);
    return it != map.end() ? it->second : empty_set;
}

SecurityManager::SecurityManager() {
    init_default_users();
}

void SecurityManager::init_default_users() {
    /* Default users for demo purposes.
     * In production, passwords would be stored as salted hashes. */
    users_["admin"]       = {"admin_hash",       UserRole::ADMIN};
    users_["doctor"]      = {"doctor_hash",      UserRole::DOCTOR};
    users_["technician"]  = {"technician_hash",  UserRole::TECHNICIAN};
}

bool SecurityManager::authenticate(const std::string& username,
                                    const std::string& password_hash) {
    auto it = users_.find(username);
    if (it == users_.end()) {
        authenticated_ = false;
        return false;
    }

    if (it->second.password_hash != password_hash) {
        authenticated_ = false;
        return false;
    }

    current_user_ = username;
    current_role_ = it->second.role;
    authenticated_ = true;
    return true;
}

bool SecurityManager::authorize(Permission permission) const {
    if (!authenticated_) return false;

    const auto& perms = role_permissions(current_role_);
    return perms.find(permission) != perms.end();
}

bool SecurityManager::authorize_all(
    const std::vector<Permission>& permissions) const {
    if (!authenticated_) return false;

    const auto& perms = role_permissions(current_role_);
    for (auto p : permissions) {
        if (perms.find(p) == perms.end()) return false;
    }
    return true;
}

bool SecurityManager::authorize_any(
    const std::vector<Permission>& permissions) const {
    if (!authenticated_) return false;

    const auto& perms = role_permissions(current_role_);
    for (auto p : permissions) {
        if (perms.find(p) != perms.end()) return true;
    }
    return false;
}

void SecurityManager::logout() {
    authenticated_ = false;
    current_user_.clear();
    current_role_ = UserRole::TECHNICIAN;
}

bool SecurityManager::add_user(const std::string& username,
                                const std::string& password_hash,
                                UserRole role) {
    /* Only admin can add users */
    if (!authorize(Permission::MANAGE_USERS)) return false;

    if (users_.find(username) != users_.end()) return false;

    users_[username] = {password_hash, role};
    return true;
}

bool SecurityManager::remove_user(const std::string& username) {
    if (!authorize(Permission::MANAGE_USERS)) return false;

    auto it = users_.find(username);
    if (it == users_.end()) return false;

    /* Cannot remove yourself */
    if (username == current_user_) return false;

    users_.erase(it);
    return true;
}

bool SecurityManager::change_role(const std::string& username,
                                   UserRole new_role) {
    if (!authorize(Permission::MANAGE_USERS)) return false;

    auto it = users_.find(username);
    if (it == users_.end()) return false;

    it->second.role = new_role;
    return true;
}

} // namespace aspira
