#include "converter.hpp"

#include <doctest/doctest.h>

TEST_CASE("find_converter returns nullopt when no match") {
  std::map<std::string, std::string> converters = {
      {"*.md", "pandoc %i -o %o"},
      {"*.tex", "latexmk -pdf %i"},
  };
  CHECK(find_converter("/path/to/file.pdf", converters, "") == std::nullopt);
}

TEST_CASE("find_converter matches glob patterns") {
  std::map<std::string, std::string> converters = {
      {"*.md", "pandoc %i -o %o"},
      {"*.tex", "latexmk -pdf %i"},
  };
  CHECK(find_converter("/path/to/doc.md", converters, "") == "pandoc %i -o %o");
  CHECK(find_converter("/path/to/paper.tex", converters, "") == "latexmk -pdf %i");
}

TEST_CASE("find_converter CLI override takes precedence") {
  std::map<std::string, std::string> converters = {{"*.md", "pandoc %i -o %o"}};
  CHECK(find_converter("/path/doc.md", converters, "custom %i %o") == "custom %i %o");
}

TEST_CASE("find_converter CLI override works with no converters") {
  std::map<std::string, std::string> converters;
  CHECK(find_converter("/path/doc.md", converters, "custom %i %o") == "custom %i %o");
}

TEST_CASE("find_converter matches only filename not full path") {
  std::map<std::string, std::string> converters = {{"*.md", "pandoc %i -o %o"}};
  CHECK(find_converter("/some/path.md/notamarkdownfile.txt", converters, "") == std::nullopt);
  CHECK(find_converter("/some/path/file.md", converters, "") == "pandoc %i -o %o");
}
