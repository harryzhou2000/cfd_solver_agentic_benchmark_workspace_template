#include "boundary/boundary_utils.hpp"
#include "mesh/mesh_types.hpp"
#include <algorithm>

namespace cfd {

Int find_boundary_patch_by_family(const std::string& family_name,
                                   const std::vector<BoundaryPatch>& patches) {
    auto it = std::find_if(patches.begin(), patches.end(),
        [&](const BoundaryPatch& p) { return p.family_name == family_name; });
    if (it != patches.end()) {
        return static_cast<Int>(std::distance(patches.begin(), it));
    }
    return INVALID_INDEX;  // not found (Int is signed, so -1 is a valid sentinel)
}

} // namespace cfd
