#pragma once

#include <optional>

#include "geometry/linalg.h"

class CSGProducts;

// Exact ray casting against the preview's CSG products, used to find the
// visible face under the mouse (e.g. to place parts on it).
namespace SurfacePick {

struct Hit {
  double t;         // distance along the ray, in units of `direction`
  Vector3d point;   // world coordinates
  Vector3d normal;  // outward normal of the visible face
  int leafIndex;    // node index of the leaf whose face was hit
};

// First visible surface along origin + t * direction (t > 0). Each product is
// the intersection of its positive leaves minus its subtracted leaves; the
// products are united. Leaves must be closed 3D meshes.
std::optional<Hit> castRay(const CSGProducts& products, const Vector3d& origin, const Vector3d& direction);

}  // namespace SurfacePick
