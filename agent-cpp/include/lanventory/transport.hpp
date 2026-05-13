#pragma once

#include <nlohmann/json.hpp>
#include "config.hpp"

namespace lv::transport {

nlohmann::json send_scan(
    const config::AgentConfig& cfg,
    const config::AgentState& state,
    const nlohmann::json& payload);

}  // namespace lv::transport
