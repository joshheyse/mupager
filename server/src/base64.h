#pragma once

#include <cstddef>
#include <string>

/// RFC 4648 base64 encoding (no decode needed — protocol is write-only).
namespace base64 {

/// @brief Encode raw bytes to a base64 string.
/// @param data Pointer to input bytes.
/// @param len Number of bytes to encode.
std::string encode(const unsigned char* data, size_t len);

/// @brief Encode a string to base64.
std::string encode(const std::string& data);

} // namespace base64
