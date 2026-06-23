/**
 * @file config_manager.cpp
 * @brief Configuration manager implementation with simple JSON parsing
 */

#include "aspira/app/config_manager.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace aspira {

/* Simple JSON value extractor (no external dependency) */
static std::string extract_json_string(const std::string& json,
                                        const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();
    /* Skip whitespace */
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos < json.length() && json[pos] == '"') {
        /* String value */
        pos++;
        size_t end = json.find('"', pos);
        if (end != std::string::npos) {
            return json.substr(pos, end - pos);
        }
    }
    return "";
}

static float extract_json_float(const std::string& json,
                                 const std::string& key, float default_val = 0.0f) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    size_t end = json.find_first_of(",}\n\r \t", pos);
    std::string val = json.substr(pos, end - pos);
    return strtof(val.c_str(), nullptr);
}

static int extract_json_int(const std::string& json,
                             const std::string& key, int default_val = 0) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    size_t end = json.find_first_of(",}\n\r \t", pos);
    std::string val = json.substr(pos, end - pos);
    return atoi(val.c_str());
}

static bool extract_json_bool(const std::string& json,
                               const std::string& key, bool default_val = false) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return default_val;
}

ConfigManager::ConfigManager() {
    /* Set sensible defaults */
    probe_config_.num_elements = 64;
    probe_config_.frequency_hz = 5.0e6f;
    probe_config_.sampling_rate_hz = 40.0e6f;
    probe_config_.sound_speed_mps = 1540.0f;
    probe_config_.pitch_mm = 0.3f;
    probe_config_.samples_per_line = 2048;
    probe_config_.num_lines = 128;
    probe_config_.frame_rate = 30.0f;
    probe_config_.noise_level = 0.05f;
}

bool ConfigManager::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Return true with defaults if file doesn't exist
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    /* Parse probe section */
    std::string probe_str = "\"probe\"";
    size_t probe_pos = json.find(probe_str);
    if (probe_pos != std::string::npos) {
        std::string sub = json.substr(probe_pos);
        probe_config_.num_elements = (uint32_t)extract_json_int(sub, "num_elements", 64);
        probe_config_.frequency_hz = extract_json_float(sub, "center_frequency_hz", 5.0e6f);
        probe_config_.sampling_rate_hz = extract_json_float(sub, "sampling_rate_hz", 40.0e6f);
        probe_config_.sound_speed_mps = extract_json_float(sub, "sound_speed_mps", 1540.0f);
        probe_config_.pitch_mm = extract_json_float(sub, "pitch_mm", 0.3f);
        probe_config_.samples_per_line = (uint32_t)extract_json_int(sub, "samples_per_line", 2048);
        probe_config_.num_lines = (uint32_t)extract_json_int(sub, "num_lines", 128);
        probe_config_.frame_rate = extract_json_float(sub, "frame_rate", 30.0f);
    }

    /* Parse pipeline section */
    size_t pipe_pos = json.find("\"pipeline\"");
    if (pipe_pos != std::string::npos) {
        std::string sub = json.substr(pipe_pos);
        pipeline_config_.acquisition_queue_size = (uint64_t)extract_json_int(sub, "acquisition_queue_size", 64);
        pipeline_config_.render_queue_size = (uint64_t)extract_json_int(sub, "render_queue_size", 64);
        pipeline_config_.frame_pool_size = (uint64_t)extract_json_int(sub, "frame_pool_size", 64);
        pipeline_config_.thread_pool_size = (size_t)extract_json_int(sub, "thread_pool_size", 4);
        pipeline_config_.watchdog_timeout_ms = (uint64_t)extract_json_int(sub, "watchdog_timeout_ms", 500);
        pipeline_config_.gain_db = extract_json_float(sub, "gain_db", 30.0f);
        pipeline_config_.enable_envelope = extract_json_bool(sub, "enable_envelope", true);
        pipeline_config_.enable_dc_remove = extract_json_bool(sub, "enable_dc_remove", true);
        pipeline_config_.enable_beamform = extract_json_bool(sub, "enable_beamform", false);
    }

    /* Parse logging section */
    size_t log_pos = json.find("\"logging\"");
    if (log_pos != std::string::npos) {
        std::string sub = json.substr(log_pos);
        logging_config_.level = extract_json_string(sub, "level");
        if (logging_config_.level.empty()) logging_config_.level = "info";
        logging_config_.console_output = extract_json_bool(sub, "console_output", true);
        logging_config_.file_output = extract_json_bool(sub, "file_output", true);
        logging_config_.file_path = extract_json_string(sub, "file_path");
        if (logging_config_.file_path.empty()) logging_config_.file_path = "/tmp/aspira.log";
        logging_config_.json_format = extract_json_bool(sub, "json_format", true);
    }

    /* Parse simulation section */
    size_t sim_pos = json.find("\"simulation\"");
    if (sim_pos != std::string::npos) {
        std::string sub = json.substr(sim_pos);
        sim_config_.noise_level = extract_json_float(sub, "noise_level", 0.05f);
        sim_config_.num_targets = (uint32_t)extract_json_int(sub, "num_targets", 3);
    }

    return true;
}

bool ConfigManager::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"probe\": {\n";
    file << "    \"num_elements\": " << probe_config_.num_elements << ",\n";
    file << "    \"center_frequency_hz\": " << probe_config_.frequency_hz << ",\n";
    file << "    \"sampling_rate_hz\": " << probe_config_.sampling_rate_hz << ",\n";
    file << "    \"sound_speed_mps\": " << probe_config_.sound_speed_mps << ",\n";
    file << "    \"pitch_mm\": " << probe_config_.pitch_mm << ",\n";
    file << "    \"samples_per_line\": " << probe_config_.samples_per_line << ",\n";
    file << "    \"num_lines\": " << probe_config_.num_lines << ",\n";
    file << "    \"frame_rate\": " << probe_config_.frame_rate << "\n";
    file << "  },\n";
    file << "  \"pipeline\": {\n";
    file << "    \"acquisition_queue_size\": " << pipeline_config_.acquisition_queue_size << ",\n";
    file << "    \"render_queue_size\": " << pipeline_config_.render_queue_size << ",\n";
    file << "    \"frame_pool_size\": " << pipeline_config_.frame_pool_size << ",\n";
    file << "    \"thread_pool_size\": " << pipeline_config_.thread_pool_size << ",\n";
    file << "    \"watchdog_timeout_ms\": " << pipeline_config_.watchdog_timeout_ms << "\n";
    file << "  },\n";
    file << "  \"logging\": {\n";
    file << "    \"level\": \"" << logging_config_.level << "\",\n";
    file << "    \"console_output\": " << (logging_config_.console_output ? "true" : "false") << ",\n";
    file << "    \"file_output\": " << (logging_config_.file_output ? "true" : "false") << ",\n";
    file << "    \"file_path\": \"" << logging_config_.file_path << "\",\n";
    file << "    \"json_format\": " << (logging_config_.json_format ? "true" : "false") << "\n";
    file << "  }\n";
    file << "}\n";

    return true;
}

} // namespace aspira
