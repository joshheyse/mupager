#include "terminal/controller.hpp"

#include "app.hpp"
#include "action.hpp"
#include "geometry.hpp"
#include "graphics/sgr.hpp"
#include "terminal/frontend.hpp"
#include "terminal/key_bindings.hpp"
#include "input_event.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

/// @brief Box-drawing separator for the status line (│).
static constexpr const char* Sep = " \xe2\x94\x82 ";

/// @brief Horizontal rule character (─).
static constexpr const char* Horiz = "\xe2\x94\x80";

/// @brief Extract pixel scroll delta from any scroll-type command.
static std::optional<std::pair<int, int>> scroll_delta(const Action& act, const CellSize& cell) {
  if (auto* ms = std::get_if<action::MouseScroll>(&act)) {
    return std::pair{ms->dx, ms->dy};
  }
  if (auto* sd = std::get_if<action::ScrollDown>(&act)) {
    return std::pair{0, cell.height * std::max(1, sd->count)};
  }
  if (auto* su = std::get_if<action::ScrollUp>(&act)) {
    return std::pair{0, -cell.height * std::max(1, su->count)};
  }
  if (auto* sl = std::get_if<action::ScrollLeft>(&act)) {
    return std::pair{-cell.width * std::max(1, sl->count), 0};
  }
  if (auto* sr = std::get_if<action::ScrollRight>(&act)) {
    return std::pair{cell.width * std::max(1, sr->count), 0};
  }
  return std::nullopt;
}

static std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(' ');
  if (start == std::string::npos) {
    return {};
  }
  return s.substr(start, s.find_last_not_of(' ') - start + 1);
}

TerminalController::TerminalController(App& app, TerminalFrontend& frontend, const KeyBindings& bindings, int scroll_lines)
    : app_(app)
    , frontend_(frontend)
    , input_handler_(bindings, scroll_lines) {}

InputMode TerminalController::effective_input_mode() const {
  switch (terminal_mode_) {
    case TerminalMode::Command:
      return InputMode::Command;
    case TerminalMode::Search:
      return InputMode::Search;
    case TerminalMode::Help:
      return InputMode::Help;
    case TerminalMode::Outline:
      return InputMode::Outline;
    case TerminalMode::Sidebar:
      return InputMode::Sidebar;
    case TerminalMode::Normal:
      break;
  }
  return app_.input_mode();
}

void TerminalController::forward_action(const Action& act) {
  app_.handle_action(act);
  update_terminal_ui();
}

void TerminalController::update_terminal_ui() {
  update_sidebar_display();
  update_statusline();
}

void TerminalController::initialize() {
  update_terminal_ui();
}

void TerminalController::idle_tick() {
  // Flash expiry is handled by App's idle_tick via last_action_.
  // We just need to re-render the statusline if flash state changed.
  update_statusline();
}

// --- Statusline ---

void TerminalController::update_statusline() {
  auto vs = app_.view_state();

  std::string left;
  if (terminal_mode_ == TerminalMode::Command) {
    left = std::format(":{}", command_input_);
  }
  else if (terminal_mode_ == TerminalMode::Search) {
    left = std::format("/{}", search_input_);
  }
  else if (vs.search_total > 0 && terminal_mode_ == TerminalMode::Normal && app_.app_mode() == AppMode::Normal) {
    if (vs.search_page_matches > 0 && vs.search_page_matches < vs.search_total) {
      left = std::format("/{} [{}/{} ({})]", vs.search_term, vs.search_current, vs.search_total, vs.search_page_matches);
    }
    else {
      left = std::format("/{} [{}/{}]", vs.search_term, vs.search_current, vs.search_total);
    }
  }
  else if (!vs.visual_mode.empty()) {
    if (vs.visual_mode == "visual") {
      left = "-- VISUAL --";
    }
    else {
      left = "-- VISUAL BLOCK --";
    }
  }
  else if (!vs.flash_message.empty()) {
    left = vs.flash_message;
  }

  std::string right = vs.view_mode;
  if (vs.zoom_percent != 100) {
    right += std::format("{}{}%", Sep, vs.zoom_percent);
  }
  right += std::format("{}{}{}{}/{}", Sep, vs.theme, Sep, vs.current_page, vs.total_pages);

  if (!vs.cache_pages.empty()) {
    auto rs_str = vs.cache_pages; // Reuse field for render-scale info
    if (vs.cache_bytes >= 1024 * 1024) {
      right += std::format(" [{}] {:.1f}M", vs.cache_pages, static_cast<double>(vs.cache_bytes) / (1024.0 * 1024.0));
    }
    else if (vs.cache_bytes > 0) {
      right += std::format(" [{}] {:.0f}K", vs.cache_pages, static_cast<double>(vs.cache_bytes) / 1024.0);
    }
  }

  frontend_.statusline(left, right);
}

// --- Command bar ---

void TerminalController::enter_command_mode() {
  terminal_mode_ = TerminalMode::Command;
  command_input_.clear();
  update_statusline();
}

void TerminalController::command_char(char ch) {
  command_input_ += ch;
  update_statusline();
}

void TerminalController::command_backspace() {
  if (!command_input_.empty()) {
    command_input_.pop_back();
  }
  update_statusline();
}

void TerminalController::command_execute() {
  auto [parsed, error] = parse_command_string(command_input_);
  terminal_mode_ = TerminalMode::Normal;
  command_input_.clear();
  if (parsed) {
    // Handle SetSidebarWidth locally
    if (auto* sw = std::get_if<action::SetSidebarWidth>(&*parsed)) {
      sidebar_width_cols_ = sw->cols;
      if (sidebar_visible_) {
        int cols = sidebar_effective_width();
        frontend_.set_canvas_inset(cols);
        frontend_.clear();
        forward_action(action::Resize{});
      }
      else {
        update_statusline();
      }
    }
    else {
      forward_action(*parsed);
    }
  }
  else {
    if (!error.empty()) {
      // Show error as a local flash on statusline
      // We can't use app's flash since it's not an app error.
      // Just show it directly for one statusline render.
      frontend_.statusline(error, "");
    }
    else {
      update_statusline();
    }
  }
}

void TerminalController::command_cancel() {
  terminal_mode_ = TerminalMode::Normal;
  command_input_.clear();
  update_statusline();
}

// --- Search bar ---

void TerminalController::enter_search_mode() {
  terminal_mode_ = TerminalMode::Search;
  search_input_.clear();
  update_statusline();
}

void TerminalController::search_char(char ch) {
  search_input_ += ch;
  update_statusline();
}

void TerminalController::search_backspace() {
  if (!search_input_.empty()) {
    search_input_.pop_back();
  }
  update_statusline();
}

void TerminalController::search_execute() {
  if (!search_input_.empty()) {
    forward_action(action::Search{search_input_});
  }
  terminal_mode_ = TerminalMode::Normal;
  update_statusline();
}

void TerminalController::search_cancel() {
  terminal_mode_ = TerminalMode::Normal;
  search_input_.clear();
  update_statusline();
}

// --- Help ---

void TerminalController::show_help() {
  terminal_mode_ = TerminalMode::Help;

  auto bindings = app_.bindings().help_bindings();
  int max_key_len = 0;
  for (const auto& hb : bindings) {
    max_key_len = std::max(max_key_len, static_cast<int>(hb.key_label.size()));
  }

  auto format_line = [&](const std::string& key, const std::string& desc) {
    std::string line = key;
    line += std::string(max_key_len - static_cast<int>(key.size()) + 3, ' ');
    line += desc;
    return line;
  };

  std::vector<std::string> lines;
  lines.emplace_back("Key Bindings");
  lines.emplace_back("");
  for (const auto& hb : bindings) {
    lines.push_back(format_line(hb.key_label, hb.description));
  }

  frontend_.show_overlay("Help", lines);
}

void TerminalController::dismiss_overlay() {
  terminal_mode_ = TerminalMode::Normal;
  frontend_.clear_overlay();
  // Re-render document pages
  forward_action(action::Show{});
}

// --- Outline popup ---

bool TerminalController::fuzzy_match(const std::string& text, const std::string& pattern) {
  size_t ti = 0;
  for (size_t pi = 0; pi < pattern.size(); ++pi) {
    char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pi])));
    bool found = false;
    while (ti < text.size()) {
      char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[ti])));
      ++ti;
      if (tc == pc) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

void TerminalController::outline_apply_filter() {
  const auto& outline = app_.outline();
  filtered_indices_.clear();
  for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
    if (outline_filter_.empty() || fuzzy_match(outline[i].title, outline_filter_)) {
      filtered_indices_.push_back(i);
    }
  }
  outline_cursor_ = 0;
  outline_scroll_ = 0;
}

void TerminalController::open_outline() {
  const auto& outline = app_.outline();
  if (outline.empty()) {
    // Use app's flash mechanism - but app doesn't expose set_flash.
    // Just forward a no-op and the statusline shows nothing. Show via statusline.
    frontend_.statusline("No outline", "");
    return;
  }
  terminal_mode_ = TerminalMode::Outline;
  outline_cursor_ = 0;
  outline_scroll_ = 0;
  outline_filter_.clear();
  outline_apply_filter();
  show_outline_popup();
}

void TerminalController::outline_navigate(int delta) {
  if (filtered_indices_.empty()) {
    return;
  }
  outline_cursor_ = std::clamp(outline_cursor_ + delta, 0, static_cast<int>(filtered_indices_.size()) - 1);
  show_outline_popup();
}

void TerminalController::outline_filter_char(char ch) {
  outline_filter_ += ch;
  outline_apply_filter();
  show_outline_popup();
}

void TerminalController::outline_filter_backspace() {
  if (!outline_filter_.empty()) {
    outline_filter_.pop_back();
    outline_apply_filter();
    show_outline_popup();
  }
}

void TerminalController::outline_jump() {
  if (filtered_indices_.empty()) {
    return;
  }
  const auto& outline = app_.outline();
  int page = outline[filtered_indices_[outline_cursor_]].page;
  terminal_mode_ = TerminalMode::Normal;
  frontend_.clear_overlay();
  forward_action(action::GotoPage{page + 1}); // GotoPage is 1-based
}

void TerminalController::close_outline() {
  terminal_mode_ = TerminalMode::Normal;
  frontend_.clear_overlay();
  // Re-render document pages
  forward_action(action::Show{});
}

void TerminalController::show_outline_popup() {
  const auto& outline = app_.outline();
  // Get full terminal size (add inset back since overlay uses full terminal)
  auto client = frontend_.client_info();
  int total_cols = client.cols + frontend_.canvas_inset();
  int total_rows = client.rows;

  int box_lines = std::max(4, total_rows * 3 / 4 - 2);
  int box_width = std::max(20, total_cols * 3 / 4);

  int max_visible = box_lines - 2; // header: filter + separator

  if (outline_cursor_ < outline_scroll_) {
    outline_scroll_ = outline_cursor_;
  }
  if (outline_cursor_ >= outline_scroll_ + max_visible) {
    outline_scroll_ = outline_cursor_ - max_visible + 1;
  }

  int content_w = box_width - 6;
  auto pad_line = [content_w](const std::string& text, int visible_len) -> std::string {
    if (visible_len >= content_w) {
      return text;
    }
    return text + std::string(content_w - visible_len, ' ');
  };

  auto truncate_to = [](const std::string& text, int max_cols) -> std::string {
    int cols = 0;
    size_t pos = 0;
    while (pos < text.size() && cols < max_cols) {
      auto c = static_cast<unsigned char>(text[pos]);
      if (c == 0x1B && pos + 1 < text.size() && text[pos + 1] == '[') {
        pos += 2;
        while (pos < text.size() && static_cast<unsigned char>(text[pos]) < 0x40) {
          ++pos;
        }
        if (pos < text.size()) {
          ++pos;
        }
        continue;
      }
      size_t char_len = 1;
      if (c >= 0xF0) {
        char_len = 4;
      }
      else if (c >= 0xE0) {
        char_len = 3;
      }
      else if (c >= 0x80) {
        char_len = 2;
      }
      pos += char_len;
      ++cols;
    }
    return text.substr(0, pos);
  };

  std::vector<std::string> lines;

  auto filter_line = std::format("> {}", outline_filter_);
  lines.push_back(pad_line(filter_line, static_cast<int>(filter_line.size())));
  std::string sep;
  for (int i = 0; i < content_w; ++i) {
    sep += Horiz;
  }
  lines.push_back(sep);

  int end = std::min(outline_scroll_ + max_visible, static_cast<int>(filtered_indices_.size()));
  for (int vi = outline_scroll_; vi < end; ++vi) {
    const auto& entry = outline[filtered_indices_[vi]];
    std::string indent(entry.level * 2, ' ');
    auto page_str = std::to_string(entry.page + 1);
    auto title_part = std::format("{}{}", indent, entry.title);
    int title_len = static_cast<int>(title_part.size());
    int page_len = static_cast<int>(page_str.size());

    int max_title = content_w - page_len - 1;
    if (title_len > max_title && max_title > 3) {
      title_part = truncate_to(title_part, max_title - 3) + "...";
      title_len = max_title;
    }

    int gap = std::max(1, content_w - title_len - page_len);
    auto visible_text = std::format("{}{:>{}}{}", title_part, "", gap, page_str);

    if (vi == outline_cursor_) {
      lines.push_back(std::format("{}{}", sgr::Bold, visible_text));
    }
    else {
      lines.push_back(visible_text);
    }
  }

  if (filtered_indices_.empty()) {
    lines.push_back(pad_line("  (no matches)", 14));
  }

  while (static_cast<int>(lines.size()) < box_lines) {
    lines.push_back(pad_line("", 0));
  }

  frontend_.show_overlay("Table of Contents", lines);
}

// --- Sidebar ---

int TerminalController::sidebar_effective_width() const {
  if (!sidebar_visible_) {
    return 0;
  }
  auto client = frontend_.client_info();
  int total_cols = client.cols + frontend_.canvas_inset();
  if (sidebar_width_cols_ > 0) {
    return std::min(sidebar_width_cols_, total_cols - 10);
  }
  return std::max(15, total_cols / 5);
}

int TerminalController::active_outline_index() const {
  const auto& outline = app_.outline();
  auto vs = app_.view_state();
  int cp = vs.current_page - 1; // 0-based
  int best = -1;
  for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
    if (outline[i].page <= cp) {
      best = i;
    }
  }
  return best;
}

void TerminalController::sidebar_apply_filter() {
  const auto& outline = app_.outline();
  sidebar_filtered_.clear();
  for (int i = 0; i < static_cast<int>(outline.size()); ++i) {
    if (sidebar_filter_.empty() || fuzzy_match(outline[i].title, sidebar_filter_)) {
      sidebar_filtered_.push_back(i);
    }
  }
  sidebar_cursor_ = 0;
  sidebar_scroll_ = 0;
}

void TerminalController::toggle_sidebar() {
  if (sidebar_visible_) {
    sidebar_visible_ = false;
    sidebar_filter_.clear();
    terminal_mode_ = TerminalMode::Normal;
    frontend_.set_canvas_inset(0);
    frontend_.clear();
    forward_action(action::Resize{});
  }
  else {
    const auto& outline = app_.outline();
    if (outline.empty()) {
      frontend_.statusline("No outline", "");
      return;
    }
    sidebar_visible_ = true;
    sidebar_scroll_ = 0;
    terminal_mode_ = TerminalMode::Sidebar;
    sidebar_filter_.clear();
    sidebar_apply_filter();
    int active = active_outline_index();
    if (active >= 0) {
      for (int fi = 0; fi < static_cast<int>(sidebar_filtered_.size()); ++fi) {
        if (sidebar_filtered_[fi] == active) {
          sidebar_cursor_ = fi;
          break;
        }
      }
    }
    int cols = sidebar_effective_width();
    frontend_.set_canvas_inset(cols);
    frontend_.clear();
    forward_action(action::Resize{});
  }
}

void TerminalController::sidebar_unfocus() {
  terminal_mode_ = TerminalMode::Normal;
  sidebar_filter_.clear();
  sidebar_apply_filter();
  update_sidebar_display();
  update_statusline();
}

void TerminalController::sidebar_close() {
  terminal_mode_ = TerminalMode::Normal;
  sidebar_visible_ = false;
  sidebar_filter_.clear();
  frontend_.set_canvas_inset(0);
  frontend_.clear();
  forward_action(action::Resize{});
}

void TerminalController::sidebar_navigate(int delta) {
  if (sidebar_filtered_.empty()) {
    return;
  }
  sidebar_cursor_ = std::clamp(sidebar_cursor_ + delta, 0, static_cast<int>(sidebar_filtered_.size()) - 1);
  update_sidebar_display();
  update_statusline();
}

void TerminalController::sidebar_filter_char(char ch) {
  sidebar_filter_ += ch;
  sidebar_apply_filter();
  update_sidebar_display();
  update_statusline();
}

void TerminalController::sidebar_filter_backspace() {
  if (!sidebar_filter_.empty()) {
    sidebar_filter_.pop_back();
    sidebar_apply_filter();
    update_sidebar_display();
    update_statusline();
  }
}

void TerminalController::sidebar_jump() {
  if (sidebar_filtered_.empty()) {
    return;
  }
  const auto& outline = app_.outline();
  int page = outline[sidebar_filtered_[sidebar_cursor_]].page;
  terminal_mode_ = TerminalMode::Normal;
  sidebar_filter_.clear();
  sidebar_apply_filter();
  forward_action(action::GotoPage{page + 1}); // 1-based
}

void TerminalController::update_sidebar_display() {
  if (!sidebar_visible_) {
    return;
  }
  const auto& outline = app_.outline();
  if (outline.empty()) {
    return;
  }

  auto client = frontend_.client_info();
  int sidebar_cols = sidebar_effective_width();
  int visible_rows = client.rows - 1;
  bool focused = (terminal_mode_ == TerminalMode::Sidebar);

  if (focused) {
    int header_rows = 2;
    int max_visible = visible_rows - header_rows;

    if (sidebar_cursor_ < sidebar_scroll_) {
      sidebar_scroll_ = sidebar_cursor_;
    }
    if (sidebar_cursor_ >= sidebar_scroll_ + max_visible) {
      sidebar_scroll_ = sidebar_cursor_ - max_visible + 1;
    }

    std::vector<std::string> lines;
    lines.emplace_back("TOC");

    if (!sidebar_filter_.empty()) {
      lines.push_back(std::format("> {}", sidebar_filter_));
    }
    else {
      lines.emplace_back("");
    }

    int highlight_line = -1;
    int end = std::min(sidebar_scroll_ + max_visible, static_cast<int>(sidebar_filtered_.size()));
    for (int vi = sidebar_scroll_; vi < end; ++vi) {
      const auto& entry = outline[sidebar_filtered_[vi]];
      std::string indent(entry.level * 2, ' ');
      lines.push_back(std::format("{}{}", indent, entry.title));
      if (vi == sidebar_cursor_) {
        highlight_line = static_cast<int>(lines.size()) - 1;
      }
    }

    if (sidebar_filtered_.empty()) {
      lines.emplace_back("  (no matches)");
    }

    frontend_.show_sidebar(lines, highlight_line, sidebar_cols, true);
  }
  else {
    int active = active_outline_index();

    if (active >= 0) {
      if (active < sidebar_scroll_) {
        sidebar_scroll_ = active;
      }
      if (active >= sidebar_scroll_ + visible_rows) {
        sidebar_scroll_ = active - visible_rows + 1;
      }
    }

    std::vector<std::string> lines;
    int highlight_line = -1;
    int end = std::min(sidebar_scroll_ + visible_rows, static_cast<int>(outline.size()));
    for (int i = sidebar_scroll_; i < end; ++i) {
      const auto& entry = outline[i];
      std::string indent(entry.level * 2, ' ');
      lines.push_back(std::format("{}{}", indent, entry.title));
      if (i == active) {
        highlight_line = static_cast<int>(lines.size()) - 1;
      }
    }

    frontend_.show_sidebar(lines, highlight_line, sidebar_cols, false);
  }
}

// --- Command parsing ---

std::pair<std::optional<Action>, std::string> TerminalController::parse_command_string(const std::string& raw) {
  std::string input = trim(raw);
  if (input.empty()) {
    return {std::nullopt, {}};
  }

  // Bare number → goto page
  try {
    size_t pos = 0;
    int page = std::stoi(input, &pos);
    if (pos == input.size()) {
      return {action::GotoPage{page}, {}};
    }
  }
  catch (...) {
  }

  auto space = input.find(' ');
  std::string name = (space != std::string::npos) ? input.substr(0, space) : input;
  std::string args = (space != std::string::npos) ? trim(input.substr(space + 1)) : "";

  if (name == "goto" || name == "g") {
    try {
      return {action::GotoPage{std::stoi(args)}, {}};
    }
    catch (...) {
      return {std::nullopt, "Invalid page number"};
    }
  }
  if (name == "q" || name == "quit") {
    return {action::Quit{}, {}};
  }
  if (name == "reload") {
    return {action::Reload{}, {}};
  }
  if (name == "set") {
    auto key_space = args.find(' ');
    std::string key = (key_space != std::string::npos) ? args.substr(0, key_space) : args;
    std::string value = (key_space != std::string::npos) ? trim(args.substr(key_space + 1)) : "";

    if (key == "theme") {
      return {action::SetTheme{value}, {}};
    }
    if (key == "mode") {
      return {action::SetViewMode{value}, {}};
    }
    if (key == "render-scale") {
      return {action::SetRenderScale{value}, {}};
    }
    if (key == "sidebar-width") {
      try {
        return {action::SetSidebarWidth{std::stoi(value)}, {}};
      }
      catch (...) {
        return {std::nullopt, std::format("Invalid width: {}", value)};
      }
    }
    return {std::nullopt, std::format("Unknown setting: {}", key)};
  }
  return {std::nullopt, std::format("Unknown: {}", name)};
}

// --- Input handling ---

void TerminalController::handle_input(const InputEvent& event) {
  spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event.id, event.id, event.modifiers, static_cast<int>(event.type));

  // Handle terminal-mode input directly (don't go through translate)
  if (terminal_mode_ == TerminalMode::Command) {
    bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
    if (!is_press) {
      return;
    }
    if (event.id == 27) {
      command_cancel();
    }
    else if (event.id == '\n' || event.id == '\r') {
      command_execute();
    }
    else if (event.id == 127 || event.id == 8 || event.id == input::Backspace) {
      command_backspace();
    }
    else if (event.id >= 32 && event.id < 127) {
      command_char(static_cast<char>(event.id));
    }
    return;
  }

  if (terminal_mode_ == TerminalMode::Search) {
    bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
    if (!is_press) {
      return;
    }
    if (event.id == 27) {
      search_cancel();
    }
    else if (event.id == '\n' || event.id == '\r') {
      search_execute();
    }
    else if (event.id == 127 || event.id == 8 || event.id == input::Backspace) {
      search_backspace();
    }
    else if (event.id >= 32 && event.id < 127) {
      search_char(static_cast<char>(event.id));
    }
    return;
  }

  if (terminal_mode_ == TerminalMode::Help) {
    bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
    if (!is_press) {
      return;
    }
    dismiss_overlay();
    return;
  }

  if (terminal_mode_ == TerminalMode::Outline) {
    bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
    if (!is_press) {
      return;
    }
    auto client = frontend_.client_info();
    int total_rows = client.rows;
    if (event.id == 27 || event.id == 'q') {
      close_outline();
    }
    else if (event.id == 0x0E || event.id == input::ArrowDown) {
      outline_navigate(1);
    }
    else if (event.id == 0x10 || event.id == input::ArrowUp) {
      outline_navigate(-1);
    }
    else if (event.id == input::PageDown) {
      int page_size = std::max(1, total_rows * 3 / 4 - 4);
      outline_navigate(page_size);
    }
    else if (event.id == input::PageUp) {
      int page_size = std::max(1, total_rows * 3 / 4 - 4);
      outline_navigate(-page_size);
    }
    else if (event.id == input::Home) {
      outline_navigate(-10000);
    }
    else if (event.id == input::End) {
      outline_navigate(10000);
    }
    else if (event.id == '\n' || event.id == '\r') {
      outline_jump();
    }
    else if (event.id == 127 || event.id == 8 || event.id == input::Backspace) {
      outline_filter_backspace();
    }
    else if (event.id >= 32 && event.id < 127) {
      outline_filter_char(static_cast<char>(event.id));
    }
    return;
  }

  if (terminal_mode_ == TerminalMode::Sidebar) {
    bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
    if (!is_press) {
      return;
    }
    auto client = frontend_.client_info();
    int total_rows = client.rows;
    if (event.id == 27) {
      sidebar_unfocus();
    }
    else if (event.id == 'q' || event.id == 'e') {
      sidebar_close();
    }
    else if (event.id == 0x0E || event.id == input::ArrowDown) {
      sidebar_navigate(1);
    }
    else if (event.id == 0x10 || event.id == input::ArrowUp) {
      sidebar_navigate(-1);
    }
    else if (event.id == input::PageDown) {
      int page_size = std::max(1, total_rows - 3);
      sidebar_navigate(page_size);
    }
    else if (event.id == input::PageUp) {
      int page_size = std::max(1, total_rows - 3);
      sidebar_navigate(-page_size);
    }
    else if (event.id == input::Home) {
      sidebar_navigate(-10000);
    }
    else if (event.id == input::End) {
      sidebar_navigate(10000);
    }
    else if (event.id == '\n' || event.id == '\r') {
      sidebar_jump();
    }
    else if (event.id == 127 || event.id == 8 || event.id == input::Backspace) {
      sidebar_filter_backspace();
    }
    else if (event.id >= 32 && event.id < 127) {
      sidebar_filter_char(static_cast<char>(event.id));
    }
    return;
  }

  // Normal mode — use translate() for shared commands
  auto client = frontend_.client_info();
  auto mode = effective_input_mode();
  auto act = input_handler_.translate(event, mode, client.rows, client.cell);
  if (!act) {
    return;
  }

  // Intercept terminal-only actions before forwarding
  if (std::holds_alternative<action::EnterCommandMode>(*act)) {
    enter_command_mode();
    return;
  }
  if (std::holds_alternative<action::EnterSearchMode>(*act)) {
    enter_search_mode();
    return;
  }
  if (std::holds_alternative<action::ShowHelp>(*act)) {
    show_help();
    return;
  }
  if (std::holds_alternative<action::OpenOutline>(*act)) {
    open_outline();
    return;
  }
  if (std::holds_alternative<action::ToggleSidebar>(*act)) {
    toggle_sidebar();
    return;
  }

  // Subtract canvas inset from mouse coordinates for ClickAt/DragStart/DragUpdate/DragEnd
  int inset = frontend_.canvas_inset();
  if (inset > 0) {
    if (auto* click = std::get_if<action::ClickAt>(&*act)) {
      act = action::ClickAt{click->col - inset, click->row};
    }
    else if (auto* ds = std::get_if<action::DragStart>(&*act)) {
      act = action::DragStart{ds->col - inset, ds->row};
    }
    else if (auto* du = std::get_if<action::DragUpdate>(&*act)) {
      act = action::DragUpdate{du->col - inset, du->row};
    }
    else if (auto* de = std::get_if<action::DragEnd>(&*act)) {
      act = action::DragEnd{de->col - inset, de->row};
    }
  }

  // Scroll coalescing: drain pending scroll events before rendering
  auto delta = scroll_delta(*act, client.cell);
  if (delta) {
    int dx = delta->first;
    int dy = delta->second;
    while (auto next = frontend_.poll_input(0)) {
      auto next_act = input_handler_.translate(*next, effective_input_mode(), client.rows, client.cell);
      if (next_act) {
        auto next_delta = scroll_delta(*next_act, client.cell);
        if (next_delta) {
          dx += next_delta->first;
          dy += next_delta->second;
        }
        else {
          if (dx != 0 || dy != 0) {
            forward_action(action::MouseScroll{dx, dy});
            dx = 0;
            dy = 0;
          }
          forward_action(*next_act);
          break;
        }
      }
    }
    if (dx != 0 || dy != 0) {
      forward_action(action::MouseScroll{dx, dy});
    }
  }
  else if (auto* move = std::get_if<action::SelectionMove>(&*act)) {
    int dx = move->dx;
    int dy = move->dy;
    while (auto next = frontend_.poll_input(0)) {
      auto next_act = input_handler_.translate(*next, effective_input_mode(), client.rows, client.cell);
      if (next_act) {
        if (auto* nm = std::get_if<action::SelectionMove>(&*next_act)) {
          dx += nm->dx;
          dy += nm->dy;
        }
        else {
          forward_action(action::SelectionMove{dx, dy});
          forward_action(*next_act);
          dx = 0;
          dy = 0;
          break;
        }
      }
    }
    if (dx != 0 || dy != 0) {
      forward_action(action::SelectionMove{dx, dy});
    }
  }
  else if (auto* drag = std::get_if<action::DragUpdate>(&*act)) {
    int col = drag->col;
    int row = drag->row;
    while (auto next = frontend_.poll_input(0)) {
      auto next_act = input_handler_.translate(*next, effective_input_mode(), client.rows, client.cell);
      if (next_act) {
        if (auto* nd = std::get_if<action::DragUpdate>(&*next_act)) {
          col = nd->col;
          row = nd->row;
        }
        else {
          forward_action(action::DragUpdate{col, row});
          forward_action(*next_act);
          col = -1;
          break;
        }
      }
    }
    if (col >= 0) {
      forward_action(action::DragUpdate{col, row});
    }
  }
  else {
    forward_action(*act);
  }
}
