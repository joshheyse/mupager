#pragma once

#include <cstdint>
#include <string>

class Pixmap;

/// Kitty graphics protocol escape sequence encoding.
namespace kitty {

/// Kitty graphics protocol action for transmission commands.
enum class TransmitAction : char {
  Transmit = 't',        ///< a=t — transmit only, store for later placement.
  TransmitDisplay = 'T', ///< a=T — transmit and display immediately.
  Query = 'q',           ///< a=q — test-load without storing.
};

/// Pixel data format.
enum class PixelFormat : int {
  Rgb = 24,  ///< f=24 — 24-bit RGB, 3 bytes per pixel.
  Rgba = 32, ///< f=32 — 32-bit RGBA, 4 bytes per pixel.
  Png = 100, ///< f=100 — PNG-encoded data.
};

/// How image data is delivered to the terminal.
enum class Medium : char {
  Direct = 'd',       ///< t=d — inline base64 in the escape sequence.
  File = 'f',         ///< t=f — base64-encoded file path; file left on disk.
  TempFile = 't',     ///< t=t — base64-encoded file path; terminal deletes after reading.
  SharedMemory = 's', ///< t=s — base64-encoded POSIX shared memory object name.
};

/// Compression applied to the payload.
enum class Compression : char {
  None = 0,  ///< No compression.
  Zlib = 'z' ///< o=z — RFC 1950 zlib deflate.
};

/// Target specifier for delete commands.
enum class DeleteTarget : char {
  All = 'a',         ///< d=a — all visible placements.
  ById = 'i',        ///< d=i — by image ID, optionally by placement ID.
  ByNumber = 'n',    ///< d=n — by image number.
  AtCursor = 'c',    ///< d=c — at cursor position.
  AtPosition = 'p',  ///< d=p — at cell position (x, y).
  AtPositionZ = 'q', ///< d=q — at cell position + z-index.
  ByColumn = 'x',    ///< d=x — by column.
  ByRow = 'y',       ///< d=y — by row.
  ByZIndex = 'z',    ///< d=z — by z-index.
  ByIdRange = 'r',   ///< d=r — by image ID range (x=min, y=max).
  Frames = 'f',      ///< d=f — animation frames for an image.
};

/// Animation playback state.
enum class AnimationState : int {
  Stop = 1,    ///< s=1 — stop playback.
  Loading = 2, ///< s=2 — run, waiting for more frames.
  Run = 3,     ///< s=3 — run with normal looping.
};

/// @brief Kitty graphics protocol command for transmitting pixel data.
///
/// Covers direct transmit (a=t), transmit+display (a=T), query (a=q), and tmux
/// unicode placeholder variants. The serialize() method handles 4096-byte chunking.
struct TransmitCommand {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t image_id = 0;
  uint32_t placement_id = 0;
  TransmitAction action = TransmitAction::Transmit; ///< a= — transmit action.
  PixelFormat format = PixelFormat::Rgb;            ///< f= — pixel data format.
  Medium medium = Medium::Direct;                   ///< t= — data delivery medium.
  Compression compression = Compression::None;      ///< o= — payload compression.
  bool unicode = false;                             ///< U=1 — unicode placeholders (for tmux).
  uint32_t columns = 0;                             ///< c= — placement columns (0 = omit).
  uint32_t rows = 0;                                ///< r= — placement rows (0 = omit).
  uint32_t data_size = 0;                           ///< S= — bytes to read from file/shm (0 = omit).
  uint32_t data_offset = 0;                         ///< O= — byte offset into file/shm (0 = omit).
  uint32_t image_number = 0;                        ///< I= — image number (0 = omit), mutually exclusive with i=.
  int32_t z_index = 0;                              ///< z= — layer ordering (0 = omit).
  uint32_t cell_x_offset = 0;                       ///< X= — sub-cell pixel X offset (0 = omit).
  uint32_t cell_y_offset = 0;                       ///< Y= — sub-cell pixel Y offset (0 = omit).
  bool do_not_move_cursor = false;                  ///< C=1 — suppress cursor movement.
  uint32_t parent_image_id = 0;                     ///< P= — parent image ID for relative placement (0 = omit).
  uint32_t parent_placement_id = 0;                 ///< Q= — parent placement ID for relative placement (0 = omit).
  int32_t horizontal_offset = 0;                    ///< H= — cells from parent (0 = omit).
  int32_t vertical_offset = 0;                      ///< V= — cells from parent (0 = omit).
  uint32_t quiet = 2;                               ///< q= — response suppression level.

  /// @brief Serialize to chunked APC escape sequences with the given base64 payload.
  std::string serialize(const std::string& b64) const;
};

/// @brief Kitty graphics protocol command for placing a previously transmitted image.
struct PlaceCommand {
  uint32_t image_id = 0;
  uint32_t placement_id = 1;
  int src_x = 0;
  int src_y = 0;
  int src_w = 0;
  int src_h = 0;
  uint32_t columns = 0;             ///< c= — placement columns (0 = omit).
  uint32_t rows = 0;                ///< r= — placement rows (0 = omit).
  int32_t z_index = 0;              ///< z= — layer ordering (0 = omit).
  uint32_t cell_x_offset = 0;       ///< X= — sub-cell pixel X offset (0 = omit).
  uint32_t cell_y_offset = 0;       ///< Y= — sub-cell pixel Y offset (0 = omit).
  bool do_not_move_cursor = false;  ///< C=1 — suppress cursor movement.
  bool unicode = false;             ///< U=1 — unicode placeholders.
  uint32_t parent_image_id = 0;     ///< P= — parent image ID for relative placement (0 = omit).
  uint32_t parent_placement_id = 0; ///< Q= — parent placement ID for relative placement (0 = omit).
  int32_t horizontal_offset = 0;    ///< H= — cells from parent (0 = omit).
  int32_t vertical_offset = 0;      ///< V= — cells from parent (0 = omit).
  uint32_t quiet = 2;               ///< q= — response suppression level.

  /// @brief Serialize to a single APC escape sequence.
  std::string serialize() const;
};

/// @brief Kitty graphics protocol command for deleting images or placements.
///
/// The target field selects the delete mode (d= key), and the free flag
/// selects uppercase (free image data) vs lowercase (placements only).
struct DeleteCommand {
  DeleteTarget target = DeleteTarget::All; ///< d= — what to delete.
  bool free = false;                       ///< true → uppercase target (free data), false → lowercase (placements only).
  uint32_t image_id = 0;                   ///< i= — image ID (for BY_ID, FRAMES targets).
  uint32_t placement_id = 0;               ///< p= — placement ID (optional, for BY_ID, BY_NUMBER targets).
  uint32_t image_number = 0;               ///< I= — image number (for BY_NUMBER target).
  uint32_t x = 0;                          ///< x= — column or range min (for AT_POSITION, AT_POSITION_Z, BY_ID_RANGE, BY_COLUMN).
  uint32_t y = 0;                          ///< y= — row or range max (for AT_POSITION, AT_POSITION_Z, BY_ID_RANGE, BY_ROW).
  int32_t z = 0;                           ///< z= — z-index (for AT_POSITION_Z, BY_Z_INDEX).
  uint32_t quiet = 2;                      ///< q= — response suppression level.

  /// @brief Serialize to a single APC escape sequence.
  std::string serialize() const;
};

/// @brief Kitty graphics protocol command for transferring animation frame data (a=f).
struct AnimationFrameCommand {
  uint32_t image_id = 0;                       ///< i= — target image.
  PixelFormat format = PixelFormat::Rgba;      ///< f= — pixel format.
  Medium medium = Medium::Direct;              ///< t= — data delivery.
  Compression compression = Compression::None; ///< o= — compression.
  uint32_t data_size = 0;                      ///< S= — bytes to read.
  uint32_t data_offset = 0;                    ///< O= — byte offset.
  uint32_t x = 0;                              ///< x= — frame rect X offset.
  uint32_t y = 0;                              ///< y= — frame rect Y offset.
  uint32_t width = 0;                          ///< s= — frame rect width.
  uint32_t height = 0;                         ///< v= — frame rect height.
  uint32_t base_frame = 0;                     ///< c= — base frame to copy canvas from.
  uint32_t edit_frame = 0;                     ///< r= — edit existing frame in-place.
  int32_t frame_gap = 0;                       ///< z= — display duration in ms.
  uint32_t composition_mode = 0;               ///< X= — 0=blend, 1=replace.
  uint32_t background_color = 0;               ///< Y= — 32-bit RGBA canvas color.
  uint32_t quiet = 2;                          ///< q= — response suppression.

  /// @brief Serialize to chunked APC escape sequences with the given base64 payload.
  std::string serialize(const std::string& b64) const;
};

/// @brief Kitty graphics protocol command for controlling animation playback (a=a).
struct AnimationControlCommand {
  uint32_t image_id = 0;                       ///< i= — target image.
  uint32_t image_number = 0;                   ///< I= — alternative: by image number.
  AnimationState state = AnimationState::Stop; ///< s= — playback state.
  uint32_t loop_count = 0;                     ///< v= — 0=no change, 1=infinite, n=loop n-1 times.
  uint32_t current_frame = 0;                  ///< c= — jump to frame.
  uint32_t target_frame = 0;                   ///< r= — frame whose gap to modify.
  int32_t frame_gap = 0;                       ///< z= — gap to set in ms (0 = omit).
  uint32_t quiet = 2;                          ///< q= — response suppression.

  /// @brief Serialize to a single APC escape sequence.
  std::string serialize() const;
};

/// @brief Kitty graphics protocol command for composing one frame onto another (a=c).
struct ComposeCommand {
  uint32_t image_id = 0;   ///< i= — target image.
  uint32_t src_frame = 0;  ///< r= — source frame number.
  uint32_t dst_frame = 0;  ///< c= — destination frame number.
  uint32_t src_x = 0;      ///< x= — source X offset.
  uint32_t src_y = 0;      ///< y= — source Y offset.
  uint32_t width = 0;      ///< w= — rect width (0 = full).
  uint32_t height = 0;     ///< h= — rect height (0 = full).
  uint32_t dst_x = 0;      ///< X= — destination X offset.
  uint32_t dst_y = 0;      ///< Y= — destination Y offset.
  uint32_t blend_mode = 0; ///< C= — 0=alpha-blend, 1=replace.
  uint32_t quiet = 2;      ///< q= — response suppression.

  /// @brief Serialize to a single APC escape sequence.
  std::string serialize() const;
};

/// @brief Wrap APC escape sequences in DCS passthrough for tmux.
/// Each APC sequence is individually wrapped with doubled ESC bytes.
std::string wrap_tmux(const std::string& escape);

/// @brief Encode a pixmap as Kitty graphics protocol escape sequences for direct terminal display.
/// @param placement_id Optional placement ID for later replacement via place().
std::string encode(const Pixmap& pixmap, uint32_t image_id = 0, uint32_t placement_id = 0);

/// @brief Transmit a pixmap to the terminal without displaying it (a=t).
/// When cols and rows are provided (> 0), uses display+unicode placeholder mode (a=T, U=1).
/// The image is stored by the terminal and can be placed later with place().
std::string transmit(const Pixmap& pixmap, uint32_t image_id, int cols = 0, int rows = 0);

/// @brief Place a previously transmitted image with source rect cropping (a=p).
/// Uses placement id 1, so repeated calls replace the previous placement.
std::string place(uint32_t image_id, int src_x, int src_y, int src_w, int src_h);

/// @brief Return an escape sequence that deletes the image with the given ID.
std::string delete_image(uint32_t image_id);

/// @brief Generate Unicode placeholder text for a range of cell rows.
/// @param image_id The image ID encoded in the foreground color.
/// @param first_row First cell row index to display.
/// @param num_rows Number of cell rows to display.
/// @param num_cols Number of cell columns per row.
/// @param first_col First cell column index to display (default 0).
std::string placeholders(uint32_t image_id, int first_row, int num_rows, int num_cols, int first_col = 0);

/// @brief Delete all image placements (but keep images in memory).
std::string delete_all_placements();

} // namespace kitty
