#include "gui/SurfacePick.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/CSGNode.h"
#include "geometry/PolySet.h"

namespace SurfacePick {

namespace {

// Where the ray crosses a surface: distance, outward normal there, leaf.
struct Boundary {
  double t;
  Vector3d normal;
  int leaf;
};

// A stretch of the ray inside a solid.
struct Interval {
  Boundary in, out;
};

// Sorted and disjoint.
using Intervals = std::vector<Interval>;

Boundary flipped(const Boundary& b) { return {b.t, -b.normal, b.leaf}; }

bool rayHitsBox(const BoundingBox& box, const Vector3d& origin, const Vector3d& dir)
{
  if (box.isEmpty()) return false;
  double t0 = 0.0, t1 = std::numeric_limits<double>::infinity();
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(dir[i]) < 1e-15) {
      if (origin[i] < box.min()[i] || origin[i] > box.max()[i]) return false;
      continue;
    }
    double a = (box.min()[i] - origin[i]) / dir[i];
    double b = (box.max()[i] - origin[i]) / dir[i];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    if (t0 > t1) return false;
  }
  return true;
}

// Stretches of the ray inside one leaf (a closed mesh), from the order of
// its entering and exiting crossings.
Intervals leafIntervals(const CSGLeaf& leaf, const Vector3d& origin, const Vector3d& dir)
{
  if (!leaf.polyset || leaf.polyset->getDimension() != 3) return {};
  // Slightly grown box: the bounding box may be tight on flat faces.
  BoundingBox box = leaf.getBoundingBox();
  if (box.isEmpty()) return {};
  box.extend(box.min() - Vector3d::Constant(1e-6));
  box.extend(box.max() + Vector3d::Constant(1e-6));
  if (!rayHitsBox(box, origin, dir)) return {};

  const Transform3d& m = leaf.matrix;
  const bool mirrored = m.linear().determinant() < 0;
  std::vector<Vector3d> world;
  world.reserve(leaf.polyset->vertices.size());
  for (const auto& v : leaf.polyset->vertices) world.push_back(m * v);

  struct Crossing {
    double t;
    Vector3d normal;
    bool entering;
  };
  std::vector<Crossing> crossings;
  for (const auto& face : leaf.polyset->indices) {
    for (size_t k = 1; k + 1 < face.size(); ++k) {
      const Vector3d& a = world[face[0]];
      const Vector3d& b = world[face[k]];
      const Vector3d& c = world[face[k + 1]];
      // Moller-Trumbore
      const Vector3d e1 = b - a, e2 = c - a;
      const Vector3d p = dir.cross(e2);
      const double det = e1.dot(p);
      if (std::fabs(det) < 1e-14) continue;
      const double inv = 1.0 / det;
      const Vector3d s = origin - a;
      const double u = s.dot(p) * inv;
      if (u < -1e-9 || u > 1.0 + 1e-9) continue;
      const Vector3d q = s.cross(e1);
      const double v = dir.dot(q) * inv;
      if (v < -1e-9 || u + v > 1.0 + 1e-9) continue;
      const double t = e2.dot(q) * inv;
      if (t <= 0.0) continue;
      Vector3d n = e1.cross(e2);
      if (mirrored) n = -n;
      if (n.squaredNorm() < 1e-30) continue;
      n.normalize();
      crossings.push_back({t, n, n.dot(dir) < 0.0});
    }
  }
  std::sort(crossings.begin(), crossings.end(), [](const Crossing& x, const Crossing& y) { return x.t < y.t; });

  Intervals result;
  int depth = 0;
  Boundary start{0.0, -dir.normalized(), leaf.index};  // used when the ray starts inside
  double lastT = -1.0;
  bool lastEntering = false;
  for (const auto& c : crossings) {
    // A ray through a shared edge crosses both triangles: count it once.
    if (std::fabs(c.t - lastT) < 1e-9 * (1.0 + std::fabs(c.t)) && c.entering == lastEntering) continue;
    lastT = c.t;
    lastEntering = c.entering;
    const Boundary here{c.t, c.normal, leaf.index};
    if (c.entering) {
      if (depth++ == 0) start = here;
    } else if (--depth <= 0) {
      result.push_back({start, here});
      depth = 0;
    }
  }
  return result;
}

Intervals intersect(const Intervals& a, const Intervals& b)
{
  Intervals result;
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    const Boundary& in = a[i].in.t >= b[j].in.t ? a[i].in : b[j].in;
    const Boundary& out = a[i].out.t <= b[j].out.t ? a[i].out : b[j].out;
    if (in.t < out.t) result.push_back({in, out});
    if (a[i].out.t < b[j].out.t) ++i;
    else ++j;
  }
  return result;
}

// Removing a solid exposes its faces from the inside: their normals flip.
Intervals subtract(const Intervals& a, const Intervals& b)
{
  Intervals result;
  for (Interval rest : a) {
    bool remaining = true;
    for (const Interval& cut : b) {
      if (cut.out.t <= rest.in.t) continue;
      if (cut.in.t >= rest.out.t) break;
      if (cut.in.t > rest.in.t) result.push_back({rest.in, flipped(cut.in)});
      if (cut.out.t >= rest.out.t) {
        remaining = false;
        break;
      }
      rest.in = flipped(cut.out);
    }
    if (remaining) result.push_back(rest);
  }
  return result;
}

}  // namespace

std::optional<Hit> castRay(const CSGProducts& products, const Vector3d& origin, const Vector3d& direction)
{
  std::unordered_map<const CSGLeaf *, Intervals> cache;
  auto intervalsOf = [&](const std::shared_ptr<CSGLeaf>& leaf) -> const Intervals& {
    auto it = cache.find(leaf.get());
    if (it == cache.end()) it = cache.emplace(leaf.get(), leafIntervals(*leaf, origin, direction)).first;
    return it->second;
  };

  std::optional<Hit> best;
  for (const auto& product : products.products) {
    if (product.intersections.empty()) continue;
    Intervals solid = intervalsOf(product.intersections.front().leaf);
    for (size_t i = 1; i < product.intersections.size() && !solid.empty(); ++i) {
      solid = intersect(solid, intervalsOf(product.intersections[i].leaf));
    }
    for (const auto& cut : product.subtractions) {
      if (solid.empty()) break;
      solid = subtract(solid, intervalsOf(cut.leaf));
    }
    for (const Interval& interval : solid) {
      if (interval.in.t <= 1e-9) continue;  // the ray starts inside
      if (!best || interval.in.t < best->t) {
        best = Hit{interval.in.t, origin + interval.in.t * direction, interval.in.normal.normalized(),
                   interval.in.leaf};
      }
      break;
    }
  }
  return best;
}

}  // namespace SurfacePick
