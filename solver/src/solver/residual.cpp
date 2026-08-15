#include <fmt/format.h>

#include "solver/residual.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "numerics/reconstruction.hpp"
#include "physics/boundary.hpp"
#include "physics/inviscid_flux.hpp"
#include "physics/viscous_flux.hpp"

namespace cfd {

ResidualNorm compute_norms(const std::vector<double>& R,
                           const DistributedMesh& dmesh, MPI_Comm comm) {
  double sum_sq[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double linf_pv[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double sum_sq_all = 0.0;
  long long n_owned = dmesh.n_owned;
  const long long n_avail =
      std::min<long long>(n_owned, static_cast<long long>(R.size()) / NVARS);
  for (long long c = 0; c < n_avail; ++c) {
    const double* r = R.data() + static_cast<std::size_t>(c) * NVARS;
    for (int k = 0; k < NVARS; ++k) {
      const double v = std::fabs(r[k]);
      sum_sq[k] += v * v;
      sum_sq_all += v * v;
      linf_pv[k] = std::max(linf_pv[k], v);
    }
  }

  double sum_sq_g[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double linf_g[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double sum_all_g = 0.0;
  long long n_owned_g = 0;
  MPI_Allreduce(sum_sq, sum_sq_g, NVARS, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(&sum_sq_all, &sum_all_g, 1, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(linf_pv, linf_g, NVARS, MPI_DOUBLE, MPI_MAX, comm);
  MPI_Allreduce(&n_owned, &n_owned_g, 1, MPI_LONG_LONG, MPI_SUM, comm);
  const double linf_all = *std::max_element(linf_g, linf_g + NVARS);

  ResidualNorm norm;
  const double inv_n = 1.0 / static_cast<double>(n_owned_g);
  for (int k = 0; k < NVARS; ++k) {
    norm.l2_per_var[k] = std::sqrt(sum_sq_g[k] * inv_n);
    norm.linf_per_var[k] = linf_g[k];
  }
  norm.l2 = std::sqrt(sum_all_g * inv_n);
  norm.linf = linf_all;
  return norm;
}

void compute_residual(const std::vector<double>& U,
                      const DistributedMesh& dmesh, const CaseConfig& cfg,
                      double mu, std::vector<double>& residual,
                      const std::vector<double>* gradients) {
  const double gamma = cfg.gas.gamma;
  const double R = cfg.gas.R;
  const double Pr = cfg.gas.prandtl;
  const double rusanov_scale =
      cfg.run_control.rusanov_dissipation_scale.value_or(1.0);
  const bool viscous = (cfg.physics.mode == "laminar") && mu > 0.0;
  const bool second_order = gradients != nullptr;

  residual.assign(U.size(), 0.0);

  // Freestream conservative state (for the farfield BC).
  double U_inf[NVARS];
  prim_to_cons(freestream_primitive(cfg), gamma, U_inf);

  auto cell_state = [&](int c) -> const double* {
    return U.data() + static_cast<std::size_t>(c) * NVARS;
  };
  auto cell_grad = [&](int c) -> const double* {
    return gradients->data() + static_cast<std::size_t>(c) * NVARS * 2;
  };
  // Reconstructed (or plain) state of cell `c` at the face `f`.
  auto face_state = [&](int c, const Face2D& f, double* Uf) {
    if (second_order) {
      const Vector3 dx =
          f.center - dmesh.cells[static_cast<std::size_t>(c)].cell_center;
      reconstruct_face(cell_state(c), cell_grad(c), dx, gamma, Uf);
    } else {
      for (int k = 0; k < NVARS; ++k) Uf[k] = cell_state(c)[k];
    }
  };

  auto accumulate_face = [&](int left, int right, const Face2D& f,
                             bool acc_left, bool acc_right) {
    double ULf[NVARS], URf[NVARS];
    face_state(left, f, ULf);
    face_state(right, f, URf);
    double flux[NVARS];
    rusanov_flux(ULf, URf, f.normal, f.area, gamma, flux, rusanov_scale, R);
    if (viscous) {
      double fv[NVARS];
      if (second_order) {
        // Second-order viscous flux: face-averaged primitive gradients
        // (gradients were halo-exchanged, so ghost entries are valid).
        viscous_flux_gradient(cell_state(left), cell_state(right),
                              cell_grad(left), cell_grad(right), f.normal,
                              f.area, mu, gamma, R, Pr, fv);
      } else {
        viscous_flux(cell_state(left), cell_state(right),
                     dmesh.cells[static_cast<std::size_t>(left)].cell_center,
                     dmesh.cells[static_cast<std::size_t>(right)].cell_center,
                     f.center, f.normal, f.area, mu, gamma, R, Pr, fv);
      }
      // The Navier-Stokes system is dU/dt + div(F_conv) = div(F_visc), so the
      // residual is R = integral(F_conv - F_visc).n dA: the viscous flux is
      // SUBTRACTED (adding it would make viscosity anti-diffusive).
      for (int k = 0; k < NVARS; ++k) flux[k] -= fv[k];
    }
    if (acc_left) {
      double* resL = residual.data() + static_cast<std::size_t>(left) * NVARS;
      for (int k = 0; k < NVARS; ++k) resL[k] += flux[k];
    }
    if (acc_right) {
      double* resR = residual.data() + static_cast<std::size_t>(right) * NVARS;
      for (int k = 0; k < NVARS; ++k) resR[k] -= flux[k];
    }
  };

  // ---- interior faces --------------------------------------------------
  for (const Face2D& f : dmesh.interior_faces)
    accumulate_face(f.left_cell, f.right_cell, f, true, true);

  // ---- boundary faces --------------------------------------------------
  for (const Face2D& f : dmesh.boundary_faces) {
    const int owner = f.left_cell >= 0 ? f.left_cell : f.right_cell;
    double Uface[NVARS];
    face_state(owner, f, Uface);
    double flux[NVARS] = {0.0, 0.0, 0.0, 0.0};
    switch (f.bc_type) {
      case BCType::Farfield:
        farfield_flux(Uface, U_inf, f.normal, f.area, gamma, rusanov_scale,
                      flux);
        break;
      case BCType::SlipWall:
        slip_wall_flux(Uface, f.normal, f.area, gamma, flux);
        break;
      case BCType::NoSlipAdiabaticWall: {
        // Direct wall flux: inviscid (pressure) part added below, viscous
        // (shear) part subtracted — same convention as interior faces. The
        // pressure uses the (reconstructed) face state; the shear uses the
        // cell-center velocity (Newton's law, see boundary.cpp).
        double fvis[NVARS] = {0.0, 0.0, 0.0, 0.0};
        noslip_adiabatic_wall_flux(cell_state(owner), Uface,
                                   dmesh.cells[static_cast<std::size_t>(owner)]
                                       .cell_center,
                                   f.center, f.normal, f.area, gamma, R, Pr,
                                   mu, flux, fvis);
        double* res =
            residual.data() + static_cast<std::size_t>(owner) * NVARS;
        for (int k = 0; k < NVARS; ++k) res[k] -= fvis[k];
        break;
      }
      case BCType::Interior:
      default:
        // Should never appear in the boundary list; skip defensively.
        continue;
    }
    double* res = residual.data() + static_cast<std::size_t>(owner) * NVARS;
    for (int k = 0; k < NVARS; ++k) res[k] += flux[k];
  }

  // ---- MPI faces -------------------------------------------------------
  for (const Face2D& f : dmesh.send_faces)
    accumulate_face(f.left_cell, f.right_cell, f, true, false);
  for (const Face2D& f : dmesh.recv_faces)
    accumulate_face(f.left_cell, f.right_cell, f, false, true);
}

}  // namespace cfd
