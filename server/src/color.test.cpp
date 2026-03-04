#include "color.h"

#include <doctest/doctest.h>

TEST_CASE("Color::parse hex") {
  auto c = Color::parse("#1a2b3c");
  REQUIRE(c.has_value());
  CHECK(c->r == 0x1a);
  CHECK(c->g == 0x2b);
  CHECK(c->b == 0x3c);
  CHECK_FALSE(c->is_default);
}

TEST_CASE("Color::parse uppercase hex") {
  auto c = Color::parse("#FF00AA");
  REQUIRE(c.has_value());
  CHECK(c->r == 255);
  CHECK(c->g == 0);
  CHECK(c->b == 170);
}

TEST_CASE("Color::parse default") {
  auto c = Color::parse("default");
  REQUIRE(c.has_value());
  CHECK(c->is_default);
}

TEST_CASE("Color::parse integer as 256-color index") {
  auto c = Color::parse("234");
  REQUIRE(c.has_value());
  CHECK(c->is_indexed);
  CHECK(c->index == 234);
  CHECK_FALSE(c->is_default);
}

TEST_CASE("Color::parse integer zero") {
  auto c = Color::parse("0");
  REQUIRE(c.has_value());
  CHECK(c->is_indexed);
  CHECK(c->index == 0);
}

TEST_CASE("Color::parse integer 255") {
  auto c = Color::parse("255");
  REQUIRE(c.has_value());
  CHECK(c->is_indexed);
  CHECK(c->index == 255);
}

TEST_CASE("Color::parse invalid strings") {
  CHECK_FALSE(Color::parse("").has_value());
  CHECK_FALSE(Color::parse("#12345").has_value());
  CHECK_FALSE(Color::parse("#1234567").has_value());
  CHECK_FALSE(Color::parse("red").has_value());
  CHECK_FALSE(Color::parse("#GGHHII").has_value());
  CHECK_FALSE(Color::parse("256").has_value());
  CHECK_FALSE(Color::parse("999").has_value());
}

TEST_CASE("Color::sgr_fg concrete") {
  auto c = Color::rgb(10, 20, 30);
  CHECK(c.sgr_fg() == "\x1b[38;2;10;20;30m");
}

TEST_CASE("Color::sgr_bg concrete") {
  auto c = Color::rgb(255, 128, 0);
  CHECK(c.sgr_bg() == "\x1b[48;2;255;128;0m");
}

TEST_CASE("Color::sgr_fg default") {
  auto c = Color::terminal_default();
  CHECK(c.sgr_fg() == "\x1b[39m");
}

TEST_CASE("Color::sgr_bg default") {
  auto c = Color::terminal_default();
  CHECK(c.sgr_bg() == "\x1b[49m");
}

TEST_CASE("Color::luminance black") {
  auto c = Color::rgb(0, 0, 0);
  CHECK(c.luminance() == doctest::Approx(0.0f));
}

TEST_CASE("Color::luminance white") {
  auto c = Color::rgb(255, 255, 255);
  CHECK(c.luminance() == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("Color::luminance green is brightest") {
  auto r = Color::rgb(255, 0, 0).luminance();
  auto g = Color::rgb(0, 255, 0).luminance();
  auto b = Color::rgb(0, 0, 255).luminance();
  CHECK(g > r);
  CHECK(g > b);
}

TEST_CASE("Color::indexed factory") {
  auto c = Color::indexed(111);
  CHECK(c.is_indexed);
  CHECK(c.index == 111);
  CHECK_FALSE(c.is_default);
}

TEST_CASE("Color::sgr_fg indexed") {
  auto c = Color::indexed(234);
  CHECK(c.sgr_fg() == "\x1b[38;5;234m");
}

TEST_CASE("Color::sgr_bg indexed") {
  auto c = Color::indexed(180);
  CHECK(c.sgr_bg() == "\x1b[48;5;180m");
}

TEST_CASE("Color equality") {
  CHECK(Color::rgb(1, 2, 3) == Color::rgb(1, 2, 3));
  CHECK(Color::rgb(1, 2, 3) != Color::rgb(1, 2, 4));
  CHECK(Color::terminal_default() == Color::terminal_default());
  CHECK(Color::terminal_default() != Color::rgb(0, 0, 0));
  CHECK(Color::indexed(111) == Color::indexed(111));
  CHECK(Color::indexed(111) != Color::indexed(112));
  CHECK(Color::indexed(0) != Color::rgb(0, 0, 0));
}
