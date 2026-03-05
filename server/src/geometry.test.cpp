#include "geometry.hpp"

#include <doctest/doctest.h>

TEST_SUITE("Size") {
  TEST_CASE("area and is_positive") {
    PixelSize s{10, 20};
    CHECK(s.area() == 200);
    CHECK(s.is_positive());

    PixelSize zero{0, 5};
    CHECK(zero.area() == 0);
    CHECK_FALSE(zero.is_positive());
  }

  TEST_CASE("equality") {
    CHECK(PixelSize{3, 4} == PixelSize{3, 4});
    CHECK(PixelSize{3, 4} != PixelSize{4, 3});
  }
}

TEST_SUITE("Point") {
  TEST_CASE("equality") {
    CHECK(PixelPoint{1, 2} == PixelPoint{1, 2});
    CHECK(PixelPoint{1, 2} != PixelPoint{2, 1});
  }

  TEST_CASE("default constructed") {
    PixelPoint p;
    CHECK(p.x == 0);
    CHECK(p.y == 0);
  }
}

TEST_SUITE("Rect") {
  TEST_CASE("accessors") {
    PixelRect r{10, 20, 100, 200};
    CHECK(r.left() == 10);
    CHECK(r.top() == 20);
    CHECK(r.right() == 110);
    CHECK(r.bottom() == 220);
    CHECK(r.size() == PixelSize{100, 200});
    CHECK(r.origin() == PixelPoint{10, 20});
  }

  TEST_CASE("contains") {
    PixelRect r{0, 0, 10, 10};
    CHECK(r.contains({0, 0}));
    CHECK(r.contains({5, 5}));
    CHECK(r.contains({9, 9}));
    CHECK_FALSE(r.contains({10, 5}));
    CHECK_FALSE(r.contains({5, 10}));
    CHECK_FALSE(r.contains({-1, 5}));
  }

  TEST_CASE("intersects") {
    PixelRect a{0, 0, 10, 10};
    PixelRect b{5, 5, 10, 10};
    CHECK(a.intersects(b));
    CHECK(b.intersects(a));

    PixelRect c{10, 0, 10, 10};
    CHECK_FALSE(a.intersects(c));

    PixelRect d{0, 10, 10, 10};
    CHECK_FALSE(a.intersects(d));
  }

  TEST_CASE("intersect") {
    PixelRect a{0, 0, 10, 10};
    PixelRect b{5, 5, 10, 10};
    auto i = a.intersect(b);
    CHECK(i == PixelRect{5, 5, 5, 5});

    PixelRect c{20, 20, 10, 10};
    auto empty = a.intersect(c);
    CHECK(empty == PixelRect{});
  }

  TEST_CASE("translated") {
    PixelRect r{10, 20, 30, 40};
    auto t = r.translated(5, -10);
    CHECK(t == PixelRect{15, 10, 30, 40});
  }

  TEST_CASE("from_xywh") {
    auto r = PixelRect::from_xywh(1, 2, 3, 4);
    CHECK(r == PixelRect{1, 2, 3, 4});
  }

  TEST_CASE("from_corners") {
    auto r = PixelRect::from_corners(10, 20, 30, 50);
    CHECK(r == PixelRect{10, 20, 20, 30});
  }

  TEST_CASE("from_origin_size") {
    auto r = PixelRect::from_origin_size({10, 20}, {30, 40});
    CHECK(r == PixelRect{10, 20, 30, 40});
  }

  TEST_CASE("equality") {
    CHECK(PixelRect{1, 2, 3, 4} == PixelRect{1, 2, 3, 4});
    CHECK(PixelRect{1, 2, 3, 4} != PixelRect{1, 2, 3, 5});
  }
}

TEST_SUITE("ClientInfo") {
  TEST_CASE("viewport_pixel subtracts one row") {
    ClientInfo ci{{800, 600}, {8, 16}, 100, 37};
    auto vp = ci.viewport_pixel();
    CHECK(vp.width == 800);
    CHECK(vp.height == 600 - 16);
  }

  TEST_CASE("is_valid") {
    ClientInfo valid{{800, 600}, {8, 16}, 100, 37};
    CHECK(valid.is_valid());

    ClientInfo no_cell{{800, 600}, {0, 0}, 100, 37};
    CHECK_FALSE(no_cell.is_valid());

    ClientInfo no_pixel{{0, 0}, {8, 16}, 0, 0};
    CHECK_FALSE(no_pixel.is_valid());
  }
}
