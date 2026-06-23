/**
 * @file audit_logger.h
 * @brief Immutable append-only audit log for medical compliance
 *
 * Audit records are written in append-only mode with sequential record
 * IDs. Records are structured as JSON for machine readability.
 */

#ifndef ASPIRA_SERVICES_AUDIT_LOGGER_H
#define ASPIRA_SERVICES_AUDIT_LOGGER_H

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace aspira {

enum class AuditAction {
    USER_LOGIN,
    USER_LOGOUT,
    SCAN_STARTED,
    SCAN_STOPPED,
    STUDY_CREATED,
    STUDY_MODIFIED,
    STUDY_DELETED,
    PATIENT_VIEWED,
    PATIENT_MODIFIED,
    CONFIG_CHANGED,
    SYSTEM_STARTUP,
    SYSTEM_SHUTDOWN,
    SECURITY_VIOLATION,
    PIPELINE_FAULT,
};

struct AuditRecord {
    uint64_t    record_id;
    std::string timestamp;      /* ISO 8601 */
    std::string username;
    AuditAction action;
    std::string details;
    std::string source_ip;

    std::string to_json() const;
};

class AuditLogger {
public:
    explicit AuditLogger(const std::string& log_path);
    ~AuditLogger();

    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;

    /**
     * @brief Log an audit event
     */
    void log(const std::string& username, AuditAction action,
             const std::string& details = "",
             const std::string& source_ip = "local");

    /**
     * @brief Read all audit records from the log file
     */
    std::vector<AuditRecord> read_all() const;

    /**
     * @brief Query records by username
     */
    std::vector<AuditRecord> query_by_user(const std::string& username) const;

    /**
     * @brief Query records by action type
     */
    std::vector<AuditRecord> query_by_action(AuditAction action) const;

    /**
     * @brief Get number of logged records
     */
    uint64_t record_count() const { return next_id_ - 1; }

    static std::string action_to_string(AuditAction action);
    static AuditAction string_to_action(const std::string& str);

private:
    std::string log_path_;
    std::ofstream file_;
    std::mutex mutex_;
    uint64_t next_id_ = 1;

    static std::string get_timestamp();
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_AUDIT_LOGGER_H */
