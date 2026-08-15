#pragma once

// LU-SGS (Lower-Upper Symmetric Gauss-Seidel) implicit operator with the
// exact block Jacobian of the first-order Rusanov flux.

#include <vector>

#include "partition/partition.hpp"

namespace cfd {

// Performs one LU-SGS sweep solving (D + L) D^{-1} (D + U) dU = -R:
//   Forward sweep (i ascending):
//     dU*_i = D_i^-1 * ( -R_i - sum_{j<i owned} L_ij * dU*_j )
//   Backward sweep (i descending):
//     dU_i  = dU*_i - D_i^-1 * sum_{j>i owned} U_ij * dU_j
// with 4x4 blocks:
//   L_ij / U_ij = the +/- split of the convective Euler Jacobian
//     (|A_euler| = alpha*A_euler + beta*I from the eigen-decomposition),
//     giving upwind coupling in BOTH sweeps
//   D_i = V_i/dt_i*I + diag_extra*I + sum_f [0.5*A*(|A_euler(n_if)|) +
//         0.5*lam_f*A*I]  (the dissipation and |A| smoothing in the
//         diagonal; the viscous contribution enters through V/dt, which is
//         built from lambda_c + lambda_v in compute_local_timesteps)
//   lam_f = (|vn_f| + a_f) * A_f
// Ghost-cell neighbors are not unknowns of the linear system, so only OWNED
// neighbors contribute to the off-diagonal sums.
//
// U: current conservative state (all local cells; ghosts up to date)
// residual: flux-balance residual R (the RHS is -R)
// dt: local pseudo-time steps per owned cell
// cell_face_map: per-owned-cell unified face indices (build_cell_face_map)
// diag_extra: optional per-owned-cell extra diagonal term added to D_i
//   (e.g. the BDF2 3V/(2*dt_phys) contribution); may be null
// dU: output, size NVARS * n_owned
void lusgs_sweep(const std::vector<double>& U,
                 const std::vector<double>& residual,
                 const DistributedMesh& dmesh,
                 const std::vector<double>& dt,
                 const std::vector<std::vector<int>>& cell_face_map,
                 double gamma, std::vector<double>& dU,
                 const std::vector<double>* diag_extra = nullptr);

// Positivity-preserving update limiter: scales the update of every OWNED
// cell by a per-cell factor so that the updated density and pressure cannot
// drop below floor_frac (default 0.5) of their current values. The whole
// cell vector is scaled uniformly (direction preserved). This protects the
// state from LU-SGS overshoots near shocks without changing the converged
// solution. U: current state, dU: in/out update (NVARS * n_owned).
void limit_update_positivity(const std::vector<double>& U,
                             std::vector<double>& dU, long long n_owned,
                             double gamma, double floor_frac = 0.5);

}  // namespace cfd
