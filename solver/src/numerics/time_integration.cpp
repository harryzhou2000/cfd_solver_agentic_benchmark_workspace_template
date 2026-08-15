#include "numerics/time_integration.hpp"

#include <algorithm>
#include <cmath>

#include "physics/gas_model.hpp"

namespace cfd {

double compute_cfl(int step, double cfl_initial, double cfl_max,
                   int ramp_steps) {
  if (ramp_steps <= 0) return cfl_max;
  const double f =
      std::min(1.0, static_cast<double>(step) / static_cast<double>(ramp_steps));
  return cfl_initial + (cfl_max - cfl_initial) * f;
}

std::vector<double> compute_local_timesteps(const std::vector<double>& U,
                                            const DistributedMesh& dmesh,
                                            double cfl, double gamma,
                                            double mu, double Pr, double R) {
  const long long n_owned = dmesh.n_owned;
  std::vector<double> lam_c(static_cast<std::size_t>(n_owned), 0.0);
  std::vector<double> lam_v(static_cast<std::size_t>(n_owned), 0.0);

  auto acc = [&](int cell, double vn_f, double a_f, const Face2D& f,
                 double rho_cell) {
    if (cell < 0 || cell >= static_cast<int>(n_owned)) return;
    const double A = f.area;
    lam_c[static_cast<std::size_t>(cell)] += (std::fabs(vn_f) + a_f) * A;
    if (mu > 0.0 && rho_cell > 0.0) {
      const double V = dmesh.cells[static_cast<std::size_t>(cell)].volume;
      lam_v[static_cast<std::size_t>(cell)] +=
          std::max(4.0 / 3.0, gamma / Pr) * (mu / rho_cell) * A * A / V;
    }
  };

  auto face_speed = [&](int left, int right, const Face2D& f, double& vn_f,
                        double& a_f) {
    const PrimitiveState pL = cons_to_prim(
        U.data() + static_cast<std::size_t>(left) * NVARS, gamma, R);
    if (right >= 0) {
      const PrimitiveState pR = cons_to_prim(
          U.data() + static_cast<std::size_t>(right) * NVARS, gamma, R);
      vn_f = 0.5 * ((pL.u + pR.u) * f.normal.x + (pL.v + pR.v) * f.normal.y);
      a_f = 0.5 * (pL.a + pR.a);
    } else {
      // Boundary face: use the cell's own values.
      vn_f = pL.u * f.normal.x + pL.v * f.normal.y;
      a_f = pL.a;
    }
  };

  for (const Face2D& f : dmesh.interior_faces) {
    double vn_f = 0.0, a_f = 0.0;
    face_speed(f.left_cell, f.right_cell, f, vn_f, a_f);
    const PrimitiveState pL = cons_to_prim(
        U.data() + static_cast<std::size_t>(f.left_cell) * NVARS, gamma, R);
    const PrimitiveState pR = cons_to_prim(
        U.data() + static_cast<std::size_t>(f.right_cell) * NVARS, gamma, R);
    acc(f.left_cell, vn_f, a_f, f, pL.rho);
    acc(f.right_cell, vn_f, a_f, f, pR.rho);
  }
  for (const Face2D& f : dmesh.boundary_faces) {
    const int owner = f.left_cell >= 0 ? f.left_cell : f.right_cell;
    double vn_f = 0.0, a_f = 0.0;
    face_speed(owner, -1, f, vn_f, a_f);
    const PrimitiveState p = cons_to_prim(
        U.data() + static_cast<std::size_t>(owner) * NVARS, gamma, R);
    acc(owner, vn_f, a_f, f, p.rho);
  }
  for (const Face2D& f : dmesh.send_faces) {
    double vn_f = 0.0, a_f = 0.0;
    face_speed(f.left_cell, f.right_cell, f, vn_f, a_f);
    const PrimitiveState p = cons_to_prim(
        U.data() + static_cast<std::size_t>(f.left_cell) * NVARS, gamma, R);
    acc(f.left_cell, vn_f, a_f, f, p.rho);
  }
  for (const Face2D& f : dmesh.recv_faces) {
    double vn_f = 0.0, a_f = 0.0;
    face_speed(f.left_cell, f.right_cell, f, vn_f, a_f);
    const PrimitiveState p = cons_to_prim(
        U.data() + static_cast<std::size_t>(f.right_cell) * NVARS, gamma, R);
    acc(f.right_cell, vn_f, a_f, f, p.rho);
  }

  std::vector<double> dt(static_cast<std::size_t>(n_owned));
  for (long long c = 0; c < n_owned; ++c) {
    const double V = dmesh.cells[static_cast<std::size_t>(c)].volume;
    dt[static_cast<std::size_t>(c)] =
        cfl * V / std::max(1e-10, lam_c[static_cast<std::size_t>(c)] +
                                      lam_v[static_cast<std::size_t>(c)]);
  }
  return dt;
}

}  // namespace cfd
