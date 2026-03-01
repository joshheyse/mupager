#include "base64.h"

#include <doctest/doctest.h>

TEST_CASE("base64 empty input") {
  CHECK(base64::encode("") == "");
}

TEST_CASE("base64 RFC 4648 test vectors") {
  CHECK(base64::encode("f") == "Zg==");
  CHECK(base64::encode("fo") == "Zm8=");
  CHECK(base64::encode("foo") == "Zm9v");
  CHECK(base64::encode("foob") == "Zm9vYg==");
  CHECK(base64::encode("fooba") == "Zm9vYmE=");
  CHECK(base64::encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64 output length is 4*ceil(n/3)") {
  for (size_t n = 0; n <= 64; ++n) {
    std::string input(n, 'x');
    size_t expected_len = 4 * ((n + 2) / 3);
    CHECK(base64::encode(input).size() == expected_len);
  }
}
