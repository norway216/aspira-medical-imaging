/**
 * @file logging_service.h
 * @brief Structured async JSON logging service
 */

#ifndef ASPIRA_SERVICES_LOGGING_SERVICE_H
#define ASPIRA_SERVICES_LOGGING_SERVICE_H

#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <queue>

namespace aspira {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERROR = 4,
    FATAL = 5,
};

class LoggingService {
public:
    explicit LoggingService(const std::string& log_path = "");
    ~LoggingService();

    LoggingService(const LoggingService&) = delete;
    LoggingService& operator=(const LoggingService&) = delete;

    void set_level(LogLevel level) { min_level_ = level; }
    void set_console_output(bool enable) { console_output_ = enable; }
    void set_file_output(bool enable) { file_output_ = enable; }
    void set_json_format(bool enable) { json_format_ = enable; }

    void trace(const std::string& msg, const std::string& module = "");
    void debug(const std::string& msg, const std::string& module = "");
    void info(const std::string& msg, const std::string& module = "");
    void warn(const std::string& msg, const std::string& module = "");
    void error(const std::string& msg, const std::string& module = "");
    void fatal(const std::string& msg, const std::string& module = "");

    void log(LogLevel level, const std::string& msg,
             const std::string& module = "");

private:
    std::string log_path_;
    std::ofstream file_;
    std::mutex mutex_;
    LogLevel min_level_ = LogLevel::INFO;
    bool console_output_ = true;
    bool file_output_ = true;
    bool json_format_ = true;

    static const char* level_to_string(LogLevel level);
    static const char* level_to_color(LogLevel level);
    std::string format_message(LogLevel level, const std::string& msg,
                               const std::string& module) const;
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_LOGGING_SERVICE_H */
