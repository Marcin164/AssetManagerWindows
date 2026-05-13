#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include <sstream>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace lv::scanner {
namespace {

std::string addr_to_str(const SOCKADDR* sa) {
    char buf[64] = {0};
    if (sa->sa_family == AF_INET) {
        auto* a = reinterpret_cast<const sockaddr_in*>(sa);
        ::inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
    } else if (sa->sa_family == AF_INET6) {
        auto* a = reinterpret_cast<const sockaddr_in6*>(sa);
        ::inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
    }
    return buf;
}

std::string prefix_to_mask(unsigned len) {
    if (len > 32) return "";
    std::uint32_t m = len == 0 ? 0u : (~0u << (32 - len));
    std::ostringstream os;
    os << ((m >> 24) & 0xFF) << '.' << ((m >> 16) & 0xFF) << '.'
       << ((m >> 8) & 0xFF) << '.' << (m & 0xFF);
    return os.str();
}

}  // namespace

nlohmann::json collect_network() {
    nlohmann::json out;
    nlohmann::json nic_config = nlohmann::json::array();
    nlohmann::json adapters = nlohmann::json::object();

    ULONG size = 16 * 1024;
    std::vector<std::uint8_t> buf(size);
    auto* head = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ULONG rc = ::GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_PREFIX,
        nullptr, head, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        head = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        rc = ::GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_PREFIX,
            nullptr, head, &size);
    }
    if (rc == NO_ERROR) {
        for (auto* a = head; a; a = a->Next) {
            // Adapter alias is widechar; convert to UTF-8.
            const wchar_t* alias_w = a->FriendlyName ? a->FriendlyName : L"";
            int n = ::WideCharToMultiByte(CP_UTF8, 0, alias_w, -1, nullptr, 0,
                                          nullptr, nullptr);
            std::string alias(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
            if (n > 0) {
                ::WideCharToMultiByte(CP_UTF8, 0, alias_w, -1, alias.data(),
                                      n, nullptr, nullptr);
            }
            const wchar_t* desc_w = a->Description ? a->Description : L"";
            int n2 = ::WideCharToMultiByte(CP_UTF8, 0, desc_w, -1, nullptr, 0,
                                           nullptr, nullptr);
            std::string desc(static_cast<size_t>(n2 > 0 ? n2 - 1 : 0), '\0');
            if (n2 > 0) {
                ::WideCharToMultiByte(CP_UTF8, 0, desc_w, -1, desc.data(),
                                      n2, nullptr, nullptr);
            }

            nlohmann::json row;
            row["InterfaceAlias"]       = alias;
            row["InterfaceDescription"] = desc;
            row["Connected"]            = (a->OperStatus == IfOperStatusUp);
            row["Speed(Mbps)"]          = a->TransmitLinkSpeed > 0
                ? static_cast<long long>(a->TransmitLinkSpeed / 1'000'000) : 0;
            row["DHCP"] = (a->Flags & IP_ADAPTER_DHCP_ENABLED) ? 1 : 0;

            std::string ipv4, ipv6, link_local;
            std::string netmask;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                const auto* sa = u->Address.lpSockaddr;
                if (!sa) continue;
                std::string s = addr_to_str(sa);
                if (sa->sa_family == AF_INET && ipv4.empty()) {
                    ipv4 = s;
                    netmask = prefix_to_mask(u->OnLinkPrefixLength);
                } else if (sa->sa_family == AF_INET6) {
                    if (s.rfind("fe80", 0) == 0 && link_local.empty()) {
                        link_local = s;
                    } else if (ipv6.empty()) {
                        ipv6 = s;
                    }
                }
            }
            row["IPv4Address"]   = ipv4;
            row["NetMask"]       = netmask;
            row["IPv6Address"]   = ipv6;
            row["IPv6LinkLocal"] = link_local;

            std::string gw4, gw6;
            for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
                const auto* sa = g->Address.lpSockaddr;
                if (!sa) continue;
                std::string s = addr_to_str(sa);
                if (sa->sa_family == AF_INET && gw4.empty()) gw4 = s;
                if (sa->sa_family == AF_INET6 && gw6.empty()) gw6 = s;
            }
            row["IPv4Gateway"] = gw4;
            row["IPv6Gateway"] = gw6;

            nlohmann::json dns = nlohmann::json::array();
            for (auto* d = a->FirstDnsServerAddress; d; d = d->Next) {
                if (d->Address.lpSockaddr) {
                    dns.push_back(addr_to_str(d->Address.lpSockaddr));
                }
            }
            row["DNSServers"] = {{"value", dns}};

            char mac[18] = {0};
            if (a->PhysicalAddressLength == 6) {
                std::snprintf(mac, sizeof(mac),
                              "%02X:%02X:%02X:%02X:%02X:%02X",
                              a->PhysicalAddress[0], a->PhysicalAddress[1],
                              a->PhysicalAddress[2], a->PhysicalAddress[3],
                              a->PhysicalAddress[4], a->PhysicalAddress[5]);
            }
            row["MAC"] = mac;

            nic_config.push_back(row);
            adapters[alias] = nlohmann::json::array();
        }
    }
    out["nic_config"]     = nic_config;
    out["adapters"]       = adapters;
    out["connections"]    = nlohmann::json::array();  // process-attribution out of scope
    out["firewall_rules"] = nlohmann::json::array();  // MSFT_NetFirewallRule pending
    return out;
}

}  // namespace lv::scanner
