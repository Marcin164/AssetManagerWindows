#pragma once

#include <nlohmann/json.hpp>

namespace lv::scanner {

nlohmann::json collect_system();
nlohmann::json collect_hardware();
nlohmann::json collect_software();
nlohmann::json collect_network();
nlohmann::json collect_security();
nlohmann::json collect_peripherals();
nlohmann::json collect_events();
nlohmann::json collect_users();

}  // namespace lv::scanner
