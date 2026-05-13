#include "lanventory/scanner.hpp"

#include <windows.h>
#include <winevt.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "wevtapi.lib")

namespace lv::scanner {
namespace {

int event_type_from_level(unsigned level) {
    // Win32 evt levels: 1=Critical 2=Error 3=Warning 4=Info 5=Verbose
    switch (level) {
        case 1: case 2: return 1;   // Error
        case 3:         return 2;   // Warning
        default:        return 4;   // Information
    }
}

std::string wide_to_utf8(const wchar_t* w) {
    if (!w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string format_filetime(const FILETIME& ft) {
    SYSTEMTIME st;
    ::FileTimeToSystemTime(&ft, &st);
    std::ostringstream os;
    os << std::setw(4) << std::setfill('0') << st.wYear << '-'
       << std::setw(2) << std::setfill('0') << st.wMonth << '-'
       << std::setw(2) << std::setfill('0') << st.wDay  << ' '
       << std::setw(2) << std::setfill('0') << st.wHour << ':'
       << std::setw(2) << std::setfill('0') << st.wMinute << ':'
       << std::setw(2) << std::setfill('0') << st.wSecond;
    return os.str();
}

nlohmann::json query_log(const wchar_t* channel, int limit = 200) {
    nlohmann::json out = nlohmann::json::array();
    EVT_HANDLE results = ::EvtQuery(
        nullptr, channel, L"*",
        EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!results) return out;

    EVT_HANDLE evs[16];
    int collected = 0;
    while (collected < limit) {
        DWORD returned = 0;
        if (!::EvtNext(results, 16, evs, INFINITE, 0, &returned)) break;
        for (DWORD i = 0; i < returned && collected < limit; ++i, ++collected) {
            // Render the event as XML and parse out the fields we care
            // about. EvtFormatMessage is too heavy for what we need.
            DWORD buf_used = 0, prop_count = 0;
            ::EvtRender(nullptr, evs[i], EvtRenderEventXml, 0, nullptr,
                        &buf_used, &prop_count);
            std::vector<wchar_t> buf(buf_used / sizeof(wchar_t) + 1);
            if (!::EvtRender(nullptr, evs[i], EvtRenderEventXml,
                             buf_used, buf.data(), &buf_used, &prop_count)) {
                ::EvtClose(evs[i]);
                continue;
            }
            std::string xml = wide_to_utf8(buf.data());

            auto extract = [&](const std::string& tag) -> std::string {
                auto a = xml.find('<' + tag);
                if (a == std::string::npos) return "";
                auto b = xml.find('>', a);
                if (b == std::string::npos) return "";
                auto c = xml.find("</" + tag, b);
                if (c == std::string::npos) return "";
                return xml.substr(b + 1, c - b - 1);
            };

            nlohmann::json row;
            row["EventID"]     = std::stoi("0" + extract("EventID"));
            row["Category"]    = "";
            try {
                row["EventType"] = event_type_from_level(
                    static_cast<unsigned>(std::stoi("0" + extract("Level"))));
            } catch (...) {
                row["EventType"] = 4;
            }

            // SourceName from <Provider Name="..."/>
            auto p = xml.find("<Provider ");
            if (p != std::string::npos) {
                auto nstart = xml.find("Name=\"", p);
                if (nstart != std::string::npos) {
                    nstart += 6;
                    auto nend = xml.find('"', nstart);
                    row["SourceName"] = xml.substr(nstart, nend - nstart);
                }
            }
            // TimeCreated SystemTime="..."
            auto tc = xml.find("<TimeCreated ");
            if (tc != std::string::npos) {
                auto ts = xml.find("SystemTime=\"", tc);
                if (ts != std::string::npos) {
                    ts += 12;
                    auto te = xml.find('"', ts);
                    auto raw = xml.substr(ts, te - ts);
                    // ISO -> "YYYY-MM-DD HH:MM:SS"
                    if (raw.size() >= 19) raw.replace(10, 1, " ");
                    row["TimeGenerated"] = raw.substr(0, 19);
                }
            }
            row["Message"] = "";
            out.push_back(row);
            ::EvtClose(evs[i]);
        }
        if (returned < 16) break;
    }
    ::EvtClose(results);
    return out;
}

}  // namespace

nlohmann::json collect_events() {
    return {
        {"System",      query_log(L"System")},
        {"Application", query_log(L"Application")},
        {"Security",    query_log(L"Security")},
    };
}

}  // namespace lv::scanner
