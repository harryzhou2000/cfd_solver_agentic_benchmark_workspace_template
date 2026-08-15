#pragma once

/// @file geometry.hpp
/// Cell/face geometry computation for 2-D unstructured meshes.

#include "common.hpp"

namespace cfd {

/// Compute cell centroids/volumes, face centroids/normals/areas, and
/// cell-face adjacency from raw mesh data (nodes, cells, boundary faces).
///
/// Fills in, for every cell: `centroid` (average of its node positions) and
/// `volume` (shoelace polygon area). For every face: `centroid` (edge
/// midpoint), `normal` (unit normal obtained by rotating the edge vector by
/// 90 degrees), and `area` (edge length). Internal faces are created from
/// edges shared by two cells; boundary faces (created by the mesh reader) get
/// their owning cell assigned so the stored normal points outward. Also
/// fills `Mesh::min_coord`/`Mesh::max_coord` (bounding box) and each cell's
/// `faces`/`neighbors` lists.
///
/// Completeness check: every edge used by exactly one cell must be claimed
/// by a boundary face; otherwise the mesh has missing boundary conditions or
/// an unmerged zone interface, and an exception is thrown.
///
/// @throws std::runtime_error if the mesh is degenerate, non-manifold in a
///         way that prevents consistent geometry, or has unclaimed 1-cell
///         edges.
void compute_geometry(Mesh& mesh);

} // namespace cfd
