#include "kitty.h"

#include "base64.h"
#include "pixmap.h"

#include <cstring>
#include <vector>

namespace kitty {

static constexpr size_t CHUNK_SIZE = 4096;

std::string encode(const Pixmap& pixmap, uint32_t image_id) {
  int w = pixmap.width();
  int h = pixmap.height();
  int comp = pixmap.components();
  int row_bytes = w * comp;

  // Strip stride padding into a packed buffer
  std::vector<unsigned char> packed(static_cast<size_t>(row_bytes) * h);
  const unsigned char* src = pixmap.samples();
  int src_stride = pixmap.stride();
  for (int y = 0; y < h; ++y) {
    std::memcpy(packed.data() + y * row_bytes, src + y * src_stride, row_bytes);
  }

  std::string b64 = base64::encode(packed.data(), packed.size());

  std::string result;
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    result += "\x1b_G";
    if (first) {
      result += "a=T,t=d,f=24,s=" + std::to_string(w) + ",v=" + std::to_string(h);
      if (image_id > 0) {
        result += ",i=" + std::to_string(image_id);
      }
      result += ",m=" + std::string(last ? "0" : "1");
      first = false;
    } else {
      result += "m=" + std::string(last ? "0" : "1");
    }
    result += ";";
    result.append(b64, offset, chunk_len);
    result += "\x1b\\";

    offset += chunk_len;
  }

  return result;
}

std::string delete_image(uint32_t image_id) {
  return "\x1b_Ga=d,d=i,i=" + std::to_string(image_id) + "\x1b\\";
}

} // namespace kitty
