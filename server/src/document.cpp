#include "document.h"

#include <algorithm>
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

Pixmap Document::render_page(int page_num, float zoom, int x_offset, int y_offset, int viewport_w, int viewport_h) const {
  fz_context* raw_ctx = ctx_.get();
  fz_page* page = nullptr;
  fz_pixmap* pix = nullptr;

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    fz_matrix ctm = fz_scale(zoom, zoom);
    fz_irect clip = {x_offset, y_offset, x_offset + viewport_w, y_offset + viewport_h};
    pix = fz_new_pixmap_with_bbox(raw_ctx, fz_device_rgb(raw_ctx), clip, nullptr, 0);
    fz_clear_pixmap_with_value(raw_ctx, pix, 255);
    fz_device* dev = fz_new_draw_device_with_bbox(raw_ctx, fz_identity, pix, &clip);
    fz_run_page(raw_ctx, page, dev, ctm, nullptr);
    fz_close_device(raw_ctx, dev);
    fz_drop_device(raw_ctx, dev);
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

static void flatten_outline(fz_context* ctx, fz_document* doc, fz_outline* node, int level, Outline& out) {
  while (node) {
    int page = node->page.page;
    if (page < 0 && node->uri) {
      page = fz_resolve_link(ctx, doc, node->uri, nullptr, nullptr).page;
    }
    if (page >= 0 && node->title) {
      out.push_back({node->title, page, level});
    }
    if (node->down) {
      flatten_outline(ctx, doc, node->down, level + 1, out);
    }
    node = node->next;
  }
}

Outline Document::load_outline() const {
  fz_context* raw_ctx = ctx_.get();
  fz_outline* root = nullptr;
  Outline result;

  fz_try(raw_ctx) {
    root = fz_load_outline(raw_ctx, doc_.get());
    flatten_outline(raw_ctx, doc_.get(), root, 0, result);
  }
  fz_always(raw_ctx) {
    fz_drop_outline(raw_ctx, root);
  }
  fz_catch(raw_ctx) {
    // Documents without outlines — return empty.
  }

  return result;
}

SearchResults Document::search(const std::string& needle) const {
  static constexpr int MAX_HITS_PER_PAGE = 512;
  fz_context* raw_ctx = ctx_.get();
  SearchResults results;
  int pages = page_count();

  for (int p = 0; p < pages; ++p) {
    fz_page* page = nullptr;
    fz_quad hits[MAX_HITS_PER_PAGE];
    int n = 0;

    fz_try(raw_ctx) {
      page = fz_load_page(raw_ctx, doc_.get(), p);
      n = fz_search_page(raw_ctx, page, needle.c_str(), nullptr, hits, MAX_HITS_PER_PAGE);
    }
    fz_always(raw_ctx) {
      fz_drop_page(raw_ctx, page);
    }
    fz_catch(raw_ctx) {
      continue;
    }

    for (int i = 0; i < n; ++i) {
      float x0 = std::min({hits[i].ul.x, hits[i].ur.x, hits[i].ll.x, hits[i].lr.x});
      float y0 = std::min({hits[i].ul.y, hits[i].ur.y, hits[i].ll.y, hits[i].lr.y});
      float x1 = std::max({hits[i].ul.x, hits[i].ur.x, hits[i].ll.x, hits[i].lr.x});
      float y1 = std::max({hits[i].ul.y, hits[i].ur.y, hits[i].ll.y, hits[i].lr.y});
      results.push_back({p, x0, y0, x1 - x0, y1 - y0});
    }
  }
  return results;
}

fz_context* Document::ctx() const {
  return ctx_.get();
}

fz_document* Document::raw() const {
  return doc_.get();
}
