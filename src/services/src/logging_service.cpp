/**
 * @file logging_service.cpp
 * @brief Structured logging service implementation
 */

#include "aspira/services/logging_service.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace aspira {

const char* LoggingService::level_to_string(LogLevel level) {
    switch (level) {
    case LogLevel::TRACE: return "TRACE";
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO";
    case LogLevel::WARN:  return "WARN";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
    default: return "UNKNOWN";
    }
}

const char* LoggingService::level_to_color(LogLevel level) {
    switch (level) {
    case LogLevel::TRACE: return "\033[37m";  /* White */
    case LogLevel::DEBUG: return "\033[36m";  /* Cyan */
    case LogLevel::INFO:  return "\033[32m";  /* Green */
    case LogLevel::WARN:  return "\033[33m";  /* Yellow */
    case LogLevel::ERROR: return "\033[31m";  /* Red */
    case LogLevel::FATAL: return "\033[1;31m"; /* Bold Red */
    default: return "\033[0m";
    }
}

LoggingService::LoggingService(const std::string& log_path)
    : log_path_(log_path) {
    if (!log_path_.empty() && file_output_) {
        file_.open(log_path_, std::ios::app);
    }
}

LoggingService::~LoggingService() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::string LoggingService::format_message(LogLevel level,
                                            const std::string& msg,
                                            const std::string& module) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    if (json_format_) {
        oss << "{\"ts\":\"";
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z\"";
        oss << ",\"level\":\"" << level_to_string(level) << "\"";
        if (!module.empty()) {
            oss << ",\"module\":\"" << module << "\"";
        }
        oss << ",\"msg\":\"" << msg << "\"}";
    } else {
        oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        oss << " [" << level_to_string(level) << "]";
        if (!module.empty()) {
            oss << " [" << module << "]";
        }
        oss << " " << msg;
    }
    return oss.str();
}

void LoggingService::log(LogLevel level, const std::string& msg,
                          const std::string& module) {
    if (level < min_level_) return;

    std::string formatted = format_message(level, msg, module);

    std::lock_guard<std::mutex> lock(mutex_);

    if (console_output_) {
        if (json_format_) {
            std::cout << formatted << std::endl;
        } else {
            std::cout << level_to_color(level) << formatted
                      << "\033[0m" << std::endl;
        }
    }

    if (file_output_ && file_.is_open()) {
        file_ << formatted << "\n";
        file_.flush();
    }
}

void LoggingService::trace(const std::string& msg, const std::string& module) {
    log(LogLevel::TRACE, msg, module);
}
void LoggingService::debug(const std::string& msg, const std::string& module) {
    log(LogLevel::DEBUG, msg, module);
}
void LoggingService::info(const std::string& msg, const std::string& module) {
    log(LogLevel::INFO, msg, module);
}
void LoggingService::warn(const std::string& msg, const std::string& module) {
    log(LogLevel::WARN, msg, module);
}
void LoggingService::error(const std::string& msg, const std::string& module) {
    log(LogLevel::ERROR, msg, module);
}
void LoggingService::fatal(const std::string& msg, const std::string& module) {
    log(LogLevel::FATAL, msg, module);
}

} // namespace aspira
