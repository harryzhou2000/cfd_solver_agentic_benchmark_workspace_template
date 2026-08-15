#pragma once

#include <map>
#include <string>
#include <vector>

#include "mesh_reader.hpp"
#include "types.hpp"

namespace cfd {

// Builds the processed Mesh (volumes, centers, internal faces, oriented
// boundary faces) from raw CGNS import data.
//
// `family_bc_map` maps boundary family names (bface_tag) to BC type strings
// from the case file, e.g. {"bc-2": "farfield", "bc-4":
// "no_slip_adiabatic_wall"}. With an empty map, family names fall back to
// bc_type_from_string() on the tag itself.
Mesh build_geometry(const MeshData& data,
                    const std::map<std::string, std::string>& family_bc_map);

// Same as above with no family->BC mapping (all boundary faces get the
// fallback BC type).
Mesh build_geometry(const MeshData& data);

}  // namespace cfd
