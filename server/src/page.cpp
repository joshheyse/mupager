#include "page.hpp"

#include "app.hpp"
#include "util/stopwatch.hpp"

#include <spdlog/spdlog.h>

#include <format>

Page::Page(std::vector<unsigned char> pixels, int w, int h, int comp, float zoom, float scale, uint32_t id, CellSize grid)
    : base_pixels_(std::move(pixels))
    , width_(w)
    , height_(h)
    , comp_(comp)
    , render_zoom_(zoom)
    , render_scale_(scale)
    , image_id_(id)
    , cell_grid_(grid) {}

Page Page::render(
    const Document& doc,
    int page_num,
    const RenderParams& params,
    float base_zoom,
    const HighlightParams& highlights,
    Frontend& frontend
) {
  float render_zoom = (params.render_scale == 0.0f) ? base_zoom * params.user_zoom : base_zoom * params.render_scale;

  Pixmap pixmap = [&] {
    Stopwatch sw(std::format("mupdf render page {} zoom={} scale={}", page_num, render_zoom, params.render_scale));
    return doc.render_page(page_num, render_zoom);
  }();

  if (params.effective_theme == Theme::Dark) {
    pixmap.invert();
  }
  else if (params.effective_theme == Theme::Terminal) {
    pixmap.recolor(params.recolor_fg, params.recolor_bg, params.recolor_accent);
  }

  if (params.draw_separator && page_num > 0) {
    PixelRect sep = {0, 0, pixmap.width(), 2};
    pixmap.highlight_rect(sep, params.separator_color, 255);
  }

  auto base = pixmap.pack_pixels();
  int w = pixmap.width();
  int h = pixmap.height();
  int comp = pixmap.components();

  // Build a temporary Page to call highlight()
  Page page(std::move(base), w, h, comp, render_zoom, params.render_scale, 0, {});
  Pixmap highlighted = page.highlight(doc, page_num, highlights);

  // Grid dimensions based on display size (not rendered pixel size)
  auto client = frontend.client_info();
  float display_zoom = base_zoom * params.user_zoom;
  auto ps = doc.page_size(page_num);
  int display_w = static_cast<int>(ps.width * display_zoom);
  int display_h = static_cast<int>(ps.height * display_zoom);
  int cols = (display_w + client.cell.width - 1) / client.cell.width;
  int rows = (display_h + client.cell.height - 1) / client.cell.height;

  uint32_t image_id = frontend.upload_image(highlighted, cols, rows);
  page.image_id_ = image_id;
  page.cell_grid_ = {cols, rows};

  return page;
}

void Page::refresh_highlights(const Document& doc, int page_num, const HighlightParams& params, Frontend& frontend) {
  Pixmap pixmap = highlight(doc, page_num, params);
  frontend.free_image(image_id_);
  image_id_ = frontend.upload_image(pixmap, cell_grid_.width, cell_grid_.height);
}

void Page::free_image(Frontend& frontend) {
  frontend.free_image(image_id_);
}

Pixmap Page::highlight(const Document& doc, int page_num, const HighlightParams& params) const {
  Pixmap pixmap = to_pixmap(doc.ctx());
  highlight_search(pixmap, page_num, render_zoom_, params);
  highlight_selection(pixmap, doc, page_num, render_zoom_, params);
  return pixmap;
}

Pixmap Page::to_pixmap(fz_context* ctx) const {
  return Pixmap::from_pixels(ctx, width_, height_, comp_, base_pixels_.data());
}

void Page::highlight_search(Pixmap& pixmap, int page_num, float render_zoom, const HighlightParams& params) {
  if (params.search_results == nullptr || params.search_results->empty()) {
    return;
  }

  const auto& results = *params.search_results;
  const auto& colors = *params.colors;

  for (int i = 0; i < static_cast<int>(results.size()); ++i) {
    const auto& hit = results[i];
    if (hit.page != page_num) {
      continue;
    }

    PixelRect rect = {
        static_cast<int>(hit.rect.x * render_zoom),
        static_cast<int>(hit.rect.y * render_zoom),
        static_cast<int>(hit.rect.width * render_zoom),
        static_cast<int>(hit.rect.height * render_zoom),
    };

    if (i == params.search_current) {
      pixmap.highlight_rect(rect, colors.search_active, colors.search_active_alpha);
    }
    else {
      pixmap.highlight_rect(rect, colors.search_highlight, colors.search_highlight_alpha);
    }
  }
}

void Page::highlight_selection(Pixmap& pixmap, const Document& doc, int page_num, float render_zoom, const HighlightParams& params) {
  if (params.app_mode != AppMode::Visual && params.app_mode != AppMode::VisualBlock) {
    return;
  }

  const auto& colors = *params.colors;

  std::vector<SearchHit> quads;
  if (params.app_mode == AppMode::Visual) {
    quads = doc.selection_quads(page_num, params.selection_anchor, params.selection_extent);
  }
  else {
    int lo = std::min(params.selection_anchor.page, params.selection_extent.page);
    int hi = std::max(params.selection_anchor.page, params.selection_extent.page);
    if (page_num < lo || page_num > hi) {
      return;
    }

    DocRect sel = DocRect::from_corners(
        std::min(params.selection_anchor.pos.x, params.selection_extent.pos.x),
        std::min(params.selection_anchor.pos.y, params.selection_extent.pos.y),
        std::max(params.selection_anchor.pos.x, params.selection_extent.pos.x),
        std::max(params.selection_anchor.pos.y, params.selection_extent.pos.y)
    );
    quads = doc.rect_selection_quads(page_num, sel);
  }

  for (const auto& hit : quads) {
    PixelRect rect = {
        static_cast<int>(hit.rect.x * render_zoom),
        static_cast<int>(hit.rect.y * render_zoom),
        static_cast<int>(hit.rect.width * render_zoom),
        static_cast<int>(hit.rect.height * render_zoom),
    };
    pixmap.highlight_rect(rect, colors.selection_highlight, colors.selection_highlight_alpha);
  }
}
