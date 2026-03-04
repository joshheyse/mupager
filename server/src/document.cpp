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

DocSize Document::page_size(int page_num) const {
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
  static constexpr int MaxHitsPerPage = 512;
  fz_context* raw_ctx = ctx_.get();
  SearchResults results;
  int pages = page_count();

  for (int p = 0; p < pages; ++p) {
    fz_page* page = nullptr;
    fz_quad hits[MaxHitsPerPage];
    int n = 0;

    fz_try(raw_ctx) {
      page = fz_load_page(raw_ctx, doc_.get(), p);
      n = fz_search_page(raw_ctx, page, needle.c_str(), nullptr, hits, MaxHitsPerPage);
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
      results.push_back({p, {x0, y0, x1 - x0, y1 - y0}});
    }
  }
  return results;
}

void Document::reload(const std::string& path) {
  fz_context* raw_ctx = ctx_.get();
  fz_document* new_doc = nullptr;

  fz_try(raw_ctx) {
    new_doc = fz_open_document(raw_ctx, path.c_str());
  }
  fz_catch(raw_ctx) {
    std::string msg = fz_caught_message(raw_ctx);
    throw std::runtime_error("failed to reload document: " + msg);
  }

  doc_ = {new_doc, DocumentDeleter{raw_ctx}};
}

std::vector<PageLink> Document::load_links(int page_num) const {
  fz_context* raw_ctx = ctx_.get();
  fz_page* page = nullptr;
  std::vector<PageLink> results;

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    fz_link* links = fz_load_links(raw_ctx, page);

    for (fz_link* link = links; link != nullptr; link = link->next) {
      PageLink pl;
      pl.page = page_num;
      pl.rect = {link->rect.x0, link->rect.y0, link->rect.x1 - link->rect.x0, link->rect.y1 - link->rect.y0};
      pl.uri = link->uri ? link->uri : "";
      pl.dest_page = -1;

      if (!pl.uri.empty() && pl.uri[0] == '#') {
        float dx = 0;
        float dy = 0;
        fz_location loc = fz_resolve_link(raw_ctx, doc_.get(), pl.uri.c_str(), &dx, &dy);
        if (loc.page >= 0) {
          pl.dest_page = loc.page;
          pl.dest = {dx, dy};
        }
      }

      results.push_back(std::move(pl));
    }

    fz_drop_link(raw_ctx, links);
  }
  fz_always(raw_ctx) {
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    // Pages without links — return empty.
  }

  return results;
}

std::string Document::copy_text(const PagePoint& a, const PagePoint& b) const {
  fz_context* raw_ctx = ctx_.get();

  // Normalize so start <= end
  PagePoint start = a;
  PagePoint end = b;
  if (start.page > end.page || (start.page == end.page && (start.pos.y > end.pos.y || (start.pos.y == end.pos.y && start.pos.x > end.pos.x)))) {
    std::swap(start, end);
  }

  std::string result;
  for (int p = start.page; p <= end.page && p < page_count(); ++p) {
    if (p < 0) {
      continue;
    }
    fz_stext_page* stext = nullptr;
    fz_page* page = nullptr;
    char* extracted = nullptr;

    fz_try(raw_ctx) {
      page = fz_load_page(raw_ctx, doc_.get(), p);
      stext = fz_new_stext_page_from_page(raw_ctx, page, nullptr);

      fz_point pa, pb;
      if (p == start.page) {
        pa = {start.pos.x, start.pos.y};
      }
      else {
        pa = {0, 0};
      }
      if (p == end.page) {
        pb = {end.pos.x, end.pos.y};
      }
      else {
        fz_rect bounds = fz_bound_page(raw_ctx, page);
        pb = {bounds.x1, bounds.y1};
      }

      extracted = fz_copy_selection(raw_ctx, stext, pa, pb, 0);
    }
    fz_always(raw_ctx) {
      fz_drop_stext_page(raw_ctx, stext);
      fz_drop_page(raw_ctx, page);
    }
    fz_catch(raw_ctx) {
      continue;
    }

    if (extracted) {
      if (!result.empty()) {
        result += '\n';
      }
      result += extracted;
      fz_free(raw_ctx, extracted);
    }
  }
  return result;
}

std::vector<SearchHit> Document::selection_quads(int page_num, const PagePoint& a, const PagePoint& b) const {
  static constexpr int MaxQuads = 1024;
  fz_context* raw_ctx = ctx_.get();
  std::vector<SearchHit> results;

  if (page_num < 0 || page_num >= page_count()) {
    return results;
  }

  fz_stext_page* stext = nullptr;
  fz_page* page = nullptr;
  fz_quad quads[MaxQuads];
  int n = 0;

  // Determine selection points for this page
  PagePoint start = a;
  PagePoint end = b;
  if (start.page > end.page || (start.page == end.page && (start.pos.y > end.pos.y || (start.pos.y == end.pos.y && start.pos.x > end.pos.x)))) {
    std::swap(start, end);
  }

  if (page_num < start.page || page_num > end.page) {
    return results;
  }

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    stext = fz_new_stext_page_from_page(raw_ctx, page, nullptr);

    fz_point pa, pb;
    if (page_num == start.page) {
      pa = {start.pos.x, start.pos.y};
    }
    else {
      pa = {0, 0};
    }
    if (page_num == end.page) {
      pb = {end.pos.x, end.pos.y};
    }
    else {
      fz_rect bounds = fz_bound_page(raw_ctx, page);
      pb = {bounds.x1, bounds.y1};
    }

    n = fz_highlight_selection(raw_ctx, stext, pa, pb, quads, MaxQuads);
  }
  fz_always(raw_ctx) {
    fz_drop_stext_page(raw_ctx, stext);
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    return results;
  }

  for (int i = 0; i < n; ++i) {
    float x0 = std::min({quads[i].ul.x, quads[i].ur.x, quads[i].ll.x, quads[i].lr.x});
    float y0 = std::min({quads[i].ul.y, quads[i].ur.y, quads[i].ll.y, quads[i].lr.y});
    float x1 = std::max({quads[i].ul.x, quads[i].ur.x, quads[i].ll.x, quads[i].lr.x});
    float y1 = std::max({quads[i].ul.y, quads[i].ur.y, quads[i].ll.y, quads[i].lr.y});
    results.push_back({page_num, {x0, y0, x1 - x0, y1 - y0}});
  }
  return results;
}

std::string Document::copy_rect_text(int page_num, float x0, float y0, float x1, float y1) const {
  fz_context* raw_ctx = ctx_.get();

  if (page_num < 0 || page_num >= page_count()) {
    return {};
  }

  // Normalize rect
  if (x0 > x1) {
    std::swap(x0, x1);
  }
  if (y0 > y1) {
    std::swap(y0, y1);
  }

  fz_stext_page* stext = nullptr;
  fz_page* page = nullptr;
  std::string result;

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    stext = fz_new_stext_page_from_page(raw_ctx, page, nullptr);

    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT) {
        continue;
      }
      for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
        std::string line_text;
        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
          float cx = (ch->quad.ul.x + ch->quad.ur.x) / 2.0f;
          float cy = (ch->quad.ul.y + ch->quad.ll.y) / 2.0f;
          if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
            char buf[8];
            int len = fz_runetochar(buf, ch->c);
            line_text.append(buf, len);
          }
        }
        if (!line_text.empty()) {
          if (!result.empty()) {
            result += '\n';
          }
          result += line_text;
        }
      }
    }
  }
  fz_always(raw_ctx) {
    fz_drop_stext_page(raw_ctx, stext);
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    return {};
  }

  return result;
}

std::vector<SearchHit> Document::rect_selection_quads(int page_num, float x0, float y0, float x1, float y1) const {
  fz_context* raw_ctx = ctx_.get();
  std::vector<SearchHit> results;

  if (page_num < 0 || page_num >= page_count()) {
    return results;
  }

  // Normalize rect
  if (x0 > x1) {
    std::swap(x0, x1);
  }
  if (y0 > y1) {
    std::swap(y0, y1);
  }

  fz_stext_page* stext = nullptr;
  fz_page* page = nullptr;

  fz_try(raw_ctx) {
    page = fz_load_page(raw_ctx, doc_.get(), page_num);
    stext = fz_new_stext_page_from_page(raw_ctx, page, nullptr);

    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT) {
        continue;
      }
      for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
          float cx = (ch->quad.ul.x + ch->quad.ur.x) / 2.0f;
          float cy = (ch->quad.ul.y + ch->quad.ll.y) / 2.0f;
          if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
            float qx0 = std::min({ch->quad.ul.x, ch->quad.ur.x, ch->quad.ll.x, ch->quad.lr.x});
            float qy0 = std::min({ch->quad.ul.y, ch->quad.ur.y, ch->quad.ll.y, ch->quad.lr.y});
            float qx1 = std::max({ch->quad.ul.x, ch->quad.ur.x, ch->quad.ll.x, ch->quad.lr.x});
            float qy1 = std::max({ch->quad.ul.y, ch->quad.ur.y, ch->quad.ll.y, ch->quad.lr.y});
            results.push_back({page_num, {qx0, qy0, qx1 - qx0, qy1 - qy0}});
          }
        }
      }
    }
  }
  fz_always(raw_ctx) {
    fz_drop_stext_page(raw_ctx, stext);
    fz_drop_page(raw_ctx, page);
  }
  fz_catch(raw_ctx) {
    return results;
  }

  return results;
}

/// @brief Classify a unicode codepoint as space, punctuation, or word character.
static int char_class(int c) {
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
    return 0; // space
  }
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c > 127) {
    return 2; // word
  }
  return 1; // punctuation
}

/// @brief Collect all stext chars from a page into a flat vector.
struct StextChar {
  float x, y; ///< Character position.
  int cls;    ///< Character class (0=space, 1=punct, 2=word).
};

static std::vector<StextChar> collect_stext_chars(fz_context* ctx, fz_document* doc, int page_num) {
  std::vector<StextChar> chars;
  fz_page* page = nullptr;
  fz_stext_page* stext = nullptr;

  fz_try(ctx) {
    page = fz_load_page(ctx, doc, page_num);
    stext = fz_new_stext_page_from_page(ctx, page, nullptr);

    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT) {
        continue;
      }
      for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
          float cx = (ch->quad.ul.x + ch->quad.ur.x) / 2.0f;
          float cy = (ch->quad.ul.y + ch->quad.ll.y) / 2.0f;
          chars.push_back({cx, cy, char_class(ch->c)});
        }
      }
    }
  }
  fz_always(ctx) {
    fz_drop_stext_page(ctx, stext);
    fz_drop_page(ctx, page);
  }
  fz_catch(ctx) {
    // Return whatever we have
  }

  return chars;
}

PagePoint Document::next_word_boundary(const PagePoint& from) const {
  fz_context* raw_ctx = ctx_.get();
  int pages = page_count();

  for (int p = from.page; p < pages; ++p) {
    auto chars = collect_stext_chars(raw_ctx, doc_.get(), p);
    if (chars.empty()) {
      continue;
    }

    // Find the first char at or after 'from' on this page
    size_t start = 0;
    if (p == from.page) {
      // Find nearest char to from point
      float best_dist = 1e18f;
      for (size_t i = 0; i < chars.size(); ++i) {
        float dx = chars[i].x - from.pos.x;
        float dy = chars[i].y - from.pos.y;
        float dist = dy * 10000.0f + dx; // prioritize y, then x
        if (dist >= -1.0f && dist < best_dist) {
          best_dist = dist;
          start = i;
        }
      }
      // If we're already past all chars on this page, try next page
      if (best_dist > 1e17f) {
        continue;
      }
    }

    // From start, find the next class transition (skip current class, then skip next class start)
    int cur_cls = chars[start].cls;
    size_t i = start + 1;

    // Skip rest of current class
    while (i < chars.size() && chars[i].cls == cur_cls) {
      ++i;
    }

    // Skip spaces
    while (i < chars.size() && chars[i].cls == 0) {
      ++i;
    }

    if (i < chars.size()) {
      return {p, {chars[i].x, chars[i].y}};
    }

    // End of page — try next page
    if (p + 1 < pages) {
      auto next_chars = collect_stext_chars(raw_ctx, doc_.get(), p + 1);
      // Skip leading spaces
      size_t j = 0;
      while (j < next_chars.size() && next_chars[j].cls == 0) {
        ++j;
      }
      if (j < next_chars.size()) {
        return {p + 1, {next_chars[j].x, next_chars[j].y}};
      }
    }
  }

  return from;
}

PagePoint Document::end_of_word_boundary(const PagePoint& from) const {
  fz_context* raw_ctx = ctx_.get();
  int pages = page_count();

  for (int p = from.page; p < pages; ++p) {
    auto chars = collect_stext_chars(raw_ctx, doc_.get(), p);
    if (chars.empty()) {
      continue;
    }

    size_t start = 0;
    if (p == from.page) {
      float best_dist = 1e18f;
      for (size_t i = 0; i < chars.size(); ++i) {
        float dx = chars[i].x - from.pos.x;
        float dy = chars[i].y - from.pos.y;
        float dist = dy * 10000.0f + dx;
        if (dist >= -1.0f && dist < best_dist) {
          best_dist = dist;
          start = i;
        }
      }
      if (best_dist > 1e17f) {
        continue;
      }
    }

    // Move forward one char to handle being at end of word already
    size_t i = start + 1;

    // Skip spaces
    while (i < chars.size() && chars[i].cls == 0) {
      ++i;
    }

    if (i >= chars.size()) {
      // Try next page
      if (p + 1 < pages) {
        auto next_chars = collect_stext_chars(raw_ctx, doc_.get(), p + 1);
        size_t j = 0;
        while (j < next_chars.size() && next_chars[j].cls == 0) {
          ++j;
        }
        if (j < next_chars.size()) {
          int cls = next_chars[j].cls;
          while (j + 1 < next_chars.size() && next_chars[j + 1].cls == cls) {
            ++j;
          }
          return {p + 1, {next_chars[j].x, next_chars[j].y}};
        }
      }
      continue;
    }

    // Advance to end of this word (last char before class changes)
    int cls = chars[i].cls;
    while (i + 1 < chars.size() && chars[i + 1].cls == cls) {
      ++i;
    }

    return {p, {chars[i].x, chars[i].y}};
  }

  return from;
}

/// @brief Find the stext line containing the given point on a page, returning the first and last char positions.
struct LineRange {
  float first_x, first_y;                     ///< Position of the first character on the line.
  float last_x, last_y;                       ///< Position of the last character on the line.
  float first_non_space_x, first_non_space_y; ///< Position of the first non-whitespace character.
  bool found = false;
};

static LineRange find_line_at(fz_context* ctx, fz_document* doc, int page_num, float target_y) {
  LineRange result;
  fz_page* page = nullptr;
  fz_stext_page* stext = nullptr;

  fz_try(ctx) {
    page = fz_load_page(ctx, doc, page_num);
    stext = fz_new_stext_page_from_page(ctx, page, nullptr);

    fz_stext_line* best_line = nullptr;
    float best_dist = 1e18f;

    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT) {
        continue;
      }
      for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
        float line_cy = (line->bbox.y0 + line->bbox.y1) / 2.0f;
        float dist = std::abs(line_cy - target_y);
        if (target_y >= line->bbox.y0 && target_y <= line->bbox.y1) {
          best_line = line;
          best_dist = 0;
          break;
        }
        if (dist < best_dist) {
          best_dist = dist;
          best_line = line;
        }
      }
      if (best_dist == 0) {
        break;
      }
    }

    if (best_line && best_line->first_char) {
      result.found = true;

      // Find first char
      fz_stext_char* first = best_line->first_char;
      result.first_x = (first->quad.ul.x + first->quad.ur.x) / 2.0f;
      result.first_y = (first->quad.ul.y + first->quad.ll.y) / 2.0f;

      // Find last char and first non-space
      result.first_non_space_x = result.first_x;
      result.first_non_space_y = result.first_y;
      bool found_non_space = false;

      fz_stext_char* last = first;
      for (fz_stext_char* ch = first; ch; ch = ch->next) {
        last = ch;
        if (!found_non_space && char_class(ch->c) != 0) {
          result.first_non_space_x = (ch->quad.ul.x + ch->quad.ur.x) / 2.0f;
          result.first_non_space_y = (ch->quad.ul.y + ch->quad.ll.y) / 2.0f;
          found_non_space = true;
        }
      }
      result.last_x = (last->quad.ul.x + last->quad.ur.x) / 2.0f;
      result.last_y = (last->quad.ul.y + last->quad.ll.y) / 2.0f;
    }
  }
  fz_always(ctx) {
    fz_drop_stext_page(ctx, stext);
    fz_drop_page(ctx, page);
  }
  fz_catch(ctx) {
    // Return unfound
  }

  return result;
}

PagePoint Document::line_start(const PagePoint& from) const {
  if (from.page < 0 || from.page >= page_count()) {
    return from;
  }
  auto range = find_line_at(ctx_.get(), doc_.get(), from.page, from.pos.y);
  if (range.found) {
    return {from.page, {range.first_x, range.first_y}};
  }
  return from;
}

PagePoint Document::line_end(const PagePoint& from) const {
  if (from.page < 0 || from.page >= page_count()) {
    return from;
  }
  auto range = find_line_at(ctx_.get(), doc_.get(), from.page, from.pos.y);
  if (range.found) {
    return {from.page, {range.last_x, range.last_y}};
  }
  return from;
}

PagePoint Document::first_non_space(const PagePoint& from) const {
  if (from.page < 0 || from.page >= page_count()) {
    return from;
  }
  auto range = find_line_at(ctx_.get(), doc_.get(), from.page, from.pos.y);
  if (range.found) {
    return {from.page, {range.first_non_space_x, range.first_non_space_y}};
  }
  return from;
}

PagePoint Document::prev_word_boundary(const PagePoint& from) const {
  fz_context* raw_ctx = ctx_.get();

  for (int p = from.page; p >= 0; --p) {
    auto chars = collect_stext_chars(raw_ctx, doc_.get(), p);
    if (chars.empty()) {
      continue;
    }

    // Find the char at or just before 'from' on this page
    size_t start = chars.size() - 1;
    if (p == from.page) {
      float best_dist = 1e18f;
      for (size_t i = 0; i < chars.size(); ++i) {
        float dx = from.pos.x - chars[i].x;
        float dy = from.pos.y - chars[i].y;
        float dist = dy * 10000.0f + dx;
        if (dist >= -1.0f && dist < best_dist) {
          best_dist = dist;
          start = i;
        }
      }
      if (best_dist > 1e17f) {
        continue;
      }
    }

    if (start == 0) {
      // Already at first char, try prev page
      continue;
    }

    int i = static_cast<int>(start) - 1;

    // Skip spaces backward
    while (i >= 0 && chars[i].cls == 0) {
      --i;
    }

    if (i < 0) {
      continue;
    }

    // Skip current class backward
    int cur_cls = chars[i].cls;
    while (i > 0 && chars[i - 1].cls == cur_cls) {
      --i;
    }

    return {p, {chars[i].x, chars[i].y}};
  }

  return from;
}

fz_context* Document::ctx() const {
  return ctx_.get();
}

fz_document* Document::raw() const {
  return doc_.get();
}
