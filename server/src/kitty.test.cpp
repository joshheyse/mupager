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
