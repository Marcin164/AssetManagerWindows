#pragma once

#include <filesystem>

namespace lv::task {

constexpr const wchar_t* kTaskName = L"LanVentoryAgent";

// Register (replace) the SYSTEM scheduled task: boot trigger + repeat
// every ``interval_minutes``. Implemented via ``schtasks.exe /Create
// /XML`` so we don't need to fight with ITaskService COM here -- the
// XML schema is well-known and stable since Windows 7.
void register_task(const std::filesystem::path& exe_path, int interval_minutes);

bool unregister_task();
void run_task_now();

}  // namespace lv::task
