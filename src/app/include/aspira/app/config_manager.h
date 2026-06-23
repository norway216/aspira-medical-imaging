/**
 * @file config_manager.h
 * @brief JSON-based configuration manager
 */

#ifndef ASPIRA_APP_CONFIG_MANAGER_H
#define ASPIRA_APP_CONFIG_MANAGER_H

#include <cstdint>
#include <string>

#include <aspira/app/data_generator.h>

namespace aspira {

struct PipelineConfig {
    uint64_t acquisition_queue_size = 64;
    uint64_t render_queue_size = 64;
    uint64_t frame_pool_size = 64;
    size_t   thread_pool_size = 4;
    uint64_t watchdog_timeout_ms = 500;
    float    gain_db = 30.0f;
    bool     enable_envelope = true;
    bool     enable_dc_remove = true;
    bool     enable_beamform = false;
};

struct LoggingConfig {
    std::string level = "info";
    bool console_output = true;
    bool file_output = true;
    std::string file_path = "/tmp/aspira.log";
    bool json_format = true;
};

struct SimulationConfig {
    float noise_level = 0.05f;
    uint32_t num_targets = 3;
};

class ConfigManager {
public:
    ConfigManager();

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    const SimulatedProbeConfig& probe_config() const { return probe_config_; }
    void set_probe_config(const SimulatedProbeConfig& cfg) { probe_config_ = cfg; }

    const PipelineConfig& pipeline_config() const { return pipeline_config_; }
    void set_pipeline_config(const PipelineConfig& cfg) { pipeline_config_ = cfg; }

    const LoggingConfig& logging_config() const { return logging_config_; }
    void set_logging_config(const LoggingConfig& cfg) { logging_config_ = cfg; }

    const SimulationConfig& sim_config() const { return sim_config_; }
    void set_sim_config(const SimulationConfig& cfg) { sim_config_ = cfg; }

private:
    SimulatedProbeConfig probe_config_;
    PipelineConfig pipeline_config_;
    LoggingConfig logging_config_;
    SimulationConfig sim_config_;
};

} // namespace aspira

#endif /* ASPIRA_APP_CONFIG_MANAGER_H */
