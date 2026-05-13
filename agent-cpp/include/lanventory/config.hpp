#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace lv::config {

struct AgentConfig {
    std::string backend_url;
    std::string enrollment_token;
    int interval_minutes = 60;
    bool verify_tls = true;
    std::optional<std::string> ca_bundle;
    int timeout_seconds = 60;
    std::optional<std::string> log_path;
};

struct AgentState {
    std::string device_id;
    std::string secret;
};

std::filesystem::path default_config_path();
std::filesystem::path default_state_path();

AgentConfig load_config(const std::filesystem::path& path);
std::optional<AgentState> load_state(const std::filesystem::path& path);

void write_state(
    const std::filesystem::path& path,
    const std::string& device_id,
    const std::string& secret,
    bool matched,
    const std::vector<std::string>& match_reasons);

}  // namespace lv::config
