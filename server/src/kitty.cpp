#include "kitty.h"

#include "base64.h"
#include "pixmap.h"

#include <cstring>
#include <vector>

namespace kitty {

static constexpr size_t CHUNK_SIZE = 4096;

// Kitty Unicode placeholder diacritics table (from Kitty source).
// Each entry is a combining character used to encode row/column indices.
// clang-format off
static constexpr uint32_t DIACRITICS[] = {
  0x0305, 0x030D, 0x030E, 0x0310, 0x0312, 0x033D, 0x033E, 0x033F,
  0x0346, 0x034A, 0x034B, 0x034C, 0x0350, 0x0351, 0x0352, 0x0357,
  0x035B, 0x0363, 0x0364, 0x0365, 0x0366, 0x0367, 0x0368, 0x0369,
  0x036A, 0x036B, 0x036C, 0x036D, 0x036E, 0x036F, 0x0483, 0x0484,
  0x0485, 0x0486, 0x0487, 0x0592, 0x0593, 0x0594, 0x0595, 0x0597,
  0x0598, 0x0599, 0x059C, 0x059D, 0x059E, 0x059F, 0x05A0, 0x05A1,
  0x05A8, 0x05A9, 0x05AB, 0x05AC, 0x05AF, 0x05C4, 0x0610, 0x0611,
  0x0612, 0x0613, 0x0614, 0x0615, 0x0616, 0x0617, 0x0657, 0x0658,
  0x0659, 0x065A, 0x065B, 0x065D, 0x065E, 0x06D6, 0x06D7, 0x06D8,
  0x06D9, 0x06DA, 0x06DB, 0x06DC, 0x06DF, 0x06E0, 0x06E1, 0x06E2,
  0x06E4, 0x06E7, 0x06E8, 0x06EB, 0x06EC, 0x0730, 0x0732, 0x0733,
  0x0735, 0x0736, 0x073A, 0x073D, 0x073F, 0x0740, 0x0741, 0x0743,
  0x0745, 0x0747, 0x0749, 0x074A, 0x07EB, 0x07EC, 0x07ED, 0x07EE,
  0x07EF, 0x07F0, 0x07F1, 0x07F3, 0x0816, 0x0817, 0x0818, 0x0819,
  0x081B, 0x081C, 0x081D, 0x081E, 0x081F, 0x0820, 0x0821, 0x0822,
  0x0823, 0x0825, 0x0826, 0x0827, 0x0829, 0x082A, 0x082B, 0x082C,
  0x082D, 0x0951, 0x0953, 0x0954, 0x0F82, 0x0F83, 0x0F86, 0x0F87,
  0x135D, 0x135E, 0x135F, 0x17DD, 0x193A, 0x1A17, 0x1A75, 0x1A76,
  0x1A77, 0x1A78, 0x1A79, 0x1A7A, 0x1A7B, 0x1A7C, 0x1B6B, 0x1B6D,
  0x1B6E, 0x1B6F, 0x1B70, 0x1B71, 0x1B72, 0x1B73, 0x1CD0, 0x1CD1,
  0x1CD2, 0x1CDA, 0x1CDB, 0x1CE0, 0x1DC0, 0x1DC1, 0x1DC3, 0x1DC4,
  0x1DC5, 0x1DC6, 0x1DC7, 0x1DC8, 0x1DC9, 0x1DCB, 0x1DCC, 0x1DD1,
  0x1DD2, 0x1DD3, 0x1DD4, 0x1DD5, 0x1DD6, 0x1DD7, 0x1DD8, 0x1DD9,
  0x1DDA, 0x1DDB, 0x1DDC, 0x1DDD, 0x1DDE, 0x1DDF, 0x1DE0, 0x1DE1,
  0x1DE2, 0x1DE3, 0x1DE4, 0x1DE5, 0x1DE6, 0x1DFE, 0x20D0, 0x20D1,
  0x20D4, 0x20D5, 0x20D6, 0x20D7, 0x20DB, 0x20DC, 0x20E1, 0x20E7,
  0x20E9, 0x20F0, 0xFE20, 0xFE21, 0xFE22, 0xFE23, 0xFE24, 0xFE25,
  0xFE26, 0xFE27, 0xFE28, 0xFE29, 0xFE2A, 0xFE2B, 0xFE2C, 0xFE2D,
  0xFE2E, 0xFE2F, 0x101FD, 0x10A0F, 0x10A38, 0xA670, 0xA671, 0xA672,
  0xA674, 0xA675, 0xA676, 0xA677, 0xA678, 0xA679, 0xA67A, 0xA67B,
  0xA67C, 0xA67D, 0xA69E, 0xA69F, 0xA6F0, 0xA6F1, 0xA8E0, 0xA8E1,
  0xA8E2, 0xA8E3, 0xA8E4, 0xA8E5, 0xA8E6, 0xA8E7, 0xA8E8, 0xA8E9,
  0xA8EA, 0xA8EB, 0xA8EC, 0xA8ED, 0xA8EE, 0xA8EF, 0xA8F0, 0xA8F1,
};
// clang-format on

static constexpr size_t DIACRITICS_COUNT = sizeof(DIACRITICS) / sizeof(DIACRITICS[0]);

/// Encode a Unicode code point as UTF-8 and append to the string.
static void append_utf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  }
  else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

/// Strip stride padding from a pixmap into a tightly packed buffer.
static std::vector<unsigned char> pack_pixels(const Pixmap& pixmap) {
  int w = pixmap.width();
  int h = pixmap.height();
  int comp = pixmap.components();
  int row_bytes = w * comp;

  std::vector<unsigned char> packed(static_cast<size_t>(row_bytes) * h);
  const unsigned char* src = pixmap.samples();
  int src_stride = pixmap.stride();
  for (int y = 0; y < h; ++y) {
    std::memcpy(packed.data() + y * row_bytes, src + y * src_stride, row_bytes);
  }
  return packed;
}

/// Wrap a single APC sequence in tmux DCS passthrough (doubles ESC bytes).
static std::string wrap_one(const std::string& seq) {
  std::string inner;
  inner.reserve(seq.size() + 32);
  for (char ch : seq) {
    if (ch == '\x1b') {
      inner += "\x1b\x1b";
    }
    else {
      inner += ch;
    }
  }
  return "\x1bPtmux;" + inner + "\x1b\\";
}

std::string wrap_tmux(const std::string& escape) {
  // Each APC sequence (\x1b_G...\x1b\\) must be wrapped individually.
  // Base64 payload never contains ESC, so \x1b_ safely marks APC starts.
  std::string result;
  size_t pos = 0;
  while (pos < escape.size()) {
    size_t start = escape.find("\x1b_G", pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = escape.find("\x1b\\", start + 3);
    if (end == std::string::npos) {
      break;
    }
    end += 2; // include the \x1b\\ itself
    result += wrap_one(escape.substr(start, end - start));
    pos = end;
  }
  return result;
}

std::string encode(const Pixmap& pixmap, uint32_t image_id, uint32_t placement_id) {
  int w = pixmap.width();
  int h = pixmap.height();

  auto packed = pack_pixels(pixmap);
  std::string b64 = base64::encode(packed);

  std::string result;
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    std::string chunk = "\x1b_G";
    if (first) {
      chunk += "a=T,t=d,f=24,q=2,s=" + std::to_string(w) + ",v=" + std::to_string(h);
      if (image_id > 0) {
        chunk += ",i=" + std::to_string(image_id);
      }
      if (placement_id > 0) {
        chunk += ",p=" + std::to_string(placement_id);
      }
      chunk += ",m=" + std::string(last ? "0" : "1");
      first = false;
    }
    else {
      chunk += "m=" + std::string(last ? "0" : "1");
    }
    chunk += ";";
    chunk.append(b64, offset, chunk_len);
    chunk += "\x1b\\";
    result += chunk;

    offset += chunk_len;
  }

  return result;
}

std::string transmit(const Pixmap& pixmap, uint32_t image_id) {
  int w = pixmap.width();
  int h = pixmap.height();

  auto packed = pack_pixels(pixmap);
  std::string b64 = base64::encode(packed);

  std::string result;
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    std::string chunk = "\x1b_G";
    if (first) {
      chunk += "a=t,t=d,f=24,q=2,s=" + std::to_string(w) + ",v=" + std::to_string(h);
      chunk += ",i=" + std::to_string(image_id);
      chunk += ",m=" + std::string(last ? "0" : "1");
      first = false;
    }
    else {
      chunk += "m=" + std::string(last ? "0" : "1");
    }
    chunk += ";";
    chunk.append(b64, offset, chunk_len);
    chunk += "\x1b\\";
    result += chunk;

    offset += chunk_len;
  }

  return result;
}

std::string place(uint32_t image_id, int src_x, int src_y, int src_w, int src_h) {
  return "\x1b_Ga=p,q=2,i=" + std::to_string(image_id) + ",p=1" + ",x=" + std::to_string(src_x) + ",y=" + std::to_string(src_y) + ",w=" + std::to_string(src_w)
         + ",h=" + std::to_string(src_h) + "\x1b\\";
}

std::string delete_image(uint32_t image_id) {
  return "\x1b_Ga=d,d=i,q=2,i=" + std::to_string(image_id) + "\x1b\\";
}

std::string encode_tmux(const Pixmap& pixmap, uint32_t image_id, int cell_width_px, int cell_height_px) {
  int w = pixmap.width();
  int h = pixmap.height();
  int cols = (w + cell_width_px - 1) / cell_width_px;
  int rows = (h + cell_height_px - 1) / cell_height_px;

  auto packed = pack_pixels(pixmap);
  std::string b64 = base64::encode(packed);

  // Build chunked transmit+display with Unicode placeholders (matches kitten icat).
  // First chunk: a=T (transmit+display), U=1 (unicode placeholders), q=2 (suppress responses)
  std::string upload;
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    std::string chunk = "\x1b_G";
    if (first) {
      chunk += "a=T,U=1,t=d,f=24,q=2,s=" + std::to_string(w) + ",v=" + std::to_string(h);
      chunk += ",i=" + std::to_string(image_id);
      chunk += ",c=" + std::to_string(cols) + ",r=" + std::to_string(rows);
      chunk += ",m=" + std::string(last ? "0" : "1");
      first = false;
    }
    else {
      chunk += "m=" + std::string(last ? "0" : "1");
    }
    chunk += ";";
    chunk.append(b64, offset, chunk_len);
    chunk += "\x1b\\";
    upload += chunk;

    offset += chunk_len;
  }

  // Wrap all APC sequences in DCS passthrough
  std::string result = wrap_tmux(upload);

  // Build Unicode placeholder text.
  // Foreground color encodes image_id: colon-separated, MSB-first (matches kitten icat).
  uint8_t id_b2 = (image_id >> 16) & 0xFF;
  uint8_t id_b1 = (image_id >> 8) & 0xFF;
  uint8_t id_b0 = image_id & 0xFF;
  result += "\x1b[38:2:" + std::to_string(id_b2) + ":" + std::to_string(id_b1) + ":" + std::to_string(id_b0) + "m";

  // Third diacritic encodes the most significant byte (bits 24-31) of image_id
  uint8_t id_b3 = (image_id >> 24) & 0xFF;

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      append_utf8(result, 0x10EEEE);
      if (static_cast<size_t>(row) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[row]);
      }
      if (static_cast<size_t>(col) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[col]);
      }
      if (static_cast<size_t>(id_b3) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[id_b3]);
      }
    }
    if (row < rows - 1) {
      result += "\n\r";
    }
  }

  result += "\x1b[39m";

  return result;
}

std::string transmit_tmux(const Pixmap& pixmap, uint32_t image_id, int cols, int rows) {
  int w = pixmap.width();
  int h = pixmap.height();

  auto packed = pack_pixels(pixmap);
  std::string b64 = base64::encode(packed);

  std::string upload;
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    std::string chunk = "\x1b_G";
    if (first) {
      chunk += "a=T,U=1,t=d,f=24,q=2,s=" + std::to_string(w) + ",v=" + std::to_string(h);
      chunk += ",i=" + std::to_string(image_id);
      chunk += ",c=" + std::to_string(cols) + ",r=" + std::to_string(rows);
      chunk += ",m=" + std::string(last ? "0" : "1");
      first = false;
    }
    else {
      chunk += "m=" + std::string(last ? "0" : "1");
    }
    chunk += ";";
    chunk.append(b64, offset, chunk_len);
    chunk += "\x1b\\";
    upload += chunk;

    offset += chunk_len;
  }

  return wrap_tmux(upload);
}

std::string placeholders(uint32_t image_id, int first_row, int num_rows, int num_cols) {
  std::string result;

  uint8_t id_b2 = (image_id >> 16) & 0xFF;
  uint8_t id_b1 = (image_id >> 8) & 0xFF;
  uint8_t id_b0 = image_id & 0xFF;
  uint8_t id_b3 = (image_id >> 24) & 0xFF;

  result += "\x1b[38:2:" + std::to_string(id_b2) + ":" + std::to_string(id_b1) + ":" + std::to_string(id_b0) + "m";

  for (int r = 0; r < num_rows; ++r) {
    int row = first_row + r;
    for (int col = 0; col < num_cols; ++col) {
      append_utf8(result, 0x10EEEE);
      if (static_cast<size_t>(row) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[row]);
      }
      if (static_cast<size_t>(col) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[col]);
      }
      if (static_cast<size_t>(id_b3) < DIACRITICS_COUNT) {
        append_utf8(result, DIACRITICS[id_b3]);
      }
    }
    if (r < num_rows - 1) {
      result += "\n\r";
    }
  }

  result += "\x1b[39m";
  return result;
}

std::string delete_image_tmux(uint32_t image_id) {
  return wrap_tmux(delete_image(image_id));
}

std::string delete_all_placements() {
  return "\x1b_Ga=d,d=a,q=2\x1b\\";
}

std::string delete_all_placements_tmux() {
  return wrap_tmux(delete_all_placements());
}

} // namespace kitty
