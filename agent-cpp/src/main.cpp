#include "lanventory/config.hpp"
#include "lanventory/dpapi.hpp"
#include "lanventory/enrollment.hpp"
#include "lanventory/log.hpp"
#include "lanventory/scanner.hpp"
#include "lanventory/task_scheduler.hpp"
#include "lanventory/transport.hpp"
#include "lanventory/wmi.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Args {
    bool once = false;
    bool watch = false;
    bool dry_run = false;
    bool enroll_only = false;
    bool force_enroll = false;
    bool encrypt_stdin = false;
    bool register_task = false;
    bool unregister_task = false;
    bool verbose = false;
    int  interval = 60;
    std::filesystem::path config = lv::config::default_config_path();
    std::filesystem::path state  = lv::config::default_state_path();
    std::vector<std::string> sections = {
        "system", "hardware", "software", "network",
        "security", "peripherals", "events", "users_and_groups",
    };
};

std::vector<std::string> split_csv(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void usage() {
    std::cerr <<
R"(Usage: lanventory-agent [options]
  --once               Run a single scan and exit (used by Task Scheduler)
  --watch              Loop forever using interval_minutes from config
  --dry-run            Print payload to stdout, do not POST
  --enroll-only        Perform enrollment and exit
  --force-enroll       Discard existing state and request a fresh device id + secret
  --register-task      Register the LanVentoryAgent scheduled task
  --unregister-task    Remove the LanVentoryAgent scheduled task
  --interval N         Interval in minutes for --register-task (default 60)
  --encrypt-stdin      Read stdin, print its DPAPI-encrypted form
  --sections a,b,c     Limit collection to these sections
  --config PATH        Override config.json location
  --state  PATH        Override state.json location
  --verbose            Verbose logging
)";
}

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << s << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (s == "--once")            a.once = true;
        else if (s == "--watch")           a.watch = true;
        else if (s == "--dry-run")         a.dry_run = true;
        else if (s == "--enroll-only")     a.enroll_only = true;
        else if (s == "--force-enroll")    a.force_enroll = true;
        else if (s == "--encrypt-stdin")   a.encrypt_stdin = true;
        else if (s == "--register-task")   a.register_task = true;
        else if (s == "--unregister-task") a.unregister_task = true;
        else if (s == "--verbose")         a.verbose = true;
        else if (s == "--interval")        a.interval = std::stoi(next());
        else if (s == "--config")          a.config = next();
        else if (s == "--state")           a.state = next();
        else if (s == "--sections")        a.sections = split_csv(next());
        else if (s == "--help" || s == "-h") { usage(); std::exit(0); }
        else {
            std::cerr << "Unknown argument: " << s << "\n";
            usage();
            std::exit(2);
        }
    }
    return a;
}

nlohmann::json build_payload(const std::vector<std::string>& sections) {
    using collector_t = nlohmann::json (*)();
    static const std::map<std::string, collector_t> table = {
        {"system",           &lv::scanner::collect_system},
        {"hardware",         &lv::scanner::collect_hardware},
        {"software",         &lv::scanner::collect_software},
        {"network",          &lv::scanner::collect_network},
        {"security",         &lv::scanner::collect_security},
        {"peripherals",      &lv::scanner::collect_peripherals},
        {"events",           &lv::scanner::collect_events},
        {"users_and_groups", &lv::scanner::collect_users},
    };
    nlohmann::json out = nlohmann::json::object();
    for (const auto& name : sections) {
        auto it = table.find(name);
        if (it == table.end()) {
            lv::log::warn("scan", "unknown section: " + name);
            continue;
        }
        lv::log::info("scan", "collecting " + name);
        try {
            out[name] = it->second();
        } catch (const std::exception& e) {
            lv::log::error("scan", std::string("section ") + name + " failed: " + e.what());
        }
    }
    return out;
}

lv::config::AgentState ensure_enrolled(
    const lv::config::AgentConfig& cfg,
    const std::filesystem::path& state_path,
    bool force) {
    if (!force) {
        if (auto s = lv::config::load_state(state_path); s.has_value()) {
            return *s;
        }
    }
    lv::log::info("enroll", "no usable state -- enrolling");
    auto r = lv::enrollment::enroll(cfg);
    lv::config::write_state(state_path, r.device_id, r.secret,
                            r.matched, r.match_reasons);
    return {r.device_id, r.secret};
}

}  // namespace

int main(int argc, char** argv) {
    auto args = parse(argc, argv);

    // Modes that bypass config / logging.
    if (args.encrypt_stdin) {
        std::string in;
        std::getline(std::cin, in);
        std::cout << lv::dpapi::protect_to_token(in);
        return 0;
    }
    if (args.register_task) {
        wchar_t path[MAX_PATH];
        ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        lv::task::register_task(std::filesystem::path(path), args.interval);
        return 0;
    }
    if (args.unregister_task) {
        return lv::task::unregister_task() ? 0 : 1;
    }

    if (!(args.once || args.watch || args.dry_run || args.enroll_only)) {
        usage();
        return 2;
    }

    // Dry-run doesn't need config; it just emits the payload.
    if (args.dry_run) {
        lv::log::init(args.verbose ? lv::log::Level::Debug : lv::log::Level::Info, {});
        lv::wmi::initialise_com();
        auto payload = build_payload(args.sections);
        std::cout << payload.dump(2) << "\n";
        lv::wmi::uninitialise_com();
        return 0;
    }

    lv::config::AgentConfig cfg;
    try {
        cfg = lv::config::load_config(args.config);
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }
    lv::log::init(args.verbose ? lv::log::Level::Debug : lv::log::Level::Info,
                  cfg.log_path ? std::filesystem::path(*cfg.log_path)
                               : std::filesystem::path{});

    lv::wmi::initialise_com();
    int rc = 0;
    try {
        auto state = ensure_enrolled(cfg, args.state, args.force_enroll);
        if (args.enroll_only) {
            lv::wmi::uninitialise_com();
            return 0;
        }

        do {
            auto payload = build_payload(args.sections);
            auto result = lv::transport::send_scan(cfg, state, payload);
            lv::log::info("agent", "scan accepted: " + result.dump());
            if (args.watch) {
                int sleep_for = (cfg.interval_minutes > 0 ? cfg.interval_minutes : 60) * 60;
                lv::log::info("agent", "sleeping " + std::to_string(sleep_for) + "s");
                std::this_thread::sleep_for(std::chrono::seconds(sleep_for));
            }
        } while (args.watch);
    } catch (const std::exception& e) {
        lv::log::error("agent", e.what());
        rc = 1;
    }
    lv::wmi::uninitialise_com();
    return rc;
}
