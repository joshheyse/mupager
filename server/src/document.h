#pragma once

#include "pixmap.h"

#include <mupdf/fitz.h>

#include <memory>
#include <string>

struct ContextDeleter {
  void operator()(fz_context* ctx) const;
};

struct DocumentDeleter {
  fz_context* ctx;
  void operator()(fz_document* doc) const;
};

class Document {
public:
  explicit Document(const std::string& path);

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  int page_count() const;
  Pixmap render_page(int page_num, float zoom) const;

  fz_context* ctx() const;
  fz_document* raw() const;

private:
  std::unique_ptr<fz_context, ContextDeleter> ctx_;
  std::unique_ptr<fz_document, DocumentDeleter> doc_;
};
