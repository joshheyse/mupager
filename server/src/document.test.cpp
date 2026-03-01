#include "document.h"

#include <doctest/doctest.h>

#include <stdexcept>

static constexpr const char* FIXTURE_PDF = PROJECT_TEST_DIR "/fixtures/test.pdf";

TEST_CASE("Document opens a valid PDF") {
  Document doc(FIXTURE_PDF);
  CHECK(doc.page_count() > 98);
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
