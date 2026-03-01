#pragma once

#include <cstddef>
#include <string>

/// RFC 4648 base64 encoding (no decode needed — protocol is write-only).
namespace base64 {

std::string encode(const unsigned char* data, size_t len);
std::string encode(const std::string& data);

} // namespace base64
