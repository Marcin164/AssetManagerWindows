#include "lanventory/config.hpp"
#include "lanventory/dpapi.hpp"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace lv::config {
namespace {

std::filesystem::path program_data_dir() {
    const char* pd = std::getenv("PROGRAMDATA");
    std::filesystem::path base = pd ? pd : R"(C:\ProgramData)";
    return base / "LanVentory" / "agent";
}

}  // namespace

std::filesystem::path default_config_path() { return program_data_dir() / "config.json"; }
std::filesystem::path default_state_path()  { return program_data_dir() / "state.json"; }

AgentConfig load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "Config not found at " + path.string() +
            ". Run the configurator first.");
    }
    std::ifstream in(path);
    nlohmann::json raw;
    in >> raw;
    in.close();

    AgentConfig cfg;
    cfg.backend_url = raw.value("backend_url", "");

    // Self-upgrade: when the installer drops a baked-in plaintext token
    // (no ``dpapi:`` prefix), encrypt it in place on first read so the
    // on-disk copy matches what later reads expect. Idempotent --
    // already-encrypted tokens fall through unchanged.
    const auto stored_token = raw.value("enrollment_token", std::string{});
    cfg.enrollment_token = dpapi::unprotect_token(stored_token);
    if (!stored_token.empty()
        && stored_token.compare(0, 6, "dpapi:") != 0) {
        try {
            raw["enrollment_token"] = dpapi::protect_to_token(stored_token);
            std::ofstream out(path);
            out << raw.dump(2);
        } catch (...) {
            // Best-effort -- plaintext on disk still works at runtime.
        }
    }
    cfg.interval_minutes = raw.value("interval_minutes", 60);
    cfg.verify_tls = raw.value("verify_tls", true);
    if (raw.contains("ca_bundle") && raw["ca_bundle"].is_string()
        && !raw["ca_bundle"].get<std::string>().empty()) {
        cfg.ca_bundle = raw["ca_bundle"].get<std::string>();
    }
    cfg.timeout_seconds = raw.value("timeout_seconds", 60);
    if (raw.contains("log_path") && raw["log_path"].is_string()
        && !raw["log_path"].get<std::string>().empty()) {
        cfg.log_path = raw["log_path"].get<std::string>();
    }

    if (cfg.backend_url.empty() || cfg.enrollment_token.empty()) {
        throw std::runtime_error("Config missing backend_url or enrollment_token");
    }
    if (!cfg.backend_url.empty() && cfg.backend_url.back() == '/') {
        cfg.backend_url.pop_back();
    }
    return cfg;
}

std::optional<AgentState> load_state(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return std::nullopt;
    std::ifstream in(path);
    nlohmann::json raw;
    try {
        in >> raw;
    } catch (...) {
        return std::nullopt;
    }
    auto device_id = raw.value("device_id", "");
    auto secret_token = raw.value("secret", "");
    if (device_id.empty() || secret_token.empty()) return std::nullopt;
    try {
        AgentState s;
        s.device_id = device_id;
        s.secret = dpapi::unprotect_token(secret_token);
        return s;
    } catch (...) {
        // Blob unreadable (host clone, partial write) -- force re-enroll.
        return std::nullopt;
    }
}

void write_state(
    const std::filesystem::path& path,
    const std::string& device_id,
    const std::string& secret,
    bool matched,
    const std::vector<std::string>& match_reasons) {

    std::filesystem::create_directories(path.parent_path());

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    ::gmtime_s(&tm, &t);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");

    nlohmann::json out = {
        {"device_id",     device_id},
        {"secret",        dpapi::protect_to_token(secret)},
        {"matched",       matched},
        {"match_reasons", match_reasons},
        {"enrolled_at",   ts.str()},
    };
    std::ofstream f(path);
    f << out.dump(2);
}

}  // namespace lv::config
