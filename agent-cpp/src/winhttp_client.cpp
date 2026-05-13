#include "lanventory/winhttp_client.hpp"
#include "lanventory/log.hpp"

#include <windows.h>
#include <winhttp.h>

#include <regex>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace lv::http {
namespace {

std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                          static_cast<int>(s.size()), out.data(), n);
    return out;
}

struct ParsedUrl {
    bool https = true;
    std::wstring host;
    INTERNET_PORT port = 443;
    std::wstring path;
};

ParsedUrl parse_url(std::string_view url) {
    std::wstring wide = utf8_to_wide(url);
    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    wchar_t scheme[16]{};
    wchar_t host[256]{};
    wchar_t path[2048]{};
    comp.lpszScheme = scheme;     comp.dwSchemeLength    = std::size(scheme);
    comp.lpszHostName = host;     comp.dwHostNameLength  = std::size(host);
    comp.lpszUrlPath = path;      comp.dwUrlPathLength   = std::size(path);
    if (!::WinHttpCrackUrl(wide.c_str(), 0, 0, &comp)) {
        throw std::runtime_error("Invalid URL: " + std::string(url));
    }
    ParsedUrl p;
    p.https = (comp.nScheme == INTERNET_SCHEME_HTTPS);
    p.host = host;
    p.port = comp.nPort;
    p.path = path[0] ? path : L"/";
    return p;
}

class ScopedHandle {
public:
    explicit ScopedHandle(HINTERNET h = nullptr) : h_(h) {}
    ~ScopedHandle() { if (h_) ::WinHttpCloseHandle(h_); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    operator HINTERNET() const { return h_; }  // NOLINT
    HINTERNET* addr() { return &h_; }
private:
    HINTERNET h_;
};

}  // namespace

Response send(const Request& req) {
    auto url = parse_url(req.url);

    ScopedHandle session(::WinHttpOpen(
        L"LanVentoryAgent/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) throw std::runtime_error("WinHttpOpen failed");

    const DWORD timeout_ms = static_cast<DWORD>(req.timeout_seconds) * 1000;
    ::WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    ScopedHandle conn(::WinHttpConnect(session, url.host.c_str(), url.port, 0));
    if (!conn) throw std::runtime_error("WinHttpConnect failed");

    const DWORD open_flags = url.https ? WINHTTP_FLAG_SECURE : 0;
    const std::wstring method = utf8_to_wide(req.method);
    ScopedHandle request(::WinHttpOpenRequest(
        conn, method.c_str(), url.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, open_flags));
    if (!request) throw std::runtime_error("WinHttpOpenRequest failed");

    if (url.https && !req.verify_tls) {
        DWORD opts = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                   | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                   | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                   | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        ::WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS,
                           &opts, sizeof(opts));
    }

    // Headers must come in as a single CRLF-joined wide string.
    std::wstring header_blob;
    for (auto& [k, v] : req.headers) {
        header_blob += utf8_to_wide(k);
        header_blob += L": ";
        header_blob += utf8_to_wide(v);
        header_blob += L"\r\n";
    }

    BOOL ok = ::WinHttpSendRequest(
        request,
        header_blob.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_blob.c_str(),
        static_cast<DWORD>(header_blob.size()),
        const_cast<void*>(static_cast<const void*>(req.body.data())),
        static_cast<DWORD>(req.body.size()),
        static_cast<DWORD>(req.body.size()),
        0);
    if (!ok) throw std::runtime_error("WinHttpSendRequest failed");

    if (!::WinHttpReceiveResponse(request, nullptr)) {
        throw std::runtime_error("WinHttpReceiveResponse failed");
    }

    Response resp;
    DWORD status = 0, status_size = sizeof(status);
    ::WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
        WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<int>(status);

    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) break;
        std::vector<char> buf(available);
        DWORD read = 0;
        if (!::WinHttpReadData(request, buf.data(), available, &read)) break;
        resp.body.append(buf.data(), read);
        if (read == 0) break;
    }
    return resp;
}

}  // namespace lv::http
