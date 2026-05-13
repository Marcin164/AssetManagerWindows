#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace lv::log {

enum class Level { Debug, Info, Warn, Error };

void init(Level level, const std::filesystem::path& file_path);
void set_level(Level level);

void log(Level lvl, std::string_view component, std::string_view message);

// Convenience wrappers.
inline void debug(std::string_view c, std::string_view m) { log(Level::Debug, c, m); }
inline void info (std::string_view c, std::string_view m) { log(Level::Info,  c, m); }
inline void warn (std::string_view c, std::string_view m) { log(Level::Warn,  c, m); }
inline void error(std::string_view c, std::string_view m) { log(Level::Error, c, m); }

}  // namespace lv::log
