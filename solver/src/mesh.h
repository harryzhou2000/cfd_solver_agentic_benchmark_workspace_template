#pragma once
// CGNS mesh reader + face/geometry construction for 2D unstructured meshes
// (TRI_3 / QUAD_4 cells, BAR_2 boundary elements).
//
// Multi-zone support: every zone of the (first) base is loaded. Node and cell
// indices are globalized across zones, and 1-to-1 interface nodes are merged
// via the ZoneGridConnectivity data so that a single shared edge hash can be
// used. Edges seen by cells from two different zones become interface faces
// (BCType::Interface); edges seen by a single cell become boundary faces.

#include <map>
#include <string>

#include "types.h"

namespace cfd {

// Read an unstructured 2D CGNS mesh and build the face list and geometry.
// `bc_map` maps mesh boundary family names to pre-validated BC types (see
// RunConfig::boundary_conditions, validated by load_case).
//
// Throws std::runtime_error on any CGNS or consistency failure.
Mesh read_mesh(const std::string& cgns_path,
               const std::map<std::string, BCType>& bc_map);

// Print a summary of the mesh (counts and boundary families) to stdout.
void print_mesh_summary(const Mesh& mesh);

}  // namespace cfd
