#include "numerics/limiter.hpp"

#include <algorithm>
#include <cmath>

#include "numerics/reconstruction.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

void barth_jespersen_limit(std::vector<double>& gradients,
                           const std::vector<double>& U,
                           const DistributedMesh& dmesh,
                           const std::vector<std::vector<int>>& cell_face_map,
                           double gamma) {
  constexpr double kEps = 1e-30;
  const long long n_owned = dmesh.n_owned;

  for (long long c = 0; c < n_owned; ++c) {
    const PrimitiveState prim = cons_to_prim(
        U.data() + static_cast<std::size_t>(c) * NVARS, gamma, 1.0);
    const Vector3& center = dmesh.cells[static_cast<std::size_t>(c)].cell_center;
    const double phi_cell[NVARS] = {prim.rho, prim.u, prim.v, prim.p};

    // Min/max over the cell and its face-neighbors.
    double phi_max[NVARS] = {prim.rho, prim.u, prim.v, prim.p};
    double phi_min[NVARS] = {prim.rho, prim.u, prim.v, prim.p};
    for (int uf : cell_face_map[static_cast<std::size_t>(c)]) {
      const int nbr = face_other_cell(dmesh, uf, static_cast<int>(c));
      if (nbr < 0) continue;  // boundary: neighbor value == cell value
      const PrimitiveState nbr_prim = cons_to_prim(
          U.data() + static_cast<std::size_t>(nbr) * NVARS, gamma, 1.0);
      const double phi_nbr[NVARS] = {nbr_prim.rho, nbr_prim.u, nbr_prim.v,
                                     nbr_prim.p};
      for (int k = 0; k < NVARS; ++k) {
        phi_max[k] = std::max(phi_max[k], phi_nbr[k]);
        phi_min[k] = std::min(phi_min[k], phi_nbr[k]);
      }
    }

    double* g = gradients.data() + static_cast<std::size_t>(c) * NVARS * 2;
    for (int k = 0; k < NVARS; ++k) {
      if (phi_max[k] == phi_min[k]) {
        // No local variation: a nonzero gradient is spurious.
        g[k * 2 + 0] = 0.0;
        g[k * 2 + 1] = 0.0;
        continue;
      }
      double alpha = 1.0;
      for (int uf : cell_face_map[static_cast<std::size_t>(c)]) {
        const Face2D& f = get_local_face(dmesh, uf);
        const Vector3 dx = f.center - center;
        const double phi_f =
            phi_cell[k] + g[k * 2 + 0] * dx.x + g[k * 2 + 1] * dx.y;
        double a;
        if (phi_f > phi_cell[k]) {
          a = (phi_max[k] - phi_cell[k]) / (phi_f - phi_cell[k] + kEps);
        } else if (phi_f < phi_cell[k]) {
          a = (phi_min[k] - phi_cell[k]) / (phi_f - phi_cell[k] + kEps);
        } else {
          a = 1.0;
        }
        alpha = std::min(alpha, std::max(0.0, std::min(1.0, a)));
      }
      g[k * 2 + 0] *= alpha;
      g[k * 2 + 1] *= alpha;
    }
  }
}

}  // namespace cfd
