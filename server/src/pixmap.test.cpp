#include "pixmap.h"

#include "document.h"

#include <doctest/doctest.h>

#include <stdexcept>

static constexpr const char* FIXTURE_PDF = PROJECT_TEST_DIR "/fixtures/test.pdf";

TEST_CASE("render_page produces valid pixmap") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  CHECK(pix.width() > 0);
  CHECK(pix.height() > 0);
  CHECK(pix.samples() != nullptr);
}

TEST_CASE("render_page at zoom 2.0 roughly doubles dimensions") {
  Document doc(FIXTURE_PDF);
  Pixmap pix1 = doc.render_page(0, 1.0f);
  Pixmap pix2 = doc.render_page(0, 2.0f);

  CHECK(pix2.width() == doctest::Approx(pix1.width() * 2).epsilon(0.01));
  CHECK(pix2.height() == doctest::Approx(pix1.height() * 2).epsilon(0.01));
}

TEST_CASE("render_page produces RGB pixmap") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  CHECK(pix.components() == 3);
}

TEST_CASE("pixmap stride >= width * components") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  CHECK(pix.stride() >= pix.width() * pix.components());
}

TEST_CASE("pixmap is move-constructible") {
  Document doc(FIXTURE_PDF);
  Pixmap pix1 = doc.render_page(0, 1.0f);
  int w = pix1.width();
  int h = pix1.height();

  Pixmap pix2(std::move(pix1));
  CHECK(pix2.width() == w);
  CHECK(pix2.height() == h);
  CHECK(pix2.samples() != nullptr);
}

TEST_CASE("render_page throws on out-of-range page") {
  Document doc(FIXTURE_PDF);
  CHECK_THROWS_AS(doc.render_page(-1, 1.0f), std::runtime_error);
  CHECK_THROWS_AS(doc.render_page(9999, 1.0f), std::runtime_error);
}
