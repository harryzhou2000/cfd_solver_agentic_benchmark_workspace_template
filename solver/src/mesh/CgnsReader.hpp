// CGNS import for 2-D unstructured meshes.
//
// Handles multi-zone files, mixed element sections (TRI_3 / QUAD_4 / MIXED),
// element- or vertex-located boundary conditions, and vertex 1-to-1 zone
// connections (which are merged so that the assembled mesh is a single
// conforming unstructured grid).  Nothing about the two benchmark meshes is
// assumed: zone names, section names, element types and family names are all
// discovered from the file.
#pragma once

#include <string>

#include "mesh/GlobalMesh.hpp"

namespace cfd {

struct CgnsReadOptions {
  // If > 0, additionally merge nodes closer than this distance.  Used only as
  // a fallback for meshes whose inter-zone interfaces are not described by
  // vertex 1-to-1 connectivity.
  Real node_merge_tol = 0.0;
  bool verbose = true;
};

struct CgnsMeshInfo {
  std::string base_name;
  int num_zones = 0;
  std::vector<std::string> zone_names;
  Index nodes_before_merge = 0;
  Index nodes_merged = 0;
  Real max_merge_gap = 0.0;
};

// Reads `path` into `mesh` (nodes, cells, boundary patches, topology).
CgnsMeshInfo readCgnsMesh(const std::string& path, const CgnsReadOptions& opt, GlobalMesh& mesh);

}  // namespace cfd
