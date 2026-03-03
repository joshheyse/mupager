#include "pixmap.h"

#include <algorithm>
#include <cstring>

void PixmapDeleter::operator()(fz_pixmap* pix) const {
  fz_drop_pixmap(ctx, pix);
}

Pixmap::Pixmap(fz_context* ctx, fz_pixmap* pix)
    : pix_(pix, PixmapDeleter{ctx}) {}

int Pixmap::width() const {
  return fz_pixmap_width(pix_.get_deleter().ctx, pix_.get());
}

int Pixmap::height() const {
  return fz_pixmap_height(pix_.get_deleter().ctx, pix_.get());
}

int Pixmap::stride() const {
  return fz_pixmap_stride(pix_.get_deleter().ctx, pix_.get());
}

int Pixmap::components() const {
  return fz_pixmap_components(pix_.get_deleter().ctx, pix_.get());
}

unsigned char* Pixmap::samples() {
  return fz_pixmap_samples(pix_.get_deleter().ctx, pix_.get());
}

const unsigned char* Pixmap::samples() const {
  return fz_pixmap_samples(pix_.get_deleter().ctx, pix_.get());
}

std::vector<unsigned char> Pixmap::pack_pixels() const {
  int w = width();
  int h = height();
  int comp = components();
  int row_bytes = w * comp;
  int src_stride = stride();

  std::vector<unsigned char> packed(static_cast<size_t>(row_bytes) * h);
  const unsigned char* src = samples();
  for (int y = 0; y < h; ++y) {
    std::memcpy(packed.data() + y * row_bytes, src + y * src_stride, row_bytes);
  }
  return packed;
}

void Pixmap::invert() {
  int h = height();
  int s = stride();
  int row_bytes = width() * components();
  unsigned char* data = samples();
  for (int y = 0; y < h; ++y) {
    unsigned char* row = data + y * s;
    for (int x = 0; x < row_bytes; ++x) {
      row[x] = 255 - row[x];
    }
  }
}

void Pixmap::highlight_rect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
  int w = width();
  int h = height();
  int comp = components();
  int s = stride();

  int x0 = std::max(0, rx);
  int y0 = std::max(0, ry);
  int x1 = std::min(w, rx + rw);
  int y1 = std::min(h, ry + rh);

  unsigned char* data = samples();
  uint8_t color[3] = {r, g, b};
  int inv_alpha = 255 - alpha;

  for (int y = y0; y < y1; ++y) {
    unsigned char* row = data + y * s + x0 * comp;
    for (int x = x0; x < x1; ++x) {
      for (int c = 0; c < std::min(comp, 3); ++c) {
        row[c] = static_cast<unsigned char>((row[c] * inv_alpha + color[c] * alpha) / 255);
      }
      row += comp;
    }
  }
}

std::vector<unsigned char> Pixmap::png_data() const {
  fz_context* ctx = pix_.get_deleter().ctx;
  fz_buffer* buf = fz_new_buffer_from_pixmap_as_png(ctx, pix_.get(), fz_default_color_params);
  unsigned char* data;
  size_t len = fz_buffer_storage(ctx, buf, &data);
  std::vector<unsigned char> result(data, data + len);
  fz_drop_buffer(ctx, buf);
  return result;
}
