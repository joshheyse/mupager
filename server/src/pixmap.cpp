#include "pixmap.h"

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
