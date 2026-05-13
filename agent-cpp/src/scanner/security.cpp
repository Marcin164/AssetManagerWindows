#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

#include <windows.h>

namespace lv::scanner {
namespace {

bool rdp_enabled() {
    HKEY h = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"System\\CurrentControlSet\\Control\\Terminal Server",
            0, KEY_READ, &h) != ERROR_SUCCESS) {
        return false;
    }
    DWORD v = 1, sz = sizeof(v), type = 0;
    LSTATUS rc = ::RegQueryValueExW(h, L"fDenyTSConnections", nullptr, &type,
                                    reinterpret_cast<LPBYTE>(&v), &sz);
    ::RegCloseKey(h);
    return (rc == ERROR_SUCCESS && v == 0);
}

}  // namespace

nlohmann::json collect_security() {
    nlohmann::json out;

    // SecurityCenter2 lives in a non-default namespace.
    auto av_rows = wmi::query(
        L"SELECT displayName, productState, pathToSignedProductExe "
        L"FROM AntiVirusProduct",
        L"ROOT\\SecurityCenter2");
    out["antivirus"] = av_rows;

    // BitLocker. Win32_EncryptableVolume sits under MicrosoftVolumeEncryption.
    nlohmann::json bl = nlohmann::json::array();
    auto vols = wmi::query(
        L"SELECT DriveLetter, ConversionStatus, ProtectionStatus, "
        L"EncryptionMethod, VolumeType, VolumeStatus "
        L"FROM Win32_EncryptableVolume",
        L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
    for (auto& v : vols) {
        nlohmann::json row;
        if (v.contains("DriveLetter"))      row["MountPoint"]       = v["DriveLetter"];
        if (v.contains("ProtectionStatus")) row["ProtectionStatus"] = v["ProtectionStatus"];
        if (v.contains("EncryptionMethod")) row["EncryptionMethod"] = v["EncryptionMethod"];
        if (v.contains("VolumeType"))       row["VolumeType"]       = v["VolumeType"];
        // ConversionStatus = 1 means "fully encrypted" in BitLocker speak.
        long cs = v.value("ConversionStatus", 0L);
        row["LockStatus"] = (cs == 1) ? 0 : 1;
        row["CapacityGB"] = 0;
        bl.push_back(row);
    }
    out["bitlocker"] = bl;

    // Firewall profile data lives under StandardCimv2.
    nlohmann::json fw = nlohmann::json::array();
    auto fw_rows = wmi::query(
        L"SELECT Name, Enabled, AllowInboundRules, AllowUserApps, AllowUserPorts "
        L"FROM MSFT_NetFirewallProfile",
        L"ROOT\\StandardCimv2");
    for (auto& r : fw_rows) {
        nlohmann::json row = {
            {"Name",              r.value("Name", "")},
            {"Enabled",           r.value("Enabled", false) ? 1 : 0},
            {"AllowUserApps",     r.value("AllowUserApps", false) ? 1 : 0},
            {"AllowUserPorts",    r.value("AllowUserPorts", false) ? 1 : 0},
            {"AllowInboundRules", r.value("AllowInboundRules", false) ? 1 : 0},
        };
        fw.push_back(row);
    }
    out["firewall_profile"] = fw;

    out["rdp_status"] = {{"RDP_Enabled", rdp_enabled()}};

    out["tpm"] = wmi::query_first(
        L"SELECT SpecVersion, ManufacturerIdTxt, ManufacturerVersion, "
        L"IsOwned_InitialValue, IsEnabled_InitialValue, IsActivated_InitialValue "
        L"FROM Win32_Tpm",
        L"ROOT\\CIMV2\\Security\\MicrosoftTpm");

    out["uac_status"]   = nlohmann::json::object();
    out["updates"]      = wmi::query(
        L"SELECT HotFixID, Description, InstalledOn FROM Win32_QuickFixEngineering");
    out["startup_apps"] = nlohmann::json::array();
    return out;
}

}  // namespace lv::scanner
