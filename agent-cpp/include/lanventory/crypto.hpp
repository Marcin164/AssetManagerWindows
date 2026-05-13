#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lv::crypto {

// SHA-256 of UTF-8 bytes, returned as lowercase hex.
std::string sha256_hex(std::string_view input);

// HMAC-SHA256, returned as lowercase hex. ``key`` is treated as raw
// bytes -- to mirror the backend's behaviour we pass the hex string of
// the secret hash (which is what Node's createHmac uses when given
// a string key).
std::string hmac_sha256_hex(std::string_view key, std::string_view message);

// Cryptographically secure random hex string. ``bytes`` = number of
// raw bytes; output length = bytes * 2.
std::string random_hex(std::size_t bytes);

// Base64 encode / decode.
std::string base64_encode(const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> base64_decode(std::string_view b64);

}  // namespace lv::crypto
