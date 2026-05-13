#include "lanventory/crypto.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace lv::crypto {
namespace {

std::string to_hex(const std::uint8_t* data, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (std::size_t i = 0; i < n; ++i) {
        out[2 * i]     = digits[(data[i] >> 4) & 0x0F];
        out[2 * i + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

}  // namespace

std::string sha256_hex(std::string_view input) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (::BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256)");
    }
    BCRYPT_HASH_HANDLE h = nullptr;
    if (::BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) != STATUS_SUCCESS) {
        ::BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptCreateHash");
    }
    ::BCryptHashData(h, (PUCHAR)input.data(),
                     static_cast<ULONG>(input.size()), 0);
    std::array<std::uint8_t, 32> out{};
    ::BCryptFinishHash(h, out.data(), static_cast<ULONG>(out.size()), 0);
    ::BCryptDestroyHash(h);
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return to_hex(out.data(), out.size());
}

std::string hmac_sha256_hex(std::string_view key, std::string_view message) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (::BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_SHA256_ALGORITHM, nullptr,
            BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(HMAC-SHA256)");
    }
    BCRYPT_HASH_HANDLE h = nullptr;
    if (::BCryptCreateHash(alg, &h, nullptr, 0,
                           (PUCHAR)key.data(),
                           static_cast<ULONG>(key.size()), 0) != STATUS_SUCCESS) {
        ::BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptCreateHash (HMAC)");
    }
    ::BCryptHashData(h, (PUCHAR)message.data(),
                     static_cast<ULONG>(message.size()), 0);
    std::array<std::uint8_t, 32> out{};
    ::BCryptFinishHash(h, out.data(), static_cast<ULONG>(out.size()), 0);
    ::BCryptDestroyHash(h);
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return to_hex(out.data(), out.size());
}

std::string random_hex(std::size_t bytes) {
    std::vector<std::uint8_t> buf(bytes);
    if (::BCryptGenRandom(
            nullptr, buf.data(),
            static_cast<ULONG>(buf.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != STATUS_SUCCESS) {
        throw std::runtime_error("BCryptGenRandom");
    }
    return to_hex(buf.data(), buf.size());
}

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    DWORD out_size = 0;
    ::CryptBinaryToStringA(
        bytes.data(), static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &out_size);
    std::string out(out_size, '\0');
    ::CryptBinaryToStringA(
        bytes.data(), static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &out_size);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::vector<std::uint8_t> base64_decode(std::string_view b64) {
    DWORD out_size = 0;
    ::CryptStringToBinaryA(
        b64.data(), static_cast<DWORD>(b64.size()),
        CRYPT_STRING_BASE64, nullptr, &out_size, nullptr, nullptr);
    std::vector<std::uint8_t> out(out_size);
    ::CryptStringToBinaryA(
        b64.data(), static_cast<DWORD>(b64.size()),
        CRYPT_STRING_BASE64, out.data(), &out_size, nullptr, nullptr);
    out.resize(out_size);
    return out;
}

}  // namespace lv::crypto
