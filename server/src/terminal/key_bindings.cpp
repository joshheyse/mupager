#include "terminal/key_bindings.h"

#include <cctype>
#include <unordered_map>

const ActionInfo ActionTable[] = {
    {cmd::ScrollDown::Action, "Scroll Down", [] { return Command{cmd::ScrollDown{}}; }},
    {cmd::ScrollUp::Action, "Scroll Up", [] { return Command{cmd::ScrollUp{}}; }},
    {cmd::HalfPageDown::Action, "Half Page Down", [] { return Command{cmd::HalfPageDown{}}; }},
    {cmd::HalfPageUp::Action, "Half Page Up", [] { return Command{cmd::HalfPageUp{}}; }},
    {cmd::PageDown::Action, "Page Down", [] { return Command{cmd::PageDown{}}; }},
    {cmd::PageUp::Action, "Page Up", [] { return Command{cmd::PageUp{}}; }},
    {cmd::ScrollLeft::Action, "Scroll Left", [] { return Command{cmd::ScrollLeft{}}; }},
    {cmd::ScrollRight::Action, "Scroll Right", [] { return Command{cmd::ScrollRight{}}; }},
    {cmd::ZoomIn::Action, "Zoom In", [] { return Command{cmd::ZoomIn{}}; }},
    {cmd::ZoomOut::Action, "Zoom Out", [] { return Command{cmd::ZoomOut{}}; }},
    {cmd::ZoomReset::Action, "Fit Width", [] { return Command{cmd::ZoomReset{}}; }},
    {cmd::ToggleViewMode::Action, "Toggle View", [] { return Command{cmd::ToggleViewMode{}}; }},
    {cmd::ToggleTheme::Action, "Toggle Theme", [] { return Command{cmd::ToggleTheme{}}; }},
    {cmd::Quit::Action, "Quit", [] { return Command{cmd::Quit{}}; }},
    {cmd::GotoFirstPage::Action, "First Page", [] { return Command{cmd::GotoFirstPage{}}; }},
    {cmd::GotoLastPage::Action, "Last Page", [] { return Command{cmd::GotoLastPage{}}; }},
    {cmd::JumpBack::Action, "Jump Back", [] { return Command{cmd::JumpBack{}}; }},
    {cmd::JumpForward::Action, "Jump Forward", [] { return Command{cmd::JumpForward{}}; }},
    {cmd::EnterLinkHints::Action, "Link Hints", [] { return Command{cmd::EnterLinkHints{}}; }},
    {cmd::EnterCommandMode::Action, "Command Mode", [] { return Command{cmd::EnterCommandMode{}}; }},
    {cmd::EnterSearchMode::Action, "Search", [] { return Command{cmd::EnterSearchMode{}}; }},
    {cmd::SearchNext::Action, "Next Match", [] { return Command{cmd::SearchNext{}}; }},
    {cmd::SearchPrev::Action, "Previous Match", [] { return Command{cmd::SearchPrev{}}; }},
    {cmd::OpenOutline::Action, "Table of Contents", [] { return Command{cmd::OpenOutline{}}; }},
    {cmd::ToggleSidebar::Action, "Toggle Sidebar TOC", [] { return Command{cmd::ToggleSidebar{}}; }},
    {cmd::ShowHelp::Action, "Toggle Help", [] { return Command{cmd::ShowHelp{}}; }},
    {cmd::ClearSearch::Action, "Clear Search", [] { return Command{cmd::ClearSearch{}}; }},
    {cmd::EnterVisualMode::Action, "Visual Select", [] { return Command{cmd::EnterVisualMode{}}; }},
    {cmd::EnterVisualBlockMode::Action, "Visual Block Select", [] { return Command{cmd::EnterVisualBlockMode{}}; }},
    {cmd::SelectionYank::Action, "Yank Selection", [] { return Command{cmd::SelectionYank{}}; }},
    {"visual-next-word", "Next Word", [] { return Command{cmd::SelectionMoveWord{1}}; }},
    {"visual-prev-word", "Previous Word", [] { return Command{cmd::SelectionMoveWord{-1}}; }},
    {"visual-word-end", "Word End", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::WordEnd}}; }},
    {"visual-line-start", "Line Start", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::LineStart}}; }},
    {"visual-line-end", "Line End", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::LineEnd}}; }},
    {"visual-first-non-space", "First Non-Space", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::FirstNonSpace}}; }},
    {"visual-doc-start", "Document Start", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::DocStart}}; }},
    {"visual-doc-end", "Document End", [] { return Command{cmd::SelectionGoto{cmd::SelectionTarget::DocEnd}}; }},
};

const size_t ActionTableSize = sizeof(ActionTable) / sizeof(ActionTable[0]);

/// @brief Map of special key names to their input constants.
static const std::unordered_map<std::string, std::pair<uint32_t, std::string>>& special_key_map() {
  static const std::unordered_map<std::string, std::pair<uint32_t, std::string>> Map = {
      {"Tab", {0x09, "Tab"}},
      {"Esc", {27, "Esc"}},
      {"Escape", {27, "Esc"}},
      {"Up", {input::ArrowUp, "\xe2\x86\x91"}},
      {"Down", {input::ArrowDown, "\xe2\x86\x93"}},
      {"Left", {input::ArrowLeft, "\xe2\x86\x90"}},
      {"Right", {input::ArrowRight, "\xe2\x86\x92"}},
      {"PageUp", {input::PageUp, "PageUp"}},
      {"PageDown", {input::PageDown, "PageDown"}},
      {"Home", {input::Home, "Home"}},
      {"End", {input::End, "End"}},
      {"Space", {' ', "Space"}},
      {"Enter", {'\r', "Enter"}},
      {"Backspace", {input::Backspace, "Backspace"}},
  };
  return Map;
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

const ActionInfo* action_from_name(std::string_view name) {
  for (size_t i = 0; i < ActionTableSize; ++i) {
    if (name == ActionTable[i].name) {
      return &ActionTable[i];
    }
  }
  return nullptr;
}

KeyBindings KeyBindings::defaults() {
  KeyBindings kb;

  auto bind_str = [&](const char* action_name, const std::string& spec) {
    auto* info = action_from_name(action_name);
    auto ks = parse_key_spec(spec);
    if (info && ks) {
      kb.bind(info, *ks);
    }
  };

  bind_str(cmd::ScrollDown::Action, "j");
  bind_str(cmd::ScrollDown::Action, "Down");
  bind_str(cmd::ScrollUp::Action, "k");
  bind_str(cmd::ScrollUp::Action, "Up");
  bind_str(cmd::HalfPageDown::Action, "d");
  bind_str(cmd::HalfPageUp::Action, "u");
  bind_str(cmd::PageDown::Action, "Ctrl+F");
  bind_str(cmd::PageUp::Action, "Ctrl+B");
  bind_str(cmd::ScrollLeft::Action, "h");
  bind_str(cmd::ScrollLeft::Action, "Left");
  bind_str(cmd::ScrollRight::Action, "l");
  bind_str(cmd::ScrollRight::Action, "Right");
  bind_str(cmd::ZoomIn::Action, "+");
  bind_str(cmd::ZoomIn::Action, "=");
  bind_str(cmd::ZoomOut::Action, "-");
  bind_str(cmd::ZoomReset::Action, "0");
  bind_str(cmd::ZoomReset::Action, "w");
  bind_str(cmd::ToggleViewMode::Action, "Tab");
  bind_str(cmd::ToggleTheme::Action, "t");
  bind_str(cmd::Quit::Action, "q");
  bind_str(cmd::GotoFirstPage::Action, "gg");
  bind_str(cmd::GotoLastPage::Action, "G");
  bind_str(cmd::JumpBack::Action, "H");
  bind_str(cmd::JumpForward::Action, "L");
  bind_str(cmd::EnterLinkHints::Action, "f");
  bind_str(cmd::EnterCommandMode::Action, ":");
  bind_str(cmd::EnterSearchMode::Action, "/");
  bind_str(cmd::SearchNext::Action, "n");
  bind_str(cmd::SearchPrev::Action, "N");
  bind_str(cmd::OpenOutline::Action, "o");
  bind_str(cmd::ToggleSidebar::Action, "e");
  bind_str(cmd::ShowHelp::Action, "?");
  bind_str(cmd::ClearSearch::Action, "Esc");
  bind_str(cmd::EnterVisualMode::Action, "v");
  bind_str(cmd::EnterVisualBlockMode::Action, "Ctrl+V");
  bind_str(cmd::SelectionYank::Action, "y");

  return kb;
}

void KeyBindings::bind(const ActionInfo* info, const KeySpec& key) {
  if (key.is_sequence) {
    prefix_key_ = key.id;
    double_prefix_info_ = info;
    has_sequence_ = true;
  }
  else {
    key_to_info_[key.id] = info;
  }

  info_to_keys_[info].push_back(key);
}

void KeyBindings::clear(const ActionInfo* info) {
  auto it = info_to_keys_.find(info);
  if (it == info_to_keys_.end()) {
    return;
  }

  for (const auto& ks : it->second) {
    if (ks.is_sequence) {
      if (has_sequence_ && double_prefix_info_ == info) {
        prefix_key_ = 0;
        has_sequence_ = false;
      }
    }
    else {
      key_to_info_.erase(ks.id);
    }
  }

  it->second.clear();
}

const ActionInfo* KeyBindings::lookup(uint32_t key_id) const {
  auto it = key_to_info_.find(key_id);
  if (it != key_to_info_.end()) {
    return it->second;
  }
  return nullptr;
}

const ActionInfo* KeyBindings::sequence_double_info() const {
  if (has_sequence_) {
    return double_prefix_info_;
  }
  return nullptr;
}

std::vector<HelpBinding> KeyBindings::help_bindings() const {
  std::vector<HelpBinding> result;

  for (size_t i = 0; i < ActionTableSize; ++i) {
    const auto* info = &ActionTable[i];
    auto it = info_to_keys_.find(info);
    if (it == info_to_keys_.end() || it->second.empty()) {
      continue;
    }

    std::string label;
    for (const auto& ks : it->second) {
      if (!label.empty()) {
        label += " / ";
      }
      label += ks.label;
    }

    result.push_back({std::move(label), info->description});
  }

  // Non-configurable entries
  result.push_back({"[n]gg / [n]G", "Go to Page n"});
  result.push_back({":reload", "Reload Document"});

  return result;
}
