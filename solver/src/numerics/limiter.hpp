#pragma once

// Barth-Jespersen gradient limiter (minimum shock-capturing requirement per
// TASK.md). Operates on the PRIMITIVE-variable gradients produced by
// compute_gradients and limits them in place.

#include <vector>

#include "partition/partition.hpp"

namespace cfd {

// Limits the primitive gradients of all OWNED cells in place with the
// Barth-Jespersen limiter:
//   For each cell and each primitive variable phi:
//     phi_max/min over the cell center and all face-neighbor cell centers
//     (boundary faces contribute the cell's own value);
//     for every face, the reconstructed face value phi_f = phi_c +
//     grad.face_dx must stay within [phi_min, phi_max]:
//       phi_f > phi_c : alpha_f = (phi_max - phi_c)/(phi_f - phi_c + eps)
//       phi_f < phi_c : alpha_f = (phi_min - phi_c)/(phi_f - phi_c + eps)
//       else          : alpha_f = 1
//     clamped to [0,1]; the cell-wide limiter is min over faces;
//   grad *= alpha.
//   gradients: input/output (NVARS*2 per owned cell, variable-major)
//   U: conservative state (all local cells; neighbors incl. ghosts must be
//      up to date)
//   gamma: specific heat ratio
void barth_jespersen_limit(std::vector<double>& gradients,
                           const std::vector<double>& U,
                           const DistributedMesh& dmesh,
                           const std::vector<std::vector<int>>& cell_face_map,
                           double gamma);

}  // namespace cfd
