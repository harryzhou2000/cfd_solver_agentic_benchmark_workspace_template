#pragma once

// CGNS unstructured mesh reader (2D).

#include <map>
#include <string>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd {

// Result of reading a CGNS mesh: the topology plus the global vertex
// coordinates (the mesh itself stores coordinates only implicitly through
// faces/cells; vertices are needed by compute_geometry).
struct MeshReadResult {
  Mesh2D mesh;
  std::vector<Vector3> vertices;  // global vertex coordinates, 0-based index
};

// Reads a 2D unstructured CGNS mesh (first base, ALL zones). Handles
// TRI_3, QUAD_4 and MIXED element sections, BAR_2 boundary sections, and
// PointList/PointRange boundary conditions. BC faces are tagged with the
// family name read from the mesh (falling back to the BC node name) and their
// BCType is assigned from `bc_map` (case-file boundary_conditions:
// family/tag name -> solver BC type string). Tags absent from `bc_map` are
// left as Farfield with a warning.
//
// Multi-zone meshes: every zone is read and concatenated (vertex arrays are
// concatenated with a per-zone offset; vertices on conforming interfaces are
// duplicated). Unassigned boundary edges (e.g. "con-*" inter-zone interface
// sections) are paired geometrically — identical midpoint and canonical
// endpoints — and merged into single interior faces. Single-zone meshes skip
// the stitching pass.
//
// Throws std::runtime_error on CGNS or consistency failures. Informational
// diagnostics ([cgns] lines) are printed only when `verbose` is true; in
// MPI runs pass `verbose = (rank == 0)` so only one rank prints them.
MeshReadResult read_cgns_mesh(
    const std::string& filename,
    const std::map<std::string, std::string>& bc_map = {},
    bool verbose = true);

}  // namespace cfd
