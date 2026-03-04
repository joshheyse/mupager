#include "terminal/key_bindings.h"

#include <doctest/doctest.h>

TEST_CASE("parse_key_spec single char") {
  auto ks = parse_key_spec("j");
  REQUIRE(ks.has_value());
  CHECK(ks->id == 'j');
  CHECK(ks->label == "j");
  CHECK_FALSE(ks->is_sequence);
}

TEST_CASE("parse_key_spec special keys") {
  SUBCASE("arrow keys") {
    auto up = parse_key_spec("Up");
    REQUIRE(up.has_value());
    CHECK(up->id == input::ArrowUp);
    CHECK_FALSE(up->is_sequence);

    auto down = parse_key_spec("Down");
    REQUIRE(down.has_value());
    CHECK(down->id == input::ArrowDown);

    auto left = parse_key_spec("Left");
    REQUIRE(left.has_value());
    CHECK(left->id == input::ArrowLeft);

    auto right = parse_key_spec("Right");
    REQUIRE(right.has_value());
    CHECK(right->id == input::ArrowRight);
  }

  SUBCASE("Tab") {
    auto tab = parse_key_spec("Tab");
    REQUIRE(tab.has_value());
    CHECK(tab->id == 0x09);
    CHECK(tab->label == "Tab");
  }

  SUBCASE("Esc") {
    auto esc = parse_key_spec("Esc");
    REQUIRE(esc.has_value());
    CHECK(esc->id == 27);
  }

  SUBCASE("PageUp/PageDown") {
    auto pu = parse_key_spec("PageUp");
    REQUIRE(pu.has_value());
    CHECK(pu->id == input::PageUp);

    auto pd = parse_key_spec("PageDown");
    REQUIRE(pd.has_value());
    CHECK(pd->id == input::PageDown);
  }

  SUBCASE("Space and Enter") {
    auto sp = parse_key_spec("Space");
    REQUIRE(sp.has_value());
    CHECK(sp->id == ' ');

    auto enter = parse_key_spec("Enter");
    REQUIRE(enter.has_value());
    CHECK(enter->id == '\r');
  }
}

TEST_CASE("parse_key_spec Ctrl combos") {
  auto cf = parse_key_spec("Ctrl+F");
  REQUIRE(cf.has_value());
  CHECK(cf->id == 6); // 'F' - 'A' + 1
  CHECK(cf->label == "Ctrl+F");
  CHECK_FALSE(cf->is_sequence);

  auto cb = parse_key_spec("Ctrl+B");
  REQUIRE(cb.has_value());
  CHECK(cb->id == 2); // 'B' - 'A' + 1
}

TEST_CASE("parse_key_spec double-key sequence") {
  auto gg = parse_key_spec("gg");
  REQUIRE(gg.has_value());
  CHECK(gg->id == 'g');
  CHECK(gg->label == "gg");
  CHECK(gg->is_sequence);
}

TEST_CASE("parse_key_spec invalid") {
  CHECK_FALSE(parse_key_spec("").has_value());
  CHECK_FALSE(parse_key_spec("Ctrl+").has_value());
  CHECK_FALSE(parse_key_spec("Ctrl+FF").has_value());
  CHECK_FALSE(parse_key_spec("Unknown").has_value());
  CHECK_FALSE(parse_key_spec("ab").has_value()); // different chars, not a sequence
}

TEST_CASE("action_from_name round-trip") {
  for (size_t i = 0; i < ActionTableSize; ++i) {
    auto* info = action_from_name(ActionTable[i].name);
    REQUIRE(info != nullptr);
    CHECK(info == &ActionTable[i]);
    CHECK(std::string(info->description) == ActionTable[i].description);
  }
}

TEST_CASE("action_from_name invalid") {
  CHECK(action_from_name("nonexistent") == nullptr);
}

TEST_CASE("KeyBindings::defaults lookup") {
  auto kb = KeyBindings::defaults();

  CHECK(kb.lookup('j') != nullptr);
  CHECK(kb.lookup('j')->name == cmd::ScrollDown::Action);
  CHECK(kb.lookup('k')->name == cmd::ScrollUp::Action);
  CHECK(kb.lookup('q')->name == cmd::Quit::Action);
  CHECK(kb.lookup('?')->name == cmd::ShowHelp::Action);
  CHECK(kb.lookup(0x06)->name == cmd::PageDown::Action);       // Ctrl+F
  CHECK(kb.lookup(0x02)->name == cmd::PageUp::Action);         // Ctrl+B
  CHECK(kb.lookup(0x09)->name == cmd::ToggleViewMode::Action); // Tab
  CHECK(kb.lookup(27)->name == cmd::ClearSearch::Action);      // Esc

  CHECK(kb.sequence_prefix_key() == 'g');
  CHECK(kb.sequence_double_info() != nullptr);
  CHECK(kb.sequence_double_info()->name == cmd::GotoFirstPage::Action);
}

TEST_CASE("KeyBindings custom override replaces defaults") {
  auto kb = KeyBindings::defaults();

  // Replace quit binding: remove 'q', bind 'x'
  auto* quit_info = action_from_name(cmd::Quit::Action);
  REQUIRE(quit_info != nullptr);
  kb.clear(quit_info);
  auto x_spec = parse_key_spec("x");
  REQUIRE(x_spec.has_value());
  kb.bind(quit_info, *x_spec);

  CHECK(kb.lookup('x') != nullptr);
  CHECK(kb.lookup('x')->name == cmd::Quit::Action);
  CHECK(kb.lookup('q') == nullptr);
}

TEST_CASE("KeyBindings::help_bindings reflects overrides") {
  auto kb = KeyBindings::defaults();
  auto* quit_info = action_from_name(cmd::Quit::Action);
  REQUIRE(quit_info != nullptr);
  kb.clear(quit_info);
  auto x_spec = parse_key_spec("x");
  REQUIRE(x_spec.has_value());
  kb.bind(quit_info, *x_spec);

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

TEST_CASE("ActionInfo::make returns correct command types") {
  auto* quit_info = action_from_name(cmd::Quit::Action);
  REQUIRE(quit_info != nullptr);
  CHECK(std::holds_alternative<cmd::Quit>(quit_info->make()));

  auto* scroll_info = action_from_name(cmd::ScrollDown::Action);
  REQUIRE(scroll_info != nullptr);
  CHECK(std::holds_alternative<cmd::ScrollDown>(scroll_info->make()));

  auto* first_page_info = action_from_name(cmd::GotoFirstPage::Action);
  REQUIRE(first_page_info != nullptr);
  CHECK(std::holds_alternative<cmd::GotoFirstPage>(first_page_info->make()));
}
