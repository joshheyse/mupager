#include "terminal/key_bindings.hpp"

#include <doctest/doctest.h>

#include <string_view>

TEST_CASE("KeySpec::parse single char") {
  auto ks = KeySpec::parse("j");
  REQUIRE(ks.has_value());
  CHECK(ks->id == 'j');
  CHECK(ks->label == "j");
  CHECK_FALSE(ks->is_sequence);
}

TEST_CASE("KeySpec::parse special keys") {
  SUBCASE("arrow keys") {
    auto up = KeySpec::parse("Up");
    REQUIRE(up.has_value());
    CHECK(up->id == input::ArrowUp);
    CHECK_FALSE(up->is_sequence);

    auto down = KeySpec::parse("Down");
    REQUIRE(down.has_value());
    CHECK(down->id == input::ArrowDown);

    auto left = KeySpec::parse("Left");
    REQUIRE(left.has_value());
    CHECK(left->id == input::ArrowLeft);

    auto right = KeySpec::parse("Right");
    REQUIRE(right.has_value());
    CHECK(right->id == input::ArrowRight);
  }

  SUBCASE("Tab") {
    auto tab = KeySpec::parse("Tab");
    REQUIRE(tab.has_value());
    CHECK(tab->id == 0x09);
    CHECK(tab->label == "Tab");
  }

  SUBCASE("Esc") {
    auto esc = KeySpec::parse("Esc");
    REQUIRE(esc.has_value());
    CHECK(esc->id == 27);
  }

  SUBCASE("PageUp/PageDown") {
    auto pu = KeySpec::parse("PageUp");
    REQUIRE(pu.has_value());
    CHECK(pu->id == input::PageUp);

    auto pd = KeySpec::parse("PageDown");
    REQUIRE(pd.has_value());
    CHECK(pd->id == input::PageDown);
  }

  SUBCASE("Space and Enter") {
    auto sp = KeySpec::parse("Space");
    REQUIRE(sp.has_value());
    CHECK(sp->id == ' ');

    auto enter = KeySpec::parse("Enter");
    REQUIRE(enter.has_value());
    CHECK(enter->id == '\r');
  }
}

TEST_CASE("KeySpec::parse Ctrl combos") {
  auto cf = KeySpec::parse("Ctrl+F");
  REQUIRE(cf.has_value());
  CHECK(cf->id == 6); // 'F' - 'A' + 1
  CHECK(cf->label == "Ctrl+F");
  CHECK_FALSE(cf->is_sequence);

  auto cb = KeySpec::parse("Ctrl+B");
  REQUIRE(cb.has_value());
  CHECK(cb->id == 2); // 'B' - 'A' + 1
}

TEST_CASE("KeySpec::parse double-key sequence") {
  auto gg = KeySpec::parse("gg");
  REQUIRE(gg.has_value());
  CHECK(gg->id == 'g');
  CHECK(gg->label == "gg");
  CHECK(gg->is_sequence);
}

TEST_CASE("KeySpec::parse invalid") {
  CHECK_FALSE(KeySpec::parse("").has_value());
  CHECK_FALSE(KeySpec::parse("Ctrl+").has_value());
  CHECK_FALSE(KeySpec::parse("Ctrl+FF").has_value());
  CHECK_FALSE(KeySpec::parse("Unknown").has_value());
  CHECK_FALSE(KeySpec::parse("ab").has_value()); // different chars, not a sequence
}

TEST_CASE("entry_from_name round-trip") {
  auto count = KeyBindings::entry_count();
  REQUIRE(count > 0);
  for (size_t i = 0; i < count; ++i) {
    // Can't iterate entries directly, but we can verify named actions resolve
  }
  // Verify specific known actions
  auto* scroll = KeyBindings::entry_from_name(action::ScrollDown::Name);
  REQUIRE(scroll != nullptr);
  CHECK(scroll->name == "scroll_down");
  CHECK(scroll->description == "Scroll Down");

  auto* quit = KeyBindings::entry_from_name(action::Quit::Name);
  REQUIRE(quit != nullptr);
  CHECK(quit->name == "quit");
}

TEST_CASE("entry_from_name invalid") {
  CHECK(KeyBindings::entry_from_name("nonexistent") == nullptr);
}

TEST_CASE("KeyBindings::defaults lookup") {
  auto kb = KeyBindings::defaults();

  CHECK(kb.lookup('j') != nullptr);
  CHECK(kb.lookup('j')->name == action::ScrollDown::Name);
  CHECK(kb.lookup('k')->name == action::ScrollUp::Name);
  CHECK(kb.lookup('q')->name == action::Quit::Name);
  CHECK(kb.lookup('?')->name == action::ShowHelp::Name);
  CHECK(kb.lookup(0x06)->name == action::PageDown::Name);       // Ctrl+F
  CHECK(kb.lookup(0x02)->name == action::PageUp::Name);         // Ctrl+B
  CHECK(kb.lookup(0x09)->name == action::ToggleViewMode::Name); // Tab
  CHECK(kb.lookup(27)->name == action::ClearSearch::Name);      // Esc

  CHECK(kb.sequence_prefix_key() == 'g');
  CHECK(kb.sequence_double_entry() != nullptr);
  CHECK(kb.sequence_double_entry()->name == action::GotoFirstPage::Name);
}

TEST_CASE("KeyBindings custom override replaces defaults") {
  auto kb = KeyBindings::defaults();

  // Replace quit binding: remove 'q', bind 'x'
  auto* quit_entry = KeyBindings::entry_from_name(action::Quit::Name);
  REQUIRE(quit_entry != nullptr);
  kb.clear(quit_entry);
  auto x_spec = KeySpec::parse("x");
  REQUIRE(x_spec.has_value());
  kb.bind(quit_entry, *x_spec);

  CHECK(kb.lookup('x') != nullptr);
  CHECK(kb.lookup('x')->name == action::Quit::Name);
  CHECK(kb.lookup('q') == nullptr);
}

TEST_CASE("KeyBindings::help_bindings reflects overrides") {
  auto kb = KeyBindings::defaults();
  auto* quit_entry = KeyBindings::entry_from_name(action::Quit::Name);
  REQUIRE(quit_entry != nullptr);
  kb.clear(quit_entry);
  auto x_spec = KeySpec::parse("x");
  REQUIRE(x_spec.has_value());
  kb.bind(quit_entry, *x_spec);

  auto help = kb.help_bindings();
  bool found_quit = false;
  for (const auto& hb : help) {
    if (hb.description == "Quit") {
      CHECK(hb.key_label == "x");
      found_quit = true;
    }
  }
  CHECK(found_quit);
}

TEST_CASE("KeyBindings::defaults help_bindings includes bound actions") {
  auto kb = KeyBindings::defaults();
  auto help = kb.help_bindings();

  // 30 actions with default key bindings + 2 non-configurable entries.
  // Visual motion actions have no default terminal keys (mode-specific dispatch).
  CHECK(help.size() == 32);
}

TEST_CASE("ActionEntry::make returns correct action types") {
  auto* quit_entry = KeyBindings::entry_from_name(action::Quit::Name);
  REQUIRE(quit_entry != nullptr);
  CHECK(std::holds_alternative<action::Quit>(quit_entry->make()));

  auto* scroll_entry = KeyBindings::entry_from_name(action::ScrollDown::Name);
  REQUIRE(scroll_entry != nullptr);
  CHECK(std::holds_alternative<action::ScrollDown>(scroll_entry->make()));

  auto* first_page_entry = KeyBindings::entry_from_name(action::GotoFirstPage::Name);
  REQUIRE(first_page_entry != nullptr);
  CHECK(std::holds_alternative<action::GotoFirstPage>(first_page_entry->make()));
}
