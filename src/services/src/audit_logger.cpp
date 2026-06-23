/**
 * @file audit_logger.cpp
 * @brief Audit logger implementation
 */

#include "aspira/services/audit_logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace aspira {

static const char* action_names[] = {
    "USER_LOGIN", "USER_LOGOUT", "SCAN_STARTED", "SCAN_STOPPED",
    "STUDY_CREATED", "STUDY_MODIFIED", "STUDY_DELETED",
    "PATIENT_VIEWED", "PATIENT_MODIFIED", "CONFIG_CHANGED",
    "SYSTEM_STARTUP", "SYSTEM_SHUTDOWN", "SECURITY_VIOLATION",
    "PIPELINE_FAULT"
};

std::string AuditLogger::action_to_string(AuditAction action) {
    int idx = static_cast<int>(action);
    if (idx >= 0 && idx < (int)(sizeof(action_names)/sizeof(action_names[0]))) {
        return action_names[idx];
    }
    return "UNKNOWN";
}

AuditAction AuditLogger::string_to_action(const std::string& str) {
    for (int i = 0; i < (int)(sizeof(action_names)/sizeof(action_names[0])); i++) {
        if (str == action_names[i]) return static_cast<AuditAction>(i);
    }
    return AuditAction::SYSTEM_SHUTDOWN; // fallback
}

std::string AuditLogger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string AuditRecord::to_json() const {
    std::ostringstream oss;
    oss << "{\"id\":" << record_id
        << ",\"timestamp\":\"" << timestamp
        << "\",\"user\":\"" << username
        << "\",\"action\":\"" << AuditLogger::action_to_string(action)
        << "\",\"details\":\"" << details
        << "\",\"ip\":\"" << source_ip << "\"}";
    return oss.str();
}

AuditLogger::AuditLogger(const std::string& log_path)
    : log_path_(log_path) {
    file_.open(log_path_, std::ios::app);
    if (file_.is_open()) {
        AuditRecord startup;
        startup.record_id = next_id_++;
        startup.timestamp = get_timestamp();
        startup.username = "system";
        startup.action = AuditAction::SYSTEM_STARTUP;
        startup.details = "Audit log initialized";
        startup.source_ip = "local";
        file_ << startup.to_json() << "\n";
        file_.flush();
    }
}

AuditLogger::~AuditLogger() {
    if (file_.is_open()) {
        AuditRecord shutdown;
        shutdown.record_id = next_id_++;
        shutdown.timestamp = get_timestamp();
        shutdown.username = "system";
        shutdown.action = AuditAction::SYSTEM_SHUTDOWN;
        shutdown.details = "Audit log closed";
        shutdown.source_ip = "local";
        file_ << shutdown.to_json() << "\n";
        file_.flush();
        file_.close();
    }
}

void AuditLogger::log(const std::string& username, AuditAction action,
                       const std::string& details, const std::string& source_ip) {
    std::lock_guard<std::mutex> lock(mutex_);

    AuditRecord record;
    record.record_id = next_id_++;
    record.timestamp = get_timestamp();
    record.username = username;
    record.action = action;
    record.details = details;
    record.source_ip = source_ip;

    if (file_.is_open()) {
        file_ << record.to_json() << "\n";
        file_.flush();
    }
}

std::vector<AuditRecord> AuditLogger::read_all() const {
    std::vector<AuditRecord> records;
    std::ifstream in(log_path_);
    if (!in.is_open()) return records;

    std::string line;
    while (std::getline(in, line)) {
        // Simple JSON parsing (full parser not needed for controlled format)
        AuditRecord rec;
        // Extract fields from JSON line
        auto find_field = [&line](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = line.find(search);
            if (pos == std::string::npos) {
                // Try numeric field
                search = "\"" + key + "\":";
                pos = line.find(search);
                if (pos == std::string::npos) return "";
                pos += search.length();
                auto end = line.find_first_of(",}", pos);
                return line.substr(pos, end - pos);
            }
            pos += search.length();
            auto end = line.find('"', pos);
            return line.substr(pos, end - pos);
        };

        std::string id_str = find_field("id");
        if (!id_str.empty()) rec.record_id = std::stoull(id_str);
        rec.timestamp = find_field("timestamp");
        rec.username = find_field("user");
        rec.action = string_to_action(find_field("action"));
        rec.details = find_field("details");
        rec.source_ip = find_field("ip");

        records.push_back(rec);
    }
    return records;
}

std::vector<AuditRecord> AuditLogger::query_by_user(const std::string& username) const {
    auto all = read_all();
    std::vector<AuditRecord> result;
    for (const auto& r : all) {
        if (r.username == username) result.push_back(r);
    }
    return result;
}

std::vector<AuditRecord> AuditLogger::query_by_action(AuditAction action) const {
    auto all = read_all();
    std::vector<AuditRecord> result;
    for (const auto& r : all) {
        if (r.action == action) result.push_back(r);
    }
    return result;
}

} // namespace aspira
