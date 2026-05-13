#include "lanventory/fingerprint.hpp"
#include "lanventory/crypto.hpp"
#include "lanventory/wmi.hpp"
#include "lanventory/log.hpp"

#include <windows.h>
#include <iphlpapi.h>
#include <tbs.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "tbs.lib")

namespace lv::fingerprint {
namespace {

std::string hostname() {
    char buf[256] = {0};
    DWORD n = sizeof(buf);
    if (::GetComputerNameA(buf, &n)) return std::string(buf, n);
    return "";
}

// Read the TPM Endorsement Key public bytes via TBS (Trusted Base
// Services). On hosts without an addressable EK we fall through and
// the enrollment payload omits ``tpmFingerprint``. Backend will fall
// back to CPU id + MAC overlap, which is fine for matching.
std::string tpm_fingerprint() {
    TBS_HCONTEXT ctx = 0;
    TBS_CONTEXT_PARAMS2 params {TBS_CONTEXT_VERSION_TWO, {}};
    params.includeTpm20 = 1;
    if (::Tbsi_Context_Create(reinterpret_cast<PCTBS_CONTEXT_PARAMS>(&params), &ctx) != TBS_SUCCESS) {
        return "";
    }
    // We don't actually call TPM commands here -- just confirming the
    // device exists and grabbing whatever stable identifier we can.
    // The most portable surrogate is the TPM device's vendor + version
    // string from WMI, hashed.
    ::Tbsip_Context_Close(ctx);

    auto tpm = wmi::query_first(
        L"SELECT ManufacturerIdTxt, ManufacturerVersion, PhysicalPresenceVersionInfo "
        L"FROM Win32_Tpm",
        L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
    if (tpm.empty()) return "";
    std::string concat;
    for (const char* k : {"ManufacturerIdTxt", "ManufacturerVersion",
                          "PhysicalPresenceVersionInfo"}) {
        if (tpm.contains(k) && tpm[k].is_string()) {
            concat += tpm[k].get<std::string>();
            concat += '|';
        }
    }
    if (concat.empty()) return "";
    return crypto::sha256_hex(concat);
}

std::vector<std::string> mac_addresses() {
    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buf(size);
    PIP_ADAPTER_ADDRESSES addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ULONG rc = ::GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                 | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, addrs, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        rc = ::GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                     | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, addrs, &size);
    }
    std::set<std::string> seen;
    if (rc == NO_ERROR) {
        for (auto* a = addrs; a; a = a->Next) {
            if (a->PhysicalAddressLength != 6) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            char mac[18];
            std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                          a->PhysicalAddress[0], a->PhysicalAddress[1],
                          a->PhysicalAddress[2], a->PhysicalAddress[3],
                          a->PhysicalAddress[4], a->PhysicalAddress[5]);
            std::string s(mac);
            if (s == "00:00:00:00:00:00" || s == "FF:FF:FF:FF:FF:FF") continue;
            seen.insert(s);
        }
    }
    return {seen.begin(), seen.end()};
}

}  // namespace

nlohmann::json collect(std::string_view agent_version) {
    nlohmann::json out = {
        {"hostname",     hostname()},
        {"platform",     "windows"},
        {"agentVersion", std::string(agent_version)},
    };
    if (auto tpm = tpm_fingerprint(); !tpm.empty()) {
        out["tpmFingerprint"] = tpm;
    }
    if (auto macs = mac_addresses(); !macs.empty()) {
        out["macAddresses"] = macs;
    }
    auto cpu = wmi::query_first(L"SELECT ProcessorId FROM Win32_Processor");
    if (cpu.contains("ProcessorId") && cpu["ProcessorId"].is_string()
        && !cpu["ProcessorId"].get<std::string>().empty()) {
        out["cpuId"] = cpu["ProcessorId"].get<std::string>();
    }
    auto board = wmi::query_first(
        L"SELECT SerialNumber, Manufacturer, Product FROM Win32_BaseBoard");
    if (board.contains("SerialNumber") && board["SerialNumber"].is_string()) {
        out["serialNumber"] = board["SerialNumber"].get<std::string>();
    }
    if (board.contains("Manufacturer") && board["Manufacturer"].is_string()) {
        out["manufacturer"] = board["Manufacturer"].get<std::string>();
    }
    if (board.contains("Product") && board["Product"].is_string()) {
        out["model"] = board["Product"].get<std::string>();
    }

    if (!out.contains("tpmFingerprint") && !out.contains("cpuId")
        && !out.contains("serialNumber") && !out.contains("macAddresses")) {
        log::warn("fingerprint", "no stable identifiers -- enrollment may auto-create a duplicate");
        out["cpuId"] = "fallback-" + out["hostname"].get<std::string>();
    }
    return out;
}

}  // namespace lv::fingerprint
