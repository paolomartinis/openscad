#include "gui/SurfacePick.h"

#include <catch2/catch_all.hpp>

#include <memory>

#include "core/CSGNode.h"
#include "geometry/PolySet.h"

namespace {

// Unit cube with outward (counter-clockwise) faces.
std::shared_ptr<const PolySet> unitCube()
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  ps->indices = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
  return ps;
}

// Box from `lo` to `hi` as a CSG leaf.
std::shared_ptr<CSGLeaf> box(const Vector3d& lo, const Vector3d& hi, int index)
{
  Transform3d m = Transform3d::Identity();
  m.translate(lo);
  m.scale(hi - lo);
  return std::make_shared<CSGLeaf>(unitCube(), m, Color4f(), "box", index);
}

}  // namespace

TEST_CASE("SurfacePick finds the visible face", "[surfacepick]")
{
  CSGProducts products;  // starts with one empty product
  auto& product = products.products.front();
  product.intersections.emplace_back(box({0, 0, 0}, {10, 10, 10}, 1));

  SECTION("top face of a box")
  {
    const auto hit = SurfacePick::castRay(products, {3, 4, 50}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(hit->point.isApprox(Vector3d(3, 4, 10)));
    CHECK(hit->normal.isApprox(Vector3d::UnitZ()));
    CHECK(hit->leafIndex == 1);
  }
  SECTION("side face, ray along -X")
  {
    const auto hit = SurfacePick::castRay(products, {40, 5, 5}, {-1, 0, 0});
    REQUIRE(hit);
    CHECK(hit->point.isApprox(Vector3d(10, 5, 5)));
    CHECK(hit->normal.isApprox(Vector3d::UnitX()));
  }
  SECTION("ray along an edge between two triangles of a face")
  {
    // The quad is split along its diagonal; x == y lies on it.
    const auto hit = SurfacePick::castRay(products, {5, 5, 50}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(hit->point.isApprox(Vector3d(5, 5, 10)));
  }
  SECTION("miss")
  {
    CHECK_FALSE(SurfacePick::castRay(products, {30, 30, 50}, {0, 0, -1}));
  }
  SECTION("inside a hole the ray reaches its bottom, facing up")
  {
    product.subtractions.emplace_back(box({4, 4, 5}, {6, 6, 11}, 2));
    const auto hit = SurfacePick::castRay(products, {5, 5.5, 50}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(hit->point.isApprox(Vector3d(5, 5.5, 5)));
    CHECK(hit->normal.isApprox(Vector3d::UnitZ()));
    CHECK(hit->leafIndex == 2);
    // Next to the hole the top face is still there.
    const auto beside = SurfacePick::castRay(products, {2, 2, 50}, {0, 0, -1});
    REQUIRE(beside);
    CHECK(beside->point.z() == Catch::Approx(10));
  }
  SECTION("hole wall seen from inside the hole")
  {
    product.subtractions.emplace_back(box({4, 4, 5}, {6, 6, 11}, 2));
    // Start inside the hole, look along +X: the wall at x = 6 faces -X.
    const auto hit = SurfacePick::castRay(products, {5, 5, 8}, {1, 0, 0});
    REQUIRE(hit);
    CHECK(hit->point.isApprox(Vector3d(6, 5, 8)));
    CHECK(hit->normal.isApprox(-Vector3d::UnitX()));
  }
  SECTION("intersection of two boxes")
  {
    product.intersections.emplace_back(box({5, -5, -5}, {20, 20, 20}, 3));
    const auto hit = SurfacePick::castRay(products, {7, 5, 50}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(hit->point.z() == Catch::Approx(10));
    const auto missed = SurfacePick::castRay(products, {2, 5, 50}, {0, 0, -1});
    CHECK_FALSE(missed);
  }
  SECTION("union: the nearest product wins")
  {
    CSGProduct second;
    second.intersections.emplace_back(box({0, 0, 20}, {10, 10, 30}, 4));
    products.products.push_back(second);
    const auto hit = SurfacePick::castRay(products, {5, 5, 50}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(hit->point.z() == Catch::Approx(30));
    CHECK(hit->leafIndex == 4);
  }
}
