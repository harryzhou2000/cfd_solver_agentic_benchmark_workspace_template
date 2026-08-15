#pragma once

#include "mesh/mesh_types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace cfd {

/// Read a CGNS mesh file and populate a Mesh structure.
/// Handles: single-zone and multi-zone (with 1-to-1 zone connectivity),
/// TRI_3 and QUAD_4 elements, boundary face sections, family-based BC mapping.
///
/// @param filename  Path to the CGNS file
/// @param bc_map    Boundary condition mapping: family_name -> BcType
/// @return Fully populated Mesh with cells, faces, and boundary patches
Mesh read_cgns_mesh(const std::string& filename,
                    const BcMappings& bc_map);

} // namespace cfd
