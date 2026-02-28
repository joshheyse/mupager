#pragma once

#include <mupdf/fitz.h>

#include <string>

class Document {
public:
  explicit Document(const std::string& path);
  ~Document();

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  int page_count() const;

  fz_context* ctx() const;
  fz_document* raw() const;

private:
  fz_context* ctx_;
  fz_document* doc_;
};
