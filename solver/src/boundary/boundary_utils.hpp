#pragma once

#include "types.hpp"
#include "mesh/mesh_types.hpp"
#include <string>

namespace cfd {

/// Look up a boundary condition family by name in the mesh
/// Returns -1 if not found
Int find_boundary_patch_by_family(const std::string& family_name,
                                   const std::vector<BoundaryPatch>& patches);

/// Check if boundary type is a wall (slip or no-slip)
inline bool is_wall_bc(BcType t) {
    return t == BcType::SlipWall || t == BcType::NoSlipAdiabaticWall;
}

/// Check if boundary type is a farfield
inline bool is_farfield_bc(BcType t) {
    return t == BcType::Farfield;
}

} // namespace cfd
