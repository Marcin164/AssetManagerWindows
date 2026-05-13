#include "lanventory/task_scheduler.hpp"
#include "lanventory/log.hpp"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lv::task {
namespace {

const char* kTaskXml =
R"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>LanVentory inventory agent.</Description>
    <Author>LanVentory</Author>
  </RegistrationInfo>
  <Triggers>
    <BootTrigger><Enabled>true</Enabled><Delay>PT2M</Delay></BootTrigger>
    <TimeTrigger>
      <Repetition>
        <Interval>PT%dM</Interval>
        <StopAtDurationEnd>false</StopAtDurationEnd>
      </Repetition>
      <StartBoundary>2024-01-01T00:00:00</StartBoundary>
      <Enabled>true</Enabled>
    </TimeTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>S-1-5-18</UserId>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <StartWhenAvailable>true</StartWhenAvailable>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <ExecutionTimeLimit>PT30M</ExecutionTimeLimit>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>%s</Command>
      <Arguments>--once</Arguments>
    </Exec>
  </Actions>
</Task>)";

int run_silent(const std::wstring& cmdline) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmdline;
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    ::WaitForSingleObject(pi.hProcess, 30'000);
    DWORD rc = 1;
    ::GetExitCodeProcess(pi.hProcess, &rc);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    return static_cast<int>(rc);
}

}  // namespace

void register_task(const std::filesystem::path& exe_path, int interval_minutes) {
    // Write the XML to %TEMP%. schtasks /Create requires a file path.
    wchar_t temp[MAX_PATH];
    DWORD n = ::GetTempPathW(MAX_PATH, temp);
    if (n == 0) throw std::runtime_error("GetTempPath failed");
    std::filesystem::path xml_path = std::filesystem::path(temp) / "lanventory-task.xml";

    char rendered[4096];
    std::snprintf(rendered, sizeof(rendered), kTaskXml,
                  interval_minutes, exe_path.string().c_str());
    // schtasks expects UTF-16 LE when the XML declares it.
    std::wstring wide;
    {
        int wn = ::MultiByteToWideChar(CP_UTF8, 0, rendered, -1, nullptr, 0);
        wide.resize(static_cast<size_t>(wn - 1));
        ::MultiByteToWideChar(CP_UTF8, 0, rendered, -1, wide.data(), wn);
    }
    std::ofstream out(xml_path, std::ios::binary);
    constexpr char bom[] = {'\xFF', '\xFE'};
    out.write(bom, 2);
    out.write(reinterpret_cast<const char*>(wide.data()),
              static_cast<std::streamsize>(wide.size() * sizeof(wchar_t)));
    out.close();

    std::wstringstream cmd;
    cmd << L"schtasks.exe /Create /TN \"" << kTaskName
        << L"\" /XML \"" << xml_path.wstring() << L"\" /F";
    int rc = run_silent(cmd.str());
    std::error_code ec;
    std::filesystem::remove(xml_path, ec);
    if (rc != 0) {
        throw std::runtime_error(
            "schtasks /Create failed with exit code " + std::to_string(rc));
    }
    log::info("task", "Registered scheduled task");
}

bool unregister_task() {
    std::wstring cmd = L"schtasks.exe /Delete /TN \"" + std::wstring(kTaskName) + L"\" /F";
    int rc = run_silent(cmd);
    return rc == 0;
}

void run_task_now() {
    std::wstring cmd = L"schtasks.exe /Run /TN \"" + std::wstring(kTaskName) + L"\"";
    int rc = run_silent(cmd);
    if (rc != 0) {
        throw std::runtime_error(
            "schtasks /Run failed with exit code " + std::to_string(rc));
    }
}

}  // namespace lv::task
