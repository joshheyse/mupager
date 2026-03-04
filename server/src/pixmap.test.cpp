#include "pixmap.h"

#include "document.h"

#include <doctest/doctest.h>

#include <stdexcept>

static constexpr const char* FIXTURE_PDF = PROJECT_FIXTURE_DIR "/test.pdf";

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

TEST_CASE("pack_pixels produces tightly packed buffer") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  auto packed = pix.pack_pixels();

  size_t expected = static_cast<size_t>(pix.width()) * pix.height() * pix.components();
  CHECK(packed.size() == expected);
}

TEST_CASE("png_data returns valid PNG") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);
  auto data = pix.png_data();

  REQUIRE(data.size() >= 8);
  CHECK(data[0] == 0x89);
  CHECK(data[1] == 'P');
  CHECK(data[2] == 'N');
  CHECK(data[3] == 'G');
}

TEST_CASE("render_page throws on out-of-range page") {
  Document doc(FIXTURE_PDF);
  CHECK_THROWS_AS(doc.render_page(-1, 1.0f), std::runtime_error);
  CHECK_THROWS_AS(doc.render_page(9999, 1.0f), std::runtime_error);
}

TEST_CASE("highlight_rect blends color onto pixmap") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  // Record original pixel value
  unsigned char orig_r = pix.samples()[0];
  unsigned char orig_g = pix.samples()[1];
  unsigned char orig_b = pix.samples()[2];

  // Apply a yellow highlight with alpha=128
  pix.highlight_rect(0, 0, 10, 10, 255, 255, 0, 128);

  unsigned char new_r = pix.samples()[0];
  unsigned char new_g = pix.samples()[1];
  unsigned char new_b = pix.samples()[2];

  // The pixel should have moved toward yellow (255,255,0)
  CHECK(new_r >= orig_r); // Red channel should increase or stay (toward 255)
  CHECK(new_b <= orig_b); // Blue channel should decrease or stay (toward 0)
  // Verify it actually changed (unless it was already yellow)
  if (orig_r != 255 || orig_g != 255 || orig_b != 0) {
    CHECK((new_r != orig_r || new_g != orig_g || new_b != orig_b));
  }
}

TEST_CASE("highlight_rect clamps to pixmap bounds") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  // Should not crash with out-of-bounds rectangle
  pix.highlight_rect(-10, -10, 20, 20, 255, 0, 0, 128);
  pix.highlight_rect(pix.width() - 5, pix.height() - 5, 100, 100, 0, 255, 0, 128);
  CHECK(true); // If we get here without crashing, the test passes
}

TEST_CASE("highlight_rect with zero alpha is no-op") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  unsigned char orig_r = pix.samples()[0];
  unsigned char orig_g = pix.samples()[1];
  unsigned char orig_b = pix.samples()[2];

  pix.highlight_rect(0, 0, 10, 10, 255, 0, 0, 0);

  CHECK(pix.samples()[0] == orig_r);
  CHECK(pix.samples()[1] == orig_g);
  CHECK(pix.samples()[2] == orig_b);
}

TEST_CASE("recolor with fg=white bg=black approximates invert") {
  Document doc(FIXTURE_PDF);
  Pixmap pix1 = doc.render_page(0, 1.0f);
  Pixmap pix2 = doc.render_page(0, 1.0f);

  pix1.invert();
  pix2.recolor(255, 255, 255, 0, 0, 0);

  // Recolor uses (R+G+B)/3 luminance, invert uses per-channel 255-v.
  // For grayscale content they should be very close.
  // For the first pixel, check they're within a small tolerance.
  int diff_r = std::abs(static_cast<int>(pix1.samples()[0]) - static_cast<int>(pix2.samples()[0]));
  int diff_g = std::abs(static_cast<int>(pix1.samples()[1]) - static_cast<int>(pix2.samples()[1]));
  int diff_b = std::abs(static_cast<int>(pix1.samples()[2]) - static_cast<int>(pix2.samples()[2]));
  // For pure white (255,255,255) both should give (0,0,0) — exact match
  // For colored pixels, tolerance of ~2 per channel
  CHECK(diff_r <= 2);
  CHECK(diff_g <= 2);
  CHECK(diff_b <= 2);
}

TEST_CASE("recolor with fg=black bg=white is identity for white pixels") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  // Record original white-ish pixel (background of the test PDF is white)
  unsigned char orig_r = pix.samples()[0];
  unsigned char orig_g = pix.samples()[1];
  unsigned char orig_b = pix.samples()[2];

  pix.recolor(0, 0, 0, 255, 255, 255);

  // For a pure white pixel (255,255,255): lum=255, result = fg*0/255 + bg*255/255 = bg = (255,255,255)
  // Should be very close to original
  int diff_r = std::abs(static_cast<int>(pix.samples()[0]) - static_cast<int>(orig_r));
  int diff_g = std::abs(static_cast<int>(pix.samples()[1]) - static_cast<int>(orig_g));
  int diff_b = std::abs(static_cast<int>(pix.samples()[2]) - static_cast<int>(orig_b));
  CHECK(diff_r <= 1);
  CHECK(diff_g <= 1);
  CHECK(diff_b <= 1);
}

TEST_CASE("recolor maps to expected range") {
  Document doc(FIXTURE_PDF);
  Pixmap pix = doc.render_page(0, 1.0f);

  // Recolor with specific fg/bg
  pix.recolor(0xc0, 0xca, 0xf5, 0x1a, 0x1b, 0x26);

  // All output pixels should be within the range [min(fg,bg), max(fg,bg)] for each channel
  int w = pix.width();
  int h = pix.height();
  int comp = pix.components();
  int s = pix.stride();
  const unsigned char* data = pix.samples();
  for (int y = 0; y < std::min(h, 5); ++y) {
    const unsigned char* row = data + y * s;
    for (int x = 0; x < std::min(w, 10) * comp; x += comp) {
      CHECK(row[x] >= 0x1a);
      CHECK(row[x] <= 0xc0);
      CHECK(row[x + 1] >= 0x1b);
      CHECK(row[x + 1] <= 0xca);
      CHECK(row[x + 2] >= 0x26);
      CHECK(row[x + 2] <= 0xf5);
    }
  }
}
