#include "document.h"

#include <doctest/doctest.h>

#include <stdexcept>

static constexpr const char* FIXTURE_PDF = PROJECT_TEST_DIR "/fixtures/test.pdf";

TEST_CASE("Document opens a valid PDF") {
  Document doc(FIXTURE_PDF);
  CHECK(doc.page_count() == 98);
}

TEST_CASE("Document exposes raw handles") {
  Document doc(FIXTURE_PDF);
  CHECK(doc.ctx() != nullptr);
  CHECK(doc.raw() != nullptr);
}

TEST_CASE("Document throws on non-existent file") {
  CHECK_THROWS_AS(Document("/nonexistent/path.pdf"), std::runtime_error);
}

TEST_CASE("Document throws on non-document file") {
  CHECK_THROWS_AS(Document("/dev/null"), std::runtime_error);
}

TEST_CASE("Document outline is non-empty for test PDF") {
  Document doc(FIXTURE_PDF);
  auto outline = doc.load_outline();
  CHECK(!outline.empty());
}

TEST_CASE("Outline entries have valid fields") {
  Document doc(FIXTURE_PDF);
  auto outline = doc.load_outline();
  int page_count = doc.page_count();
  for (const auto& entry : outline) {
    CHECK(!entry.title.empty());
    CHECK(entry.page >= 0);
    CHECK(entry.page < page_count);
    CHECK(entry.level >= 0);
  }
}

TEST_CASE("First outline entry title contains RFC") {
  Document doc(FIXTURE_PDF);
  auto outline = doc.load_outline();
  REQUIRE(!outline.empty());
  CHECK(outline[0].title.find("RFC") != std::string::npos);
}
