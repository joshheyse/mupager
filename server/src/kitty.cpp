#include "kitty.h"

#include "base64.h"
#include "pixmap.h"

#include <cctype>

namespace kitty {

static constexpr size_t CHUNK_SIZE = 4096;

static constexpr char ESC = '\x1b';
static constexpr auto APC_START = "\x1b_G";
static constexpr auto ST = "\x1b\\";
static constexpr auto DCS_TMUX_START = "\x1bPtmux;";
static constexpr auto SGR_RESET_FG = "\x1b[39m";
static constexpr uint32_t PLACEHOLDER_CHAR = 0x10EEEE;

// Kitty Unicode placeholder diacritics table (from Kitty source).
// Each entry is a combining character used to encode row/column indices.
// clang-format off
static constexpr uint32_t DIACRITICS[] = {
  0x0305, 0x030D, 0x030E,  0x0310,  0x0312,  0x033D, 0x033E, 0x033F,
  0x0346, 0x034A, 0x034B,  0x034C,  0x0350,  0x0351, 0x0352, 0x0357,
  0x035B, 0x0363, 0x0364,  0x0365,  0x0366,  0x0367, 0x0368, 0x0369,
  0x036A, 0x036B, 0x036C,  0x036D,  0x036E,  0x036F, 0x0483, 0x0484,
  0x0485, 0x0486, 0x0487,  0x0592,  0x0593,  0x0594, 0x0595, 0x0597,
  0x0598, 0x0599, 0x059C,  0x059D,  0x059E,  0x059F, 0x05A0, 0x05A1,
  0x05A8, 0x05A9, 0x05AB,  0x05AC,  0x05AF,  0x05C4, 0x0610, 0x0611,
  0x0612, 0x0613, 0x0614,  0x0615,  0x0616,  0x0617, 0x0657, 0x0658,
  0x0659, 0x065A, 0x065B,  0x065D,  0x065E,  0x06D6, 0x06D7, 0x06D8,
  0x06D9, 0x06DA, 0x06DB,  0x06DC,  0x06DF,  0x06E0, 0x06E1, 0x06E2,
  0x06E4, 0x06E7, 0x06E8,  0x06EB,  0x06EC,  0x0730, 0x0732, 0x0733,
  0x0735, 0x0736, 0x073A,  0x073D,  0x073F,  0x0740, 0x0741, 0x0743,
  0x0745, 0x0747, 0x0749,  0x074A,  0x07EB,  0x07EC, 0x07ED, 0x07EE,
  0x07EF, 0x07F0, 0x07F1,  0x07F3,  0x0816,  0x0817, 0x0818, 0x0819,
  0x081B, 0x081C, 0x081D,  0x081E,  0x081F,  0x0820, 0x0821, 0x0822,
  0x0823, 0x0825, 0x0826,  0x0827,  0x0829,  0x082A, 0x082B, 0x082C,
  0x082D, 0x0951, 0x0953,  0x0954,  0x0F82,  0x0F83, 0x0F86, 0x0F87,
  0x135D, 0x135E, 0x135F,  0x17DD,  0x193A,  0x1A17, 0x1A75, 0x1A76,
  0x1A77, 0x1A78, 0x1A79,  0x1A7A,  0x1A7B,  0x1A7C, 0x1B6B, 0x1B6D,
  0x1B6E, 0x1B6F, 0x1B70,  0x1B71,  0x1B72,  0x1B73, 0x1CD0, 0x1CD1,
  0x1CD2, 0x1CDA, 0x1CDB,  0x1CE0,  0x1DC0,  0x1DC1, 0x1DC3, 0x1DC4,
  0x1DC5, 0x1DC6, 0x1DC7,  0x1DC8,  0x1DC9,  0x1DCB, 0x1DCC, 0x1DD1,
  0x1DD2, 0x1DD3, 0x1DD4,  0x1DD5,  0x1DD6,  0x1DD7, 0x1DD8, 0x1DD9,
  0x1DDA, 0x1DDB, 0x1DDC,  0x1DDD,  0x1DDE,  0x1DDF, 0x1DE0, 0x1DE1,
  0x1DE2, 0x1DE3, 0x1DE4,  0x1DE5,  0x1DE6,  0x1DFE, 0x20D0, 0x20D1,
  0x20D4, 0x20D5, 0x20D6,  0x20D7,  0x20DB,  0x20DC, 0x20E1, 0x20E7,
  0x20E9, 0x20F0, 0xFE20,  0xFE21,  0xFE22,  0xFE23, 0xFE24, 0xFE25,
  0xFE26, 0xFE27, 0xFE28,  0xFE29,  0xFE2A,  0xFE2B, 0xFE2C, 0xFE2D,
  0xFE2E, 0xFE2F, 0x101FD, 0x10A0F, 0x10A38, 0xA670, 0xA671, 0xA672,
  0xA674, 0xA675, 0xA676,  0xA677,  0xA678,  0xA679, 0xA67A, 0xA67B,
  0xA67C, 0xA67D, 0xA69E,  0xA69F,  0xA6F0,  0xA6F1, 0xA8E0, 0xA8E1,
  0xA8E2, 0xA8E3, 0xA8E4,  0xA8E5,  0xA8E6,  0xA8E7, 0xA8E8, 0xA8E9,
  0xA8EA, 0xA8EB, 0xA8EC,  0xA8ED,  0xA8EE,  0xA8EF, 0xA8F0, 0xA8F1,
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

/// Serialize a header + base64 payload across 4096-byte APC chunks.
/// First chunk: ESC_G{header},m=0|1;{chunk}ST
/// Subsequent:  ESC_Gm=0|1;{chunk}ST
static std::string serialize_chunked(const std::string& header, const std::string& b64) {
  // Each chunk: ESC_G (3) + header/m= + ,m=X (4) + ; (1) + payload + ST (2)
  size_t num_chunks = (b64.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
  size_t overhead = header.size() + 10 + (num_chunks > 1 ? (num_chunks - 1) * 9 : 0);

  std::string result;
  result.reserve(b64.size() + overhead);
  size_t offset = 0;
  bool first = true;

  while (offset < b64.size()) {
    size_t remaining = b64.size() - offset;
    size_t chunk_len = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    bool last = (offset + chunk_len >= b64.size());

    result += APC_START;
    if (first) {
      result += header;
      result += ",m=";
      result += (last ? '0' : '1');
      first = false;
    }
    else {
      result += "m=";
      result += (last ? '0' : '1');
    }
    result += ';';
    result.append(b64, offset, chunk_len);
    result += ST;

    offset += chunk_len;
  }

  return result;
}

std::string TransmitCommand::serialize(const std::string& b64) const {
  std::string header;
  header.reserve(256);
  header += "a=";
  header += static_cast<char>(action);
  if (unicode) {
    header += ",U=1";
  }
  header += ",t=";
  header += static_cast<char>(medium);
  header += ",f=";
  header += std::to_string(static_cast<int>(format));
  if (compression != Compression::NONE) {
    header += ",o=";
    header += static_cast<char>(compression);
  }
  header += ",q=";
  header += std::to_string(quiet);
  header += ",s=";
  header += std::to_string(width);
  header += ",v=";
  header += std::to_string(height);
  if (image_id > 0) {
    header += ",i=";
    header += std::to_string(image_id);
  }
  if (placement_id > 0) {
    header += ",p=";
    header += std::to_string(placement_id);
  }
  if (image_number > 0) {
    header += ",I=";
    header += std::to_string(image_number);
  }
  if (columns > 0) {
    header += ",c=";
    header += std::to_string(columns);
  }
  if (rows > 0) {
    header += ",r=";
    header += std::to_string(rows);
  }
  if (data_size > 0) {
    header += ",S=";
    header += std::to_string(data_size);
  }
  if (data_offset > 0) {
    header += ",O=";
    header += std::to_string(data_offset);
  }
  if (z_index != 0) {
    header += ",z=";
    header += std::to_string(z_index);
  }
  if (cell_x_offset > 0) {
    header += ",X=";
    header += std::to_string(cell_x_offset);
  }
  if (cell_y_offset > 0) {
    header += ",Y=";
    header += std::to_string(cell_y_offset);
  }
  if (do_not_move_cursor) {
    header += ",C=1";
  }
  if (parent_image_id > 0) {
    header += ",P=";
    header += std::to_string(parent_image_id);
  }
  if (parent_placement_id > 0) {
    header += ",Q=";
    header += std::to_string(parent_placement_id);
  }
  if (horizontal_offset != 0) {
    header += ",H=";
    header += std::to_string(horizontal_offset);
  }
  if (vertical_offset != 0) {
    header += ",V=";
    header += std::to_string(vertical_offset);
  }
  return serialize_chunked(header, b64);
}

std::string PlaceCommand::serialize() const {
  std::string result;
  result.reserve(128);
  result += APC_START;
  result += "a=p,q=";
  result += std::to_string(quiet);
  result += ",i=";
  result += std::to_string(image_id);
  result += ",p=";
  result += std::to_string(placement_id);
  result += ",x=";
  result += std::to_string(src_x);
  result += ",y=";
  result += std::to_string(src_y);
  result += ",w=";
  result += std::to_string(src_w);
  result += ",h=";
  result += std::to_string(src_h);
  if (columns > 0) {
    result += ",c=";
    result += std::to_string(columns);
  }
  if (rows > 0) {
    result += ",r=";
    result += std::to_string(rows);
  }
  if (z_index != 0) {
    result += ",z=";
    result += std::to_string(z_index);
  }
  if (cell_x_offset > 0) {
    result += ",X=";
    result += std::to_string(cell_x_offset);
  }
  if (cell_y_offset > 0) {
    result += ",Y=";
    result += std::to_string(cell_y_offset);
  }
  if (do_not_move_cursor) {
    result += ",C=1";
  }
  if (unicode) {
    result += ",U=1";
  }
  if (parent_image_id > 0) {
    result += ",P=";
    result += std::to_string(parent_image_id);
  }
  if (parent_placement_id > 0) {
    result += ",Q=";
    result += std::to_string(parent_placement_id);
  }
  if (horizontal_offset != 0) {
    result += ",H=";
    result += std::to_string(horizontal_offset);
  }
  if (vertical_offset != 0) {
    result += ",V=";
    result += std::to_string(vertical_offset);
  }
  result += ST;
  return result;
}

std::string DeleteCommand::serialize() const {
  char target_char = static_cast<char>(target);
  if (free) {
    target_char = static_cast<char>(std::toupper(static_cast<unsigned char>(target_char)));
  }

  std::string result;
  result.reserve(80);
  result += APC_START;
  result += "a=d,d=";
  result += target_char;
  result += ",q=";
  result += std::to_string(quiet);

  switch (target) {
    case DeleteTarget::BY_ID:
    case DeleteTarget::FRAMES:
      if (image_id > 0) {
        result += ",i=";
        result += std::to_string(image_id);
      }
      if (placement_id > 0) {
        result += ",p=";
        result += std::to_string(placement_id);
      }
      break;
    case DeleteTarget::BY_NUMBER:
      if (image_number > 0) {
        result += ",I=";
        result += std::to_string(image_number);
      }
      if (placement_id > 0) {
        result += ",p=";
        result += std::to_string(placement_id);
      }
      break;
    case DeleteTarget::AT_POSITION:
      if (x > 0) {
        result += ",x=";
        result += std::to_string(x);
      }
      if (y > 0) {
        result += ",y=";
        result += std::to_string(y);
      }
      break;
    case DeleteTarget::AT_POSITION_Z:
      if (x > 0) {
        result += ",x=";
        result += std::to_string(x);
      }
      if (y > 0) {
        result += ",y=";
        result += std::to_string(y);
      }
      if (z != 0) {
        result += ",z=";
        result += std::to_string(z);
      }
      break;
    case DeleteTarget::BY_ID_RANGE:
      if (x > 0) {
        result += ",x=";
        result += std::to_string(x);
      }
      if (y > 0) {
        result += ",y=";
        result += std::to_string(y);
      }
      break;
    case DeleteTarget::BY_COLUMN:
      if (x > 0) {
        result += ",x=";
        result += std::to_string(x);
      }
      break;
    case DeleteTarget::BY_ROW:
      if (y > 0) {
        result += ",y=";
        result += std::to_string(y);
      }
      break;
    case DeleteTarget::BY_Z_INDEX:
      if (z != 0) {
        result += ",z=";
        result += std::to_string(z);
      }
      break;
    case DeleteTarget::ALL:
    case DeleteTarget::AT_CURSOR:
      break;
  }

  result += ST;
  return result;
}

std::string AnimationFrameCommand::serialize(const std::string& b64) const {
  std::string header;
  header.reserve(256);
  header += "a=f,t=";
  header += static_cast<char>(medium);
  header += ",f=";
  header += std::to_string(static_cast<int>(format));
  if (compression != Compression::NONE) {
    header += ",o=";
    header += static_cast<char>(compression);
  }
  header += ",q=";
  header += std::to_string(quiet);
  if (image_id > 0) {
    header += ",i=";
    header += std::to_string(image_id);
  }
  if (data_size > 0) {
    header += ",S=";
    header += std::to_string(data_size);
  }
  if (data_offset > 0) {
    header += ",O=";
    header += std::to_string(data_offset);
  }
  if (x > 0) {
    header += ",x=";
    header += std::to_string(x);
  }
  if (y > 0) {
    header += ",y=";
    header += std::to_string(y);
  }
  if (width > 0) {
    header += ",s=";
    header += std::to_string(width);
  }
  if (height > 0) {
    header += ",v=";
    header += std::to_string(height);
  }
  if (base_frame > 0) {
    header += ",c=";
    header += std::to_string(base_frame);
  }
  if (edit_frame > 0) {
    header += ",r=";
    header += std::to_string(edit_frame);
  }
  if (frame_gap != 0) {
    header += ",z=";
    header += std::to_string(frame_gap);
  }
  if (composition_mode > 0) {
    header += ",X=";
    header += std::to_string(composition_mode);
  }
  if (background_color > 0) {
    header += ",Y=";
    header += std::to_string(background_color);
  }
  return serialize_chunked(header, b64);
}

std::string AnimationControlCommand::serialize() const {
  std::string result;
  result.reserve(128);
  result += APC_START;
  result += "a=a,q=";
  result += std::to_string(quiet);
  if (image_id > 0) {
    result += ",i=";
    result += std::to_string(image_id);
  }
  if (image_number > 0) {
    result += ",I=";
    result += std::to_string(image_number);
  }
  result += ",s=";
  result += std::to_string(static_cast<int>(state));
  if (loop_count > 0) {
    result += ",v=";
    result += std::to_string(loop_count);
  }
  if (current_frame > 0) {
    result += ",c=";
    result += std::to_string(current_frame);
  }
  if (target_frame > 0) {
    result += ",r=";
    result += std::to_string(target_frame);
  }
  if (frame_gap != 0) {
    result += ",z=";
    result += std::to_string(frame_gap);
  }
  result += ST;
  return result;
}

std::string ComposeCommand::serialize() const {
  std::string result;
  result.reserve(128);
  result += APC_START;
  result += "a=c,q=";
  result += std::to_string(quiet);
  if (image_id > 0) {
    result += ",i=";
    result += std::to_string(image_id);
  }
  if (src_frame > 0) {
    result += ",r=";
    result += std::to_string(src_frame);
  }
  if (dst_frame > 0) {
    result += ",c=";
    result += std::to_string(dst_frame);
  }
  if (src_x > 0) {
    result += ",x=";
    result += std::to_string(src_x);
  }
  if (src_y > 0) {
    result += ",y=";
    result += std::to_string(src_y);
  }
  if (width > 0) {
    result += ",w=";
    result += std::to_string(width);
  }
  if (height > 0) {
    result += ",h=";
    result += std::to_string(height);
  }
  if (dst_x > 0) {
    result += ",X=";
    result += std::to_string(dst_x);
  }
  if (dst_y > 0) {
    result += ",Y=";
    result += std::to_string(dst_y);
  }
  if (blend_mode > 0) {
    result += ",C=";
    result += std::to_string(blend_mode);
  }
  result += ST;
  return result;
}

/// Wrap a single APC sequence in tmux DCS passthrough (doubles ESC bytes).
static std::string wrap_one(const std::string& seq) {
  std::string inner;
  inner.reserve(seq.size() + 32);
  for (char ch : seq) {
    if (ch == ESC) {
      inner += ESC;
      inner += ESC;
    }
    else {
      inner += ch;
    }
  }
  return DCS_TMUX_START + inner + ST;
}

std::string wrap_tmux(const std::string& escape) {
  // Each APC sequence (\x1b_G...\x1b\\) must be wrapped individually.
  // Base64 payload never contains ESC, so \x1b_ safely marks APC starts.
  std::string result;
  size_t pos = 0;
  while (pos < escape.size()) {
    size_t start = escape.find(APC_START, pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = escape.find(ST, start + 3);
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
  auto b64 = base64::encode(pixmap.png_data());
  TransmitCommand cmd;
  cmd.format = PixelFormat::PNG;
  cmd.width = pixmap.width();
  cmd.height = pixmap.height();
  cmd.image_id = image_id;
  cmd.placement_id = placement_id;
  cmd.action = TransmitAction::TRANSMIT_DISPLAY;
  return cmd.serialize(b64);
}

std::string transmit(const Pixmap& pixmap, uint32_t image_id, int cols, int rows) {
  auto b64 = base64::encode(pixmap.png_data());
  TransmitCommand cmd;
  cmd.format = PixelFormat::PNG;
  cmd.width = pixmap.width();
  cmd.height = pixmap.height();
  cmd.image_id = image_id;
  if (cols > 0 && rows > 0) {
    cmd.action = TransmitAction::TRANSMIT_DISPLAY;
    cmd.unicode = true;
    cmd.columns = cols;
    cmd.rows = rows;
  }
  return cmd.serialize(b64);
}

std::string place(uint32_t image_id, int src_x, int src_y, int src_w, int src_h) {
  PlaceCommand cmd;
  cmd.image_id = image_id;
  cmd.src_x = src_x;
  cmd.src_y = src_y;
  cmd.src_w = src_w;
  cmd.src_h = src_h;
  return cmd.serialize();
}

std::string delete_image(uint32_t image_id) {
  DeleteCommand cmd;
  cmd.target = DeleteTarget::BY_ID;
  cmd.image_id = image_id;
  return cmd.serialize();
}

std::string placeholders(uint32_t image_id, int first_row, int num_rows, int num_cols) {
  // Per cell: U+10EEEE (4 bytes) + up to 3 diacritics (up to 4 bytes each) = ~16 bytes
  // Plus SGR prefix (~25), newlines (2 per row), SGR reset (5)
  size_t estimated = 30 + static_cast<size_t>(num_rows) * static_cast<size_t>(num_cols) * 16 + static_cast<size_t>(num_rows) * 2;

  std::string result;
  result.reserve(estimated);

  uint8_t id_b2 = (image_id >> 16) & 0xFF;
  uint8_t id_b1 = (image_id >> 8) & 0xFF;
  uint8_t id_b0 = image_id & 0xFF;
  uint8_t id_b3 = (image_id >> 24) & 0xFF;

  result += "\x1b[38:2:";
  result += std::to_string(id_b2);
  result += ':';
  result += std::to_string(id_b1);
  result += ':';
  result += std::to_string(id_b0);
  result += 'm';

  for (int r = 0; r < num_rows; ++r) {
    int row = first_row + r;
    for (int col = 0; col < num_cols; ++col) {
      append_utf8(result, PLACEHOLDER_CHAR);
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

  result += SGR_RESET_FG;
  return result;
}

std::string delete_all_placements() {
  DeleteCommand cmd;
  cmd.target = DeleteTarget::ALL;
  return cmd.serialize();
}

} // namespace kitty
