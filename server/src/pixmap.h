#pragma once

#include <mupdf/fitz.h>

#include <memory>

struct PixmapDeleter {
  fz_context* ctx;
  void operator()(fz_pixmap* pix) const;
};

class Pixmap {
public:
  Pixmap(fz_context* ctx, fz_pixmap* pix);

  int width() const;
  int height() const;
  int stride() const;
  int components() const;
  unsigned char* samples();
  const unsigned char* samples() const;

private:
  std::unique_ptr<fz_pixmap, PixmapDeleter> pix_;
};
