#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

#include <windows.h>
#include <sysinfoapi.h>

namespace lv::scanner {
namespace {

std::string current_user() {
    char buf[256] = {0};
    DWORD n = sizeof(buf);
    if (::GetUserNameA(buf, &n)) return std::string(buf, n - 1);
    return "";
}

std::string hostname() {
    char buf[256] = {0};
    DWORD n = sizeof(buf);
    if (::GetComputerNameA(buf, &n)) return std::string(buf, n);
    return "";
}

}  // namespace

nlohmann::json collect_system() {
    auto cim = wmi::query_first(
        L"SELECT Caption, Version, SerialNumber, OSArchitecture, "
        L"InstallDate, LastBootUpTime, BuildNumber, OSLanguage, CountryCode "
        L"FROM Win32_OperatingSystem");

    nlohmann::json out = {
        {"hostname",   hostname()},
        {"username",   current_user()},
        {"Cim",        cim},
    };
    if (cim.contains("Caption") && cim["Caption"].is_string()) {
        out["os_name"] = cim["Caption"].get<std::string>();
    }
    if (cim.contains("Version") && cim["Version"].is_string()) {
        out["os_version"] = cim["Version"].get<std::string>();
    }
    if (cim.contains("LastBootUpTime") && cim["LastBootUpTime"].is_string()) {
        out["boot_time"] = cim["LastBootUpTime"].get<std::string>();
    }

    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    out["machine"] =
        si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "AMD64" :
        si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "ARM64" :
        si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" :
        "unknown";
    return out;
}

}  // namespace lv::scanner
