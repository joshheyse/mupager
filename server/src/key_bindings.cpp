#include "key_bindings.h"

#include <cctype>
#include <unordered_map>

const ActionInfo ACTION_TABLE[] = {
    {"scroll-down", "Scroll Down", Action::SCROLL_DOWN},
    {"scroll-up", "Scroll Up", Action::SCROLL_UP},
    {"half-page-down", "Half Page Down", Action::HALF_PAGE_DOWN},
    {"half-page-up", "Half Page Up", Action::HALF_PAGE_UP},
    {"page-down", "Page Down", Action::PAGE_DOWN},
    {"page-up", "Page Up", Action::PAGE_UP},
    {"scroll-left", "Scroll Left", Action::SCROLL_LEFT},
    {"scroll-right", "Scroll Right", Action::SCROLL_RIGHT},
    {"zoom-in", "Zoom In", Action::ZOOM_IN},
    {"zoom-out", "Zoom Out", Action::ZOOM_OUT},
    {"zoom-reset", "Fit Width", Action::ZOOM_RESET},
    {"toggle-view", "Toggle View", Action::TOGGLE_VIEW},
    {"toggle-theme", "Toggle Theme", Action::TOGGLE_THEME},
    {"quit", "Quit", Action::QUIT},
    {"first-page", "First Page", Action::FIRST_PAGE},
    {"last-page", "Last Page", Action::LAST_PAGE},
    {"jump-back", "Jump Back", Action::JUMP_BACK},
    {"jump-forward", "Jump Forward", Action::JUMP_FORWARD},
    {"link-hints", "Link Hints", Action::LINK_HINTS},
    {"command-mode", "Command Mode", Action::COMMAND_MODE},
    {"search", "Search", Action::SEARCH},
    {"next-match", "Next Match", Action::NEXT_MATCH},
    {"prev-match", "Previous Match", Action::PREV_MATCH},
    {"outline", "Table of Contents", Action::OUTLINE},
    {"sidebar", "Toggle Sidebar TOC", Action::SIDEBAR},
    {"help", "Toggle Help", Action::HELP},
    {"clear-search", "Clear Search", Action::CLEAR_SEARCH},
};

const size_t ACTION_TABLE_SIZE = sizeof(ACTION_TABLE) / sizeof(ACTION_TABLE[0]);

/// @brief Map of special key names to their input constants.
static const std::unordered_map<std::string, std::pair<uint32_t, std::string>>& special_key_map() {
  static const std::unordered_map<std::string, std::pair<uint32_t, std::string>> MAP = {
      {"Tab", {0x09, "Tab"}},
      {"Esc", {27, "Esc"}},
      {"Escape", {27, "Esc"}},
      {"Up", {input::ARROW_UP, "\xe2\x86\x91"}},
      {"Down", {input::ARROW_DOWN, "\xe2\x86\x93"}},
      {"Left", {input::ARROW_LEFT, "\xe2\x86\x90"}},
      {"Right", {input::ARROW_RIGHT, "\xe2\x86\x92"}},
      {"PageUp", {input::PAGE_UP, "PageUp"}},
      {"PageDown", {input::PAGE_DOWN, "PageDown"}},
      {"Home", {input::HOME, "Home"}},
      {"End", {input::END, "End"}},
      {"Space", {' ', "Space"}},
      {"Enter", {'\r', "Enter"}},
      {"Backspace", {input::BACKSPACE, "Backspace"}},
  };
  return MAP;
}

std::optional<KeySpec> parse_key_spec(const std::string& spec) {
  if (spec.empty()) {
    return std::nullopt;
  }

  // Double-key sequence (e.g. "gg")
  if (spec.size() == 2 && spec[0] == spec[1] && std::isalpha(static_cast<unsigned char>(spec[0]))) {
    return KeySpec{static_cast<uint32_t>(spec[0]), spec, true};
  }

  // Ctrl+X
  if (spec.size() >= 6 && spec.substr(0, 5) == "Ctrl+") {
    char ch = spec[5];
    if (spec.size() == 6 && std::isalpha(static_cast<unsigned char>(ch))) {
      char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      uint32_t ctrl_code = static_cast<uint32_t>(upper - 'A' + 1);
      return KeySpec{ctrl_code, spec, false};
    }
    return std::nullopt;
  }

  // Special named keys
  const auto& specials = special_key_map();
  auto it = specials.find(spec);
  if (it != specials.end()) {
    return KeySpec{it->second.first, it->second.second, false};
  }

  // Single character
  if (spec.size() == 1) {
    char ch = spec[0];
    if (ch >= 32 && ch < 127) {
      return KeySpec{static_cast<uint32_t>(ch), spec, false};
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<Action> action_from_name(std::string_view name) {
  for (size_t i = 0; i < ACTION_TABLE_SIZE; ++i) {
    if (name == ACTION_TABLE[i].name) {
      return ACTION_TABLE[i].action;
    }
  }
  return std::nullopt;
}

const char* action_description(Action a) {
  for (size_t i = 0; i < ACTION_TABLE_SIZE; ++i) {
    if (ACTION_TABLE[i].action == a) {
      return ACTION_TABLE[i].description;
    }
  }
  return "";
}

RpcCommand action_to_command(Action a) {
  switch (a) {
    case Action::SCROLL_DOWN:
      return cmd::ScrollDown{};
    case Action::SCROLL_UP:
      return cmd::ScrollUp{};
    case Action::HALF_PAGE_DOWN:
      return cmd::HalfPageDown{};
    case Action::HALF_PAGE_UP:
      return cmd::HalfPageUp{};
    case Action::PAGE_DOWN:
      return cmd::PageDown{};
    case Action::PAGE_UP:
      return cmd::PageUp{};
    case Action::SCROLL_LEFT:
      return cmd::ScrollLeft{};
    case Action::SCROLL_RIGHT:
      return cmd::ScrollRight{};
    case Action::ZOOM_IN:
      return cmd::ZoomIn{};
    case Action::ZOOM_OUT:
      return cmd::ZoomOut{};
    case Action::ZOOM_RESET:
      return cmd::ZoomReset{};
    case Action::TOGGLE_VIEW:
      return cmd::ToggleViewMode{};
    case Action::TOGGLE_THEME:
      return cmd::ToggleTheme{};
    case Action::QUIT:
      return cmd::Quit{};
    case Action::FIRST_PAGE:
      return cmd::GotoFirstPage{};
    case Action::LAST_PAGE:
      return cmd::GotoLastPage{};
    case Action::JUMP_BACK:
      return cmd::JumpBack{};
    case Action::JUMP_FORWARD:
      return cmd::JumpForward{};
    case Action::LINK_HINTS:
      return cmd::EnterLinkHints{};
    case Action::COMMAND_MODE:
      return cmd::EnterCommandMode{};
    case Action::SEARCH:
      return cmd::EnterSearchMode{};
    case Action::NEXT_MATCH:
      return cmd::SearchNext{};
    case Action::PREV_MATCH:
      return cmd::SearchPrev{};
    case Action::OUTLINE:
      return cmd::OpenOutline{};
    case Action::SIDEBAR:
      return cmd::ToggleSidebar{};
    case Action::HELP:
      return cmd::ShowHelp{};
    case Action::CLEAR_SEARCH:
      return cmd::ClearSearch{};
  }
  return cmd::Quit{};
}

KeyBindings KeyBindings::defaults() {
  KeyBindings kb;

  auto bind_str = [&](Action action, const std::string& spec) {
    auto ks = parse_key_spec(spec);
    if (ks) {
      kb.bind(action, *ks);
    }
  };

  bind_str(Action::SCROLL_DOWN, "j");
  bind_str(Action::SCROLL_DOWN, "Down");
  bind_str(Action::SCROLL_UP, "k");
  bind_str(Action::SCROLL_UP, "Up");
  bind_str(Action::HALF_PAGE_DOWN, "d");
  bind_str(Action::HALF_PAGE_UP, "u");
  bind_str(Action::PAGE_DOWN, "Ctrl+F");
  bind_str(Action::PAGE_UP, "Ctrl+B");
  bind_str(Action::SCROLL_LEFT, "h");
  bind_str(Action::SCROLL_LEFT, "Left");
  bind_str(Action::SCROLL_RIGHT, "l");
  bind_str(Action::SCROLL_RIGHT, "Right");
  bind_str(Action::ZOOM_IN, "+");
  bind_str(Action::ZOOM_IN, "=");
  bind_str(Action::ZOOM_OUT, "-");
  bind_str(Action::ZOOM_RESET, "0");
  bind_str(Action::ZOOM_RESET, "w");
  bind_str(Action::TOGGLE_VIEW, "Tab");
  bind_str(Action::TOGGLE_THEME, "t");
  bind_str(Action::QUIT, "q");
  bind_str(Action::FIRST_PAGE, "gg");
  bind_str(Action::LAST_PAGE, "G");
  bind_str(Action::JUMP_BACK, "H");
  bind_str(Action::JUMP_FORWARD, "L");
  bind_str(Action::LINK_HINTS, "f");
  bind_str(Action::COMMAND_MODE, ":");
  bind_str(Action::SEARCH, "/");
  bind_str(Action::NEXT_MATCH, "n");
  bind_str(Action::PREV_MATCH, "N");
  bind_str(Action::OUTLINE, "o");
  bind_str(Action::SIDEBAR, "e");
  bind_str(Action::HELP, "?");
  bind_str(Action::CLEAR_SEARCH, "Esc");

  return kb;
}

void KeyBindings::bind(Action action, const KeySpec& key) {
  int action_idx = static_cast<int>(action);

  if (key.is_sequence) {
    prefix_key_ = key.id;
    double_prefix_action_ = action;
    has_sequence_ = true;
  }
  else {
    key_to_action_[key.id] = action;
  }

  action_to_keys_[action_idx].push_back(key);
}

void KeyBindings::clear(Action action) {
  int action_idx = static_cast<int>(action);
  auto it = action_to_keys_.find(action_idx);
  if (it == action_to_keys_.end()) {
    return;
  }

  for (const auto& ks : it->second) {
    if (ks.is_sequence) {
      if (has_sequence_ && double_prefix_action_ == action) {
        prefix_key_ = 0;
        has_sequence_ = false;
      }
    }
    else {
      key_to_action_.erase(ks.id);
    }
  }

  it->second.clear();
}

std::optional<Action> KeyBindings::lookup(uint32_t key_id) const {
  auto it = key_to_action_.find(key_id);
  if (it != key_to_action_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<Action> KeyBindings::sequence_double_action() const {
  if (has_sequence_) {
    return double_prefix_action_;
  }
  return std::nullopt;
}

std::vector<HelpBinding> KeyBindings::help_bindings() const {
  std::vector<HelpBinding> result;

  for (size_t i = 0; i < ACTION_TABLE_SIZE; ++i) {
    Action action = ACTION_TABLE[i].action;
    int action_idx = static_cast<int>(action);
    auto it = action_to_keys_.find(action_idx);
    if (it == action_to_keys_.end() || it->second.empty()) {
      continue;
    }

    std::string label;
    for (const auto& ks : it->second) {
      if (!label.empty()) {
        label += " / ";
      }
      label += ks.label;
    }

    result.push_back({std::move(label), ACTION_TABLE[i].description});
  }

  // Non-configurable entries
  result.push_back({"[n]gg / [n]G", "Go to Page n"});
  result.push_back({":reload", "Reload Document"});

  return result;
}
