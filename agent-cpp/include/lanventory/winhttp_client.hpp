#pragma once

#include <map>
#include <string>
#include <string_view>

namespace lv::http {

struct Response {
    int status = 0;
    std::string body;
};

struct Request {
    std::string method;       // "GET" / "POST"
    std::string url;          // full https://host:port/path
    std::map<std::string, std::string> headers;
    std::string body;
    bool verify_tls = true;
    int timeout_seconds = 60;
};

// Single-shot HTTP request via WinHTTP. Throws std::runtime_error on
// transport-level failure (DNS, TLS, socket). HTTP status codes >= 400
// are returned as-is so the caller decides what's fatal.
Response send(const Request& req);

}  // namespace lv::http
