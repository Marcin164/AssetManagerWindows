#include "lanventory/transport.hpp"
#include "lanventory/crypto.hpp"
#include "lanventory/log.hpp"
#include "lanventory/winhttp_client.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace lv::transport {
namespace {

std::string iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    ::gmtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
       << std::setw(3) << std::setfill('0') << ms << 'Z';
    return os.str();
}

}  // namespace

nlohmann::json send_scan(
    const config::AgentConfig& cfg,
    const config::AgentState& state,
    const nlohmann::json& payload) {

    // Backend's AgentGuard uses the HEX STRING of sha256(secret) as the
    // HMAC key (Node createHmac('sha256', secretHashHex)). Replicate
    // exactly.
    const std::string secret_hash_hex = crypto::sha256_hex(state.secret);
    const std::string body = payload.dump();
    const std::string timestamp = iso_timestamp();
    const std::string nonce = crypto::random_hex(16);
    const std::string msg = timestamp + "|" + nonce + "|" + body;
    const std::string signature = crypto::hmac_sha256_hex(secret_hash_hex, msg);

    http::Request req;
    req.method = "POST";
    req.url = cfg.backend_url + "/devices/agent/data";
    req.headers["Content-Type"]      = "application/json";
    req.headers["X-Device-Id"]       = state.device_id;
    req.headers["X-Timestamp"]       = timestamp;
    req.headers["X-Nonce"]           = nonce;
    req.headers["X-Signature"]       = signature;
    req.headers["X-Idempotency-Key"] = nonce;
    req.headers["User-Agent"]        = "LanVentoryAgent/0.1 (+windows)";
    req.body = body;
    req.verify_tls = cfg.verify_tls;
    req.timeout_seconds = cfg.timeout_seconds;

    log::info("transport",
        "POST /devices/agent/data (" + std::to_string(body.size()) + " bytes)");

    auto resp = http::send(req);
    if (resp.status >= 400) {
        throw std::runtime_error(
            "Backend rejected scan: HTTP " + std::to_string(resp.status) +
            " -- " + resp.body.substr(0, 500));
    }
    try {
        return nlohmann::json::parse(resp.body);
    } catch (...) {
        return nlohmann::json{{"raw", resp.body}};
    }
}

}  // namespace lv::transport
