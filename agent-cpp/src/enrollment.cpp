#include "lanventory/enrollment.hpp"
#include "lanventory/fingerprint.hpp"
#include "lanventory/log.hpp"
#include "lanventory/winhttp_client.hpp"

#include <stdexcept>

namespace lv::enrollment {

Result enroll(const config::AgentConfig& cfg) {
    auto payload = fingerprint::collect("0.1.0");
    log::info("enroll",
        "Submitting fingerprint (tpm=" + std::string(payload.contains("tpmFingerprint") ? "yes" : "no")
        + " cpu=" + std::string(payload.contains("cpuId") ? "yes" : "no")
        + " macs=" + std::to_string(payload.value("macAddresses", nlohmann::json::array()).size())
        + ")");

    http::Request req;
    req.method = "POST";
    req.url = cfg.backend_url + "/devices/agent/enroll";
    req.headers["Content-Type"] = "application/json";
    req.headers["X-Enrollment-Token"] = cfg.enrollment_token;
    req.headers["User-Agent"] = "LanVentoryAgent/0.1 (+windows)";
    req.body = payload.dump();
    req.verify_tls = cfg.verify_tls;
    req.timeout_seconds = cfg.timeout_seconds;

    auto resp = http::send(req);
    if (resp.status >= 400) {
        throw std::runtime_error(
            "Enrollment failed: HTTP " + std::to_string(resp.status) +
            " -- " + resp.body.substr(0, 500));
    }

    auto data = nlohmann::json::parse(resp.body);
    Result out;
    out.device_id = data.value("deviceId", "");
    out.secret    = data.value("secret", "");
    out.matched   = data.value("matched", false);
    if (data.contains("matchReasons") && data["matchReasons"].is_array()) {
        for (auto& r : data["matchReasons"]) {
            if (r.is_string()) out.match_reasons.push_back(r.get<std::string>());
        }
    }
    if (out.device_id.empty() || out.secret.empty()) {
        throw std::runtime_error("Enrollment response missing deviceId/secret");
    }
    log::info("enroll",
        "Enrolled as " + out.device_id + " (matched=" +
        (out.matched ? "true" : "false") + ")");
    return out;
}

}  // namespace lv::enrollment
