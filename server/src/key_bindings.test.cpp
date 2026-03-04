#include "key_bindings.h"

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
    CHECK(up->id == input::ARROW_UP);
    CHECK_FALSE(up->is_sequence);

    auto down = parse_key_spec("Down");
    REQUIRE(down.has_value());
    CHECK(down->id == input::ARROW_DOWN);

    auto left = parse_key_spec("Left");
    REQUIRE(left.has_value());
    CHECK(left->id == input::ARROW_LEFT);

    auto right = parse_key_spec("Right");
    REQUIRE(right.has_value());
    CHECK(right->id == input::ARROW_RIGHT);
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
    CHECK(pu->id == input::PAGE_UP);

    auto pd = parse_key_spec("PageDown");
    REQUIRE(pd.has_value());
    CHECK(pd->id == input::PAGE_DOWN);
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
  for (size_t i = 0; i < ACTION_TABLE_SIZE; ++i) {
    auto action = action_from_name(ACTION_TABLE[i].name);
    REQUIRE(action.has_value());
    CHECK(*action == ACTION_TABLE[i].action);
    CHECK(std::string(action_description(*action)) == ACTION_TABLE[i].description);
  }
}

TEST_CASE("action_from_name invalid") {
  CHECK_FALSE(action_from_name("nonexistent").has_value());
}

TEST_CASE("KeyBindings::defaults lookup") {
  auto kb = KeyBindings::defaults();

  CHECK(kb.lookup('j') == Action::SCROLL_DOWN);
  CHECK(kb.lookup('k') == Action::SCROLL_UP);
  CHECK(kb.lookup('q') == Action::QUIT);
  CHECK(kb.lookup('?') == Action::HELP);
  CHECK(kb.lookup(0x06) == Action::PAGE_DOWN);   // Ctrl+F
  CHECK(kb.lookup(0x02) == Action::PAGE_UP);     // Ctrl+B
  CHECK(kb.lookup(0x09) == Action::TOGGLE_VIEW); // Tab
  CHECK(kb.lookup(27) == Action::CLEAR_SEARCH);  // Esc

  CHECK(kb.sequence_prefix_key() == 'g');
  CHECK(kb.sequence_double_action() == Action::FIRST_PAGE);
}

TEST_CASE("KeyBindings custom override replaces defaults") {
  auto kb = KeyBindings::defaults();

  // Replace quit binding: remove 'q', bind 'x'
  kb.clear(Action::QUIT);
  auto x_spec = parse_key_spec("x");
  REQUIRE(x_spec.has_value());
  kb.bind(Action::QUIT, *x_spec);

  CHECK(kb.lookup('x') == Action::QUIT);
  CHECK_FALSE(kb.lookup('q').has_value());
}

TEST_CASE("KeyBindings::help_bindings reflects overrides") {
  auto kb = KeyBindings::defaults();
  kb.clear(Action::QUIT);
  auto x_spec = parse_key_spec("x");
  REQUIRE(x_spec.has_value());
  kb.bind(Action::QUIT, *x_spec);

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

TEST_CASE("KeyBindings::defaults help_bindings includes all actions") {
  auto kb = KeyBindings::defaults();
  auto help = kb.help_bindings();

  // Should have entries for all 27 actions plus 2 non-configurable
  CHECK(help.size() == ACTION_TABLE_SIZE + 2);
}

TEST_CASE("action_to_command returns correct command types") {
  auto cmd = action_to_command(Action::QUIT);
  CHECK(std::holds_alternative<cmd::Quit>(cmd));

  cmd = action_to_command(Action::SCROLL_DOWN);
  CHECK(std::holds_alternative<cmd::ScrollDown>(cmd));

  cmd = action_to_command(Action::FIRST_PAGE);
  CHECK(std::holds_alternative<cmd::GotoFirstPage>(cmd));
}
