// cns2d -- finite-volume metric quantities.
//
// Everything the residual needs geometrically is precomputed once per rank:
// cell volumes/centroids, face areas/normals/centroids, and the least-squares
// gradient stencil weights.  The formulas are written in terms of node loops so
// the same code path serves triangles and quadrilaterals, and so extending to
// polygonal or 3-D cells means changing only these routines.
#pragma once

#include <vector>

#include "core/types.h"

namespace cns2d {

// Geometry of one cell.
struct CellGeometry {
  Vec2 centroid{};
  Real volume{0.0};             // 2-D: area (unit depth)
  Real inv_volume{0.0};
  Real characteristic_length{0.0};  // volume / perimeter-based length scale
};

// Geometry of one face.  'normal' is the unit normal pointing from the left
// cell towards the right cell (or out of the domain on a boundary face).
struct FaceGeometry {
  Vec2 centroid{};
  Vec2 normal{};   // unit
  Real area{0.0};  // 2-D: edge length (unit depth)
};

// Compute the area and centroid of a polygon given its node coordinates.
// Returns the signed area (positive for counter-clockwise ordering).
Real polygonAreaCentroid(const Real *xs, const Real *ys, int n, Vec2 &centroid);

}  // namespace cns2d
