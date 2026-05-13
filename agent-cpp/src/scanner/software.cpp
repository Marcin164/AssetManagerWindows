#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

#include <windows.h>

#include <set>
#include <string>

namespace lv::scanner {
namespace {

// Enumerate one uninstall registry root (HKLM/HKCU + WOW64) and append
// rows to ``out``. Skips entries without DisplayName the way the
// frontend's filter does.
void scan_uninstall_root(HKEY root, const wchar_t* subkey, REGSAM extra_flags,
                         nlohmann::json& out, std::set<std::string>& seen) {
    HKEY h = nullptr;
    if (::RegOpenKeyExW(root, subkey, 0, KEY_READ | extra_flags, &h) != ERROR_SUCCESS) {
        return;
    }
    wchar_t name[256];
    DWORD index = 0;
    for (;;) {
        DWORD name_len = std::size(name);
        FILETIME ft;
        LSTATUS rc = ::RegEnumKeyExW(h, index++, name, &name_len,
                                     nullptr, nullptr, nullptr, &ft);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;

        HKEY entry;
        if (::RegOpenKeyExW(h, name, 0, KEY_READ | extra_flags, &entry) != ERROR_SUCCESS) {
            continue;
        }

        auto read_str = [&](const wchar_t* val) -> std::string {
            wchar_t buf[1024];
            DWORD size = sizeof(buf), type = 0;
            if (::RegQueryValueExW(entry, val, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                std::wstring w(buf, (size / sizeof(wchar_t)) - 1);
                int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string s(static_cast<size_t>(n - 1), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
                return s;
            }
            return "";
        };
        auto read_dword = [&](const wchar_t* val) -> long {
            DWORD v = 0, size = sizeof(v), type = 0;
            if (::RegQueryValueExW(entry, val, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&v), &size) == ERROR_SUCCESS
                && type == REG_DWORD) return static_cast<long>(v);
            return 0;
        };

        std::string display = read_str(L"DisplayName");
        if (!display.empty() && seen.insert(display).second) {
            nlohmann::json row = {
                {"DisplayName",     display},
                {"DisplayVersion",  read_str(L"DisplayVersion")},
                {"Publisher",       read_str(L"Publisher")},
                {"EstimatedSize",   read_dword(L"EstimatedSize")},
                {"InstallDate",     read_str(L"InstallDate")},
            };
            out.push_back(row);
        }
        ::RegCloseKey(entry);
    }
    ::RegCloseKey(h);
}

}  // namespace

nlohmann::json collect_software() {
    nlohmann::json out;

    nlohmann::json installed = nlohmann::json::array();
    std::set<std::string> seen;
    const wchar_t* uninstall = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    scan_uninstall_root(HKEY_LOCAL_MACHINE, uninstall, KEY_WOW64_64KEY, installed, seen);
    scan_uninstall_root(HKEY_LOCAL_MACHINE, uninstall, KEY_WOW64_32KEY, installed, seen);
    scan_uninstall_root(HKEY_CURRENT_USER,  uninstall, 0,               installed, seen);
    out["installed_programs"] = installed;

    // Appx packages live in COM/PackageManager which is heavy to call from
    // a static native exe. Skipping for v1 -- frontend handles empty.
    out["appx_packages"] = nlohmann::json::array();

    // Windows optional features via DISM API is similarly heavy; leave
    // empty for v1. The Software tab gracefully falls back.
    out["windows_features"] = nlohmann::json::array();
    return out;
}

}  // namespace lv::scanner
