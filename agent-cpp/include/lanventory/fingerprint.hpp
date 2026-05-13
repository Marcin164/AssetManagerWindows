#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace lv::fingerprint {

// Build the enrollment fingerprint payload: TPM EK hash, MAC list, CPU
// id, baseboard serial, hostname, manufacturer, model. Matches the
// shape consumed by backend AgentEnrollDto.
nlohmann::json collect(std::string_view agent_version);

}  // namespace lv::fingerprint
