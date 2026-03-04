#include "pixmap.h"

#include <algorithm>
#include <cstring>

void PixmapDeleter::operator()(fz_pixmap* pix) const {
  fz_drop_pixmap(ctx, pix);
}

Pixmap::Pixmap(fz_context* ctx, fz_pixmap* pix)
    : pix_(pix, PixmapDeleter{ctx}) {}

Pixmap Pixmap::from_pixels(fz_context* ctx, int w, int h, int comp, const unsigned char* data) {
  fz_colorspace* cs = (comp >= 3) ? fz_device_rgb(ctx) : fz_device_gray(ctx);
  fz_pixmap* pix = fz_new_pixmap(ctx, cs, w, h, nullptr, 0);
  int stride = fz_pixmap_stride(ctx, pix);
  int row_bytes = w * comp;
  unsigned char* dst = fz_pixmap_samples(ctx, pix);
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + y * stride, data + y * row_bytes, row_bytes);
  }
  return Pixmap(ctx, pix);
}

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

void Pixmap::highlight_rect(PixelRect rect, Color color, uint8_t alpha) {
  int w = width();
  int h = height();
  int comp = components();
  int s = stride();

  int x0 = std::max(0, rect.x);
  int y0 = std::max(0, rect.y);
  int x1 = std::min(w, rect.right());
  int y1 = std::min(h, rect.bottom());

  unsigned char* data = samples();
  uint8_t rgb[3] = {color.r, color.g, color.b};
  int inv_alpha = 255 - alpha;

  for (int y = y0; y < y1; ++y) {
    unsigned char* row = data + y * s + x0 * comp;
    for (int x = x0; x < x1; ++x) {
      for (int c = 0; c < std::min(comp, 3); ++c) {
        row[c] = static_cast<unsigned char>((row[c] * inv_alpha + rgb[c] * alpha) / 255);
      }
      row += comp;
    }
  }
}

void Pixmap::recolor(Color fg, Color bg, Color accent) {
  int h = height();
  int s = stride();
  int comp = components();
  int row_bytes = width() * comp;
  unsigned char* data = samples();
  bool has_accent = !accent.is_default;

  for (int y = 0; y < h; ++y) {
    unsigned char* row = data + y * s;
    for (int x = 0; x < row_bytes; x += comp) {
      int r = row[x];
      int g = row[x + 1];
      int b = row[x + 2];
      int lum = (r + g + b) / 3;
      int inv_lum = 255 - lum;

      // Base recolored values from luminance interpolation
      int out_r = (fg.r * inv_lum + bg.r * lum) / 255;
      int out_g = (fg.g * inv_lum + bg.g * lum) / 255;
      int out_b = (fg.b * inv_lum + bg.b * lum) / 255;

      // Tint saturated pixels toward accent color
      if (has_accent) {
        int max_c = std::max({r, g, b});
        int min_c = std::min({r, g, b});
        int chroma = max_c - min_c;
        // Saturation as fraction of max channel (0-255 scale)
        int sat = (max_c > 0) ? (chroma * 255 / max_c) : 0;
        // Blend toward accent proportional to saturation
        if (sat > 30) {
          out_r = (out_r * (255 - sat) + accent.r * sat) / 255;
          out_g = (out_g * (255 - sat) + accent.g * sat) / 255;
          out_b = (out_b * (255 - sat) + accent.b * sat) / 255;
        }
      }

      row[x] = static_cast<unsigned char>(out_r);
      row[x + 1] = static_cast<unsigned char>(out_g);
      row[x + 2] = static_cast<unsigned char>(out_b);
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
