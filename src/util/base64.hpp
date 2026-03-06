#pragma once

#include <span>
#include <string>

/// RFC 4648 base64 encoding (no decode needed — protocol is write-only).
namespace base64 {

/// @brief Encode raw bytes to a base64 string.
/// @param data View of input bytes.
std::string encode(std::span<const unsigned char> data);

/// @brief Encode a string to base64.
std::string encode(const std::string& data);

} // namespace base64
