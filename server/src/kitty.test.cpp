#include "kitty.h"

#include "document.h"
#include "pixmap.h"

#include <doctest/doctest.h>

#include <string>

static constexpr const char* FIXTURE_PDF = PROJECT_TEST_DIR "/fixtures/test.pdf";

TEST_CASE("kitty encode starts with ESC_G and ends with ST") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("kitty encode contains correct format and dimensions") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("f=24") != std::string::npos);
  CHECK(out.find("s=" + std::to_string(pix.width())) != std::string::npos);
  CHECK(out.find("v=" + std::to_string(pix.height())) != std::string::npos);
}

TEST_CASE("kitty encode includes image_id when non-zero") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  std::string with_id = kitty::encode(pix, 42);
  CHECK(with_id.find("i=42") != std::string::npos);

  std::string without_id = kitty::encode(pix, 0);
  CHECK(without_id.find(",i=") == std::string::npos);
}

TEST_CASE("kitty encode last chunk has m=0") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("m=0") != std::string::npos);
}

TEST_CASE("kitty encode large image produces multiple chunks") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 2.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("m=1") != std::string::npos);
  CHECK(out.find("m=0") != std::string::npos);
}

TEST_CASE("kitty delete_image") {
  CHECK(kitty::delete_image(7) == "\x1b_Ga=d,d=i,i=7\x1b\\");
}

TEST_CASE("kitty wrap_tmux doubles ESC bytes and adds DCS passthrough") {
  std::string apc = "\x1b_Ga=T,f=24;AAAA\x1b\\";
  std::string wrapped = kitty::wrap_tmux(apc);

  CHECK(wrapped.find("\x1bPtmux;") != std::string::npos);
  CHECK(wrapped.find("\x1b\x1b") != std::string::npos);
  // Should end with ST
  CHECK(wrapped.substr(wrapped.size() - 2) == "\x1b\\");
}

TEST_CASE("kitty wrap_tmux wraps multiple APC sequences individually") {
  std::string two_apcs = "\x1b_Gm=1;AAAA\x1b\\\x1b_Gm=0;BBBB\x1b\\";
  std::string wrapped = kitty::wrap_tmux(two_apcs);

  // Should contain two DCS passthrough blocks
  size_t first = wrapped.find("\x1bPtmux;");
  REQUIRE(first != std::string::npos);
  size_t second = wrapped.find("\x1bPtmux;", first + 1);
  CHECK(second != std::string::npos);
}

TEST_CASE("kitty encode_tmux contains DCS passthrough") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode_tmux(pix, 1, 8, 16);

  CHECK(out.find("\x1bPtmux;") != std::string::npos);
}

TEST_CASE("kitty encode_tmux uses a=T with U=1 and q=2") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode_tmux(pix, 1, 8, 16);

  CHECK(out.find("a=T") != std::string::npos);
  CHECK(out.find("U=1") != std::string::npos);
  CHECK(out.find("q=2") != std::string::npos);
}

TEST_CASE("kitty encode_tmux contains Unicode placeholder U+10EEEE") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode_tmux(pix, 1, 8, 16);

  // U+10EEEE in UTF-8: F4 8E BB AE
  std::string placeholder = "\xF4\x8E\xBB\xAE";
  CHECK(out.find(placeholder) != std::string::npos);
}

TEST_CASE("kitty encode_tmux contains fg color SGR for image_id") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  // image_id=1: colon-separated, MSB first: (id>>16)=0, (id>>8)=0, id&0xFF=1
  std::string out = kitty::encode_tmux(pix, 1, 8, 16);
  CHECK(out.find("\x1b[38:2:0:0:1m") != std::string::npos);
  CHECK(out.find("\x1b[39m") != std::string::npos);

  // image_id=0x010203: (id>>16)=1, (id>>8)=2, id&0xFF=3
  std::string out2 = kitty::encode_tmux(pix, 0x010203, 8, 16);
  CHECK(out2.find("\x1b[38:2:1:2:3m") != std::string::npos);
}

TEST_CASE("kitty delete_image_tmux wraps in DCS passthrough") {
  std::string out = kitty::delete_image_tmux(7);
  CHECK(out.find("\x1bPtmux;") != std::string::npos);
  CHECK(out.find("a=d") != std::string::npos);
  CHECK(out.find("i=7") != std::string::npos);
}
