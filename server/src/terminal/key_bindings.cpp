#include "terminal/key_bindings.hpp"

#include "action.hpp"
#include "action_traits.hpp"
#include "input_event.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct EntryTable {
  static constexpr size_t MaxEntries = 64;
  ActionEntry entries[MaxEntries]{};
  size_t count = 0;

  void add(std::string_view name, std::string_view description, Action (*make)()) {
    if (count < MaxEntries) {
      entries[count] = {name, description, make};
      count++;
    }
  }
};

/// @brief Collect named actions by iterating the variant.
template <std::size_t I = 0>
void collect_named_actions(EntryTable& table) {
  if constexpr (I < std::variant_size_v<Action>) {
    using T = std::variant_alternative_t<I, Action>;
    if constexpr (IsBindable<T>::value) {
      table.add(T::Name, T::Description, []() -> Action { return T{}; });
    }
    collect_named_actions<I + 1>(table);
  }
}

EntryTable& get_entry_table() {
  static EntryTable table_ = [] {
    EntryTable t;
    collect_named_actions(t);
    // Synthetic entries for visual motions (no Name on the structs)
    t.add("visual_next_word", "Next Word", [] { return Action{action::SelectionMoveWord{1}}; });
    t.add("visual_prev_word", "Previous Word", [] { return Action{action::SelectionMoveWord{-1}}; });
    t.add("visual_word_end", "Word End", [] { return Action{action::SelectionGoto{action::SelectionTarget::WordEnd}}; });
    t.add("visual_line_start", "Line Start", [] { return Action{action::SelectionGoto{action::SelectionTarget::LineStart}}; });
    t.add("visual_line_end", "Line End", [] { return Action{action::SelectionGoto{action::SelectionTarget::LineEnd}}; });
    t.add("visual_first_non_space", "First Non-Space", [] { return Action{action::SelectionGoto{action::SelectionTarget::FirstNonSpace}}; });
    t.add("visual_doc_start", "Document Start", [] { return Action{action::SelectionGoto{action::SelectionTarget::DocStart}}; });
    t.add("visual_doc_end", "Document End", [] { return Action{action::SelectionGoto{action::SelectionTarget::DocEnd}}; });
    return t;
  }();
  return table_;
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
/// @brief Named keys recognized by KeySpec::parse: name, key ID, and display label.
static constexpr struct {
  std::string_view name;
  uint32_t id;
  std::string_view label;
} NamedKeys[] = {
    {"Tab", 0x09, "Tab"},
    {"Esc", 27, "Esc"},
    {"Escape", 27, "Esc"},
    {"Up", input::ArrowUp, "\xe2\x86\x91"},
    {"Down", input::ArrowDown, "\xe2\x86\x93"},
    {"Left", input::ArrowLeft, "\xe2\x86\x90"},
    {"Right", input::ArrowRight, "\xe2\x86\x92"},
    {"PageUp", input::PageUp, "PageUp"},
    {"PageDown", input::PageDown, "PageDown"},
    {"Home", input::Home, "Home"},
    {"End", input::End, "End"},
    {"Space", ' ', "Space"},
    {"Enter", '\r', "Enter"},
    {"Backspace", input::Backspace, "Backspace"},
};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays)

std::optional<KeySpec> KeySpec::parse(std::string_view spec) {
  if (spec.empty()) {
    return std::nullopt;
  }

  // Double-key sequence (e.g. "gg")
  if (spec.size() == 2 && spec[0] == spec[1] && std::isalpha(static_cast<unsigned char>(spec[0]))) {
    return KeySpec{static_cast<uint32_t>(spec[0]), std::string(spec), true};
  }

  // Ctrl+X
  if (spec.size() >= 6 && spec.substr(0, 5) == "Ctrl+") {
    char ch = spec[5];
    if (spec.size() == 6 && std::isalpha(static_cast<unsigned char>(ch))) {
      char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      uint32_t ctrl_code = static_cast<uint32_t>(upper - 'A' + 1);
      return KeySpec{ctrl_code, std::string(spec), false};
    }
    return std::nullopt;
  }

  // Named keys (Tab, Esc, arrows, etc.)
  for (const auto& nk : NamedKeys) {
    if (spec == nk.name) {
      return KeySpec{nk.id, std::string(nk.label), false};
    }
  }

  // Single character
  if (spec.size() == 1) {
    char ch = spec[0];
    if (ch >= 32 && ch < 127) {
      return KeySpec{static_cast<uint32_t>(ch), std::string(spec), false};
    }
    return std::nullopt;
  }

  return std::nullopt;
}

const ActionEntry* KeyBindings::entry_from_name(std::string_view name) {
  auto& table = get_entry_table();
  for (size_t i = 0; i < table.count; ++i) {
    if (name == table.entries[i].name) {
      return &table.entries[i];
    }
  }
  return nullptr;
}

size_t KeyBindings::entry_count() {
  return get_entry_table().count;
}

KeyBindings KeyBindings::defaults() {
  KeyBindings kb;

  auto bind_str = [&](std::string_view action_name, std::string_view spec) {
    auto* entry = entry_from_name(action_name);
    auto ks = KeySpec::parse(spec);
    if (entry && ks) {
      kb.bind(entry, *ks);
    }
  };

  bind_str(action::ScrollDown::Name, "j");
  bind_str(action::ScrollDown::Name, "Down");
  bind_str(action::ScrollUp::Name, "k");
  bind_str(action::ScrollUp::Name, "Up");
  bind_str(action::HalfPageDown::Name, "d");
  bind_str(action::HalfPageUp::Name, "u");
  bind_str(action::PageDown::Name, "Ctrl+F");
  bind_str(action::PageUp::Name, "Ctrl+B");
  bind_str(action::ScrollLeft::Name, "h");
  bind_str(action::ScrollLeft::Name, "Left");
  bind_str(action::ScrollRight::Name, "l");
  bind_str(action::ScrollRight::Name, "Right");
  bind_str(action::ZoomIn::Name, "+");
  bind_str(action::ZoomIn::Name, "=");
  bind_str(action::ZoomOut::Name, "-");
  bind_str(action::ZoomReset::Name, "0");
  bind_str(action::ZoomReset::Name, "w");
  bind_str(action::ToggleViewMode::Name, "Tab");
  bind_str(action::ToggleTheme::Name, "t");
  bind_str(action::Quit::Name, "q");
  bind_str(action::GotoFirstPage::Name, "gg");
  bind_str(action::GotoLastPage::Name, "G");
  bind_str(action::JumpBack::Name, "H");
  bind_str(action::JumpForward::Name, "L");
  bind_str(action::EnterLinkHints::Name, "f");
  bind_str(action::EnterCommandMode::Name, ":");
  bind_str(action::EnterSearchMode::Name, "/");
  bind_str(action::SearchNext::Name, "n");
  bind_str(action::SearchPrev::Name, "N");
  bind_str(action::OpenOutline::Name, "o");
  bind_str(action::ToggleSidebar::Name, "e");
  bind_str(action::ShowHelp::Name, "?");
  bind_str(action::ClearSearch::Name, "Esc");
  bind_str(action::EnterVisualMode::Name, "v");
  bind_str(action::EnterVisualBlockMode::Name, "Ctrl+V");
  bind_str(action::SelectionYank::Name, "y");

  return kb;
}

void KeyBindings::bind(const ActionEntry* entry, const KeySpec& key) {
  if (key.is_sequence) {
    prefix_key_ = key.id;
    double_prefix_entry_ = entry;
    has_sequence_ = true;
  }
  else {
    key_to_entry_[key.id] = entry;
  }

  entry_to_keys_[entry].push_back(key);
}

void KeyBindings::clear(const ActionEntry* entry) {
  auto it = entry_to_keys_.find(entry);
  if (it == entry_to_keys_.end()) {
    return;
  }

  for (const auto& ks : it->second) {
    if (ks.is_sequence) {
      if (has_sequence_ && double_prefix_entry_ == entry) {
        prefix_key_ = 0;
        has_sequence_ = false;
      }
    }
    else {
      key_to_entry_.erase(ks.id);
    }
  }

  it->second.clear();
}

const ActionEntry* KeyBindings::lookup(uint32_t key_id) const {
  auto it = key_to_entry_.find(key_id);
  if (it != key_to_entry_.end()) {
    return it->second;
  }
  return nullptr;
}

const ActionEntry* KeyBindings::sequence_double_entry() const {
  if (has_sequence_) {
    return double_prefix_entry_;
  }
  return nullptr;
}

std::vector<HelpBinding> KeyBindings::help_bindings() const {
  auto& table = get_entry_table();
  std::vector<HelpBinding> result;

  for (size_t i = 0; i < table.count; ++i) {
    const auto* entry = &table.entries[i];
    auto it = entry_to_keys_.find(entry);
    if (it == entry_to_keys_.end() || it->second.empty()) {
      continue;
    }

    std::string label;
    for (const auto& ks : it->second) {
      if (!label.empty()) {
        label += " / ";
      }
      label += ks.label;
    }

    result.push_back({std::move(label), std::string(entry->description)});
  }

  // Non-configurable entries
  result.push_back({"[n]gg / [n]G", "Go to Page n"});
  result.push_back({":reload", "Reload Document"});

  return result;
}
