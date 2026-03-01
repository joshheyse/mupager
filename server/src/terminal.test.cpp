#include "terminal.h"

#include <doctest/doctest.h>

#include <stdexcept>

TEST_CASE("cell_size returns positive dimensions") {
  try {
    auto cs = terminal::cell_size();
    CHECK(cs.width_px > 0);
    CHECK(cs.height_px > 0);
  } catch (const std::runtime_error&) {
    // ioctl may fail in CI or non-graphical environments; that's OK
    MESSAGE("cell_size() threw — likely no terminal attached");
  }
}
