#include "lanventory/dpapi.hpp"
#include "lanventory/crypto.hpp"

#include <windows.h>
#include <dpapi.h>

#include <stdexcept>

#pragma comment(lib, "crypt32.lib")

namespace lv::dpapi {
namespace {

constexpr DWORD kLocalMachine = CRYPTPROTECT_LOCAL_MACHINE;

}  // namespace

std::vector<std::uint8_t> protect(const std::vector<std::uint8_t>& plaintext) {
    DATA_BLOB in {static_cast<DWORD>(plaintext.size()),
                  const_cast<BYTE*>(plaintext.data())};
    DATA_BLOB out {0, nullptr};
    if (!::CryptProtectData(&in, L"LanVentoryAgent", nullptr, nullptr,
                            nullptr, kLocalMachine, &out)) {
        throw std::runtime_error("CryptProtectData failed");
    }
    std::vector<std::uint8_t> buf(out.pbData, out.pbData + out.cbData);
    ::LocalFree(out.pbData);
    return buf;
}

std::vector<std::uint8_t> unprotect(const std::vector<std::uint8_t>& blob) {
    DATA_BLOB in {static_cast<DWORD>(blob.size()),
                  const_cast<BYTE*>(blob.data())};
    DATA_BLOB out {0, nullptr};
    if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                              0, &out)) {
        throw std::runtime_error("CryptUnprotectData failed");
    }
    std::vector<std::uint8_t> buf(out.pbData, out.pbData + out.cbData);
    ::LocalFree(out.pbData);
    return buf;
}

std::string protect_to_token(std::string_view plaintext) {
    std::vector<std::uint8_t> in(plaintext.begin(), plaintext.end());
    auto blob = protect(in);
    return "dpapi:" + crypto::base64_encode(blob);
}

std::string unprotect_token(std::string_view token) {
    constexpr std::string_view prefix = "dpapi:";
    if (token.substr(0, prefix.size()) != prefix) {
        return std::string(token);
    }
    auto blob = crypto::base64_decode(token.substr(prefix.size()));
    auto plain = unprotect(blob);
    return std::string(plain.begin(), plain.end());
}

}  // namespace lv::dpapi
