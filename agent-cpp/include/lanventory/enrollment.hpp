#pragma once

#include <nlohmann/json.hpp>
#include "config.hpp"

namespace lv::enrollment {

struct Result {
    std::string device_id;
    std::string secret;
    bool matched = false;
    std::vector<std::string> match_reasons;
};

Result enroll(const config::AgentConfig& cfg);

}  // namespace lv::enrollment
