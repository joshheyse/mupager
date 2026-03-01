#include "document.h"

#include <stdexcept>

void ContextDeleter::operator()(fz_context* ctx) const {
  fz_drop_context(ctx);
}

void DocumentDeleter::operator()(fz_document* doc) const {
  fz_drop_document(ctx, doc);
}

Document::Document(const std::string& path)
    : ctx_(nullptr)
    , doc_(nullptr, DocumentDeleter{nullptr}) {
  fz_context* raw_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!raw_ctx) {
    throw std::runtime_error("failed to create mupdf context");
  }
  ctx_.reset(raw_ctx);

  fz_register_document_handlers(raw_ctx);

  fz_try(raw_ctx) {
    doc_ = {fz_open_document(raw_ctx, path.c_str()), DocumentDeleter{raw_ctx}};
  }
  fz_catch(raw_ctx) {
    std::string msg = fz_caught_message(raw_ctx);
    throw std::runtime_error("failed to open document: " + msg);
  }
}

int Document::page_count() const {
  return fz_count_pages(ctx_.get(), doc_.get());
}

std::pair<float, float> Document::page_size(int page_num) const {
  fz_context* raw_ctx = ctx_.get();
  fz_page* page = nullptr;
  fz_rect bounds;
  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    bounds = fz_bound_page(raw_ctx, page);
  }
  fz_always(raw_ctx) {
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    throw std::runtime_error("failed to get page size: " + std::string(fz_caught_message(raw_ctx)));
  }
  return {bounds.x1 - bounds.x0, bounds.y1 - bounds.y0};
}

Pixmap Document::render_page(int page_num, float zoom) const {
  fz_context* raw_ctx = ctx_.get();
  fz_page* page = nullptr;
  fz_pixmap* pix = nullptr;

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    fz_matrix ctm = fz_scale(zoom, zoom);
    pix = fz_new_pixmap_from_page(raw_ctx, page, ctm, fz_device_rgb(raw_ctx), 0);
  }
  fz_always(raw_ctx) {
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    std::string msg = fz_caught_message(raw_ctx);
    throw std::runtime_error("failed to render page: " + msg);
  }

  return Pixmap(raw_ctx, pix);
}

fz_context* Document::ctx() const {
  return ctx_.get();
}

fz_document* Document::raw() const {
  return doc_.get();
}
