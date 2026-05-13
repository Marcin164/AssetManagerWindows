#include "lanventory/log.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace lv::log {
namespace {

std::mutex g_mutex;
Level      g_level = Level::Info;
std::ofstream g_file;

const char* level_str(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

std::string timestamp_utc() {
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    ::gmtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.'
       << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

}  // namespace

void init(Level level, const std::filesystem::path& file_path) {
    std::lock_guard lock(g_mutex);
    g_level = level;
    if (!file_path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(file_path.parent_path(), ec);
        g_file.open(file_path, std::ios::app);
    }
}

void set_level(Level level) {
    std::lock_guard lock(g_mutex);
    g_level = level;
}

void log(Level lvl, std::string_view component, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (static_cast<int>(lvl) < static_cast<int>(g_level)) return;
    std::ostringstream line;
    line << timestamp_utc() << ' ' << level_str(lvl) << ' '
         << component << ' ' << message;
    const auto rendered = line.str();
    std::cerr << rendered << '\n';
    if (g_file.is_open()) {
        g_file << rendered << '\n';
        g_file.flush();
    }
}

}  // namespace lv::log
