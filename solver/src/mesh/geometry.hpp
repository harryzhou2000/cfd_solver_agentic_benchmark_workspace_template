#pragma once

// Mesh geometry computation (cell centers/volumes, face centers/normals/areas).

#include <vector>

#include "common/types.hpp"
#include "mesh/mesh.hpp"

namespace cfd {

// Computes, for every cell: cell_center (centroid of vertices) and volume
// (polygon area in 2D, shoelace formula); for every face: center (midpoint),
// normal (unit vector perpendicular to the face, oriented to point from the
// left to the right cell; outward on boundary faces) and area (face length).
// `vertices` holds the global vertex coordinates (0-based index).
void compute_geometry(Mesh2D& mesh, const std::vector<Vector3>& vertices);

}  // namespace cfd
