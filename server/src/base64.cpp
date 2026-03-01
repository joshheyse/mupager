#include "base64.h"

#include <cstdint>

namespace base64 {

static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(const unsigned char* data, size_t len) {
  std::string out;
  out.reserve(4 * ((len + 2) / 3));

  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
    out.push_back(TABLE[(triple >> 18) & 0x3F]);
    out.push_back(TABLE[(triple >> 12) & 0x3F]);
    out.push_back(TABLE[(triple >> 6) & 0x3F]);
    out.push_back(TABLE[triple & 0x3F]);
  }

  if (i < len) {
    uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) {
      triple |= static_cast<uint32_t>(data[i + 1]) << 8;
    }

    out.push_back(TABLE[(triple >> 18) & 0x3F]);
    out.push_back(TABLE[(triple >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? TABLE[(triple >> 6) & 0x3F] : '=');
    out.push_back('=');
  }

  return out;
}

std::string encode(const std::string& data) {
  return encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

} // namespace base64
