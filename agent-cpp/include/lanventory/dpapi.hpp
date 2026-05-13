#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lv::dpapi {

// Encrypt / decrypt arbitrary bytes under LocalMachine scope. The blob
// is decipherable by any process on the same host. Throws on Win32 API
// failure.
std::vector<std::uint8_t> protect(const std::vector<std::uint8_t>& plaintext);
std::vector<std::uint8_t> unprotect(const std::vector<std::uint8_t>& blob);

// Convenience: protect a UTF-8 string and emit ``dpapi:<base64>``.
std::string protect_to_token(std::string_view plaintext);

// Inverse: ``dpapi:<base64>`` -> plaintext. If the value doesn't carry
// the ``dpapi:`` prefix it's returned as-is (plaintext fallback for
// dev/lab use).
std::string unprotect_token(std::string_view token);

}  // namespace lv::dpapi
