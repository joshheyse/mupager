#include "document.h"

#include <stdexcept>

Document::Document(const std::string& path)
    : ctx_(nullptr)
    , doc_(nullptr) {
  ctx_ = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx_) {
    throw std::runtime_error("failed to create mupdf context");
  }

  fz_register_document_handlers(ctx_);

  fz_try(ctx_) {
    doc_ = fz_open_document(ctx_, path.c_str());
  }
  fz_catch(ctx_) {
    std::string msg = fz_caught_message(ctx_);
    fz_drop_context(ctx_);
    throw std::runtime_error("failed to open document: " + msg);
  }
}

Document::~Document() {
  fz_drop_document(ctx_, doc_);
  fz_drop_context(ctx_);
}

int Document::page_count() const {
  return fz_count_pages(ctx_, doc_);
}

fz_context* Document::ctx() const {
  return ctx_;
}

fz_document* Document::raw() const {
  return doc_;
}
