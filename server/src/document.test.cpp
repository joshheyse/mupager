#include "document.h"

#include <doctest/doctest.h>

#include <stdexcept>

static constexpr const char* FixturePdf = PROJECT_FIXTURE_DIR "/test.pdf";

TEST_CASE("Document opens a valid PDF") {
  Document doc(FixturePdf);
  CHECK(doc.page_count() == 98);
}

TEST_CASE("Document exposes raw handles") {
  Document doc(FixturePdf);
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
  Document doc(FixturePdf);
  auto outline = doc.load_outline();
  CHECK(!outline.empty());
}

TEST_CASE("Outline entries have valid fields") {
  Document doc(FixturePdf);
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
  Document doc(FixturePdf);
  auto outline = doc.load_outline();
  REQUIRE(!outline.empty());
  CHECK(outline[0].title.find("RFC") != std::string::npos);
}

TEST_CASE("Document search finds known text") {
  Document doc(FixturePdf);
  auto results = doc.search("RFC");
  CHECK(!results.empty());
  for (const auto& hit : results) {
    CHECK(hit.page >= 0);
    CHECK(hit.page < doc.page_count());
    CHECK(hit.rect.width > 0);
    CHECK(hit.rect.height > 0);
  }
}

TEST_CASE("Document search returns empty for nonsense string") {
  Document doc(FixturePdf);
  auto results = doc.search("zzzXXXnonexistent999");
  CHECK(results.empty());
}

TEST_CASE("Document search hits have valid bounding boxes") {
  Document doc(FixturePdf);
  auto results = doc.search("the");
  REQUIRE(!results.empty());
  for (const auto& hit : results) {
    CHECK(hit.rect.x >= 0);
    CHECK(hit.rect.y >= 0);
    CHECK(hit.rect.width > 0);
    CHECK(hit.rect.height > 0);
  }
}

TEST_CASE("Document reload preserves page count") {
  Document doc(FixturePdf);
  int original_count = doc.page_count();
  doc.reload(FixturePdf);
  CHECK(doc.page_count() == original_count);
}

TEST_CASE("Document reload throws on invalid path") {
  Document doc(FixturePdf);
  CHECK_THROWS_AS(doc.reload("/nonexistent/path.pdf"), std::runtime_error);
  CHECK(doc.page_count() == 98);
}

TEST_CASE("Document load_links returns valid results") {
  Document doc(FixturePdf);
  auto links = doc.load_links(0);
  for (const auto& link : links) {
    CHECK(link.page == 0);
    CHECK(link.rect.width > 0);
    CHECK(link.rect.height > 0);
    CHECK(!link.uri.empty());
  }
}

TEST_CASE("Document load_links internal links have valid dest_page") {
  Document doc(FixturePdf);
  int page_count = doc.page_count();
  for (int p = 0; p < std::min(page_count, 10); ++p) {
    auto links = doc.load_links(p);
    for (const auto& link : links) {
      if (link.dest_page >= 0) {
        CHECK(link.dest_page < page_count);
      }
    }
  }
}
