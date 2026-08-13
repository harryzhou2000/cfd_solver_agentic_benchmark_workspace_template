// Phase 4: second-order conservative finite-volume solver core with
// matrix-free LU-SGS implicit pseudo-time marching (see solver.h).

#include "solver.h"

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "boundary.h"
#include "flux.h"
#include "output.h"
#include "physics.h"
#include "viscous.h"

namespace cfd {

namespace {

constexpr int kConsDoubles = 4;  // rho, rhou, rhov, rhoE
constexpr int kDefaultMaxSteps = 50;  // fallback when the case sets none
constexpr double kMinSafetyCfl = 1e-4;  // floor for the safety-net halving
constexpr int kMaxSafetyRetries = 5;

// A state is admissible if it is finite and has positive density and
// pressure (the pressure check also rejects non-positive internal energy).
bool state_is_valid(const ConsState& U, const GasConfig& gas) {
  if (!(std::isfinite(U.rho) && std::isfinite(U.rhou) &&
        std::isfinite(U.rhov) && std::isfinite(U.rhoE))) {
    return false;
  }
  if (U.rho <= 0.0) {
    return false;
  }
  const double p = pressure_from_cons(U, gas);
  return std::isfinite(p) && p > 0.0;
}

// Per-component local L2 (sum of squares) and Linf (max abs) over the
// owned cells of this rank.
struct LocalNorms {
  double sum_sq[kConsDoubles] = {0.0, 0.0, 0.0, 0.0};
  double max_abs[kConsDoubles] = {0.0, 0.0, 0.0, 0.0};
};

void accumulate_local_norms(const std::vector<ConsState>& R_local,
                            LocalNorms& n) {
  for (const ConsState& r : R_local) {
    const double comps[kConsDoubles] = {r.rho, r.rhou, r.rhov, r.rhoE};
    for (int i = 0; i < kConsDoubles; ++i) {
      n.sum_sq[i] += comps[i] * comps[i];
      n.max_abs[i] = std::max(n.max_abs[i], std::abs(comps[i]));
    }
  }
}

// Local max-abs norm over the owned cells (used for the inner-iteration
// residual reduction test; no MPI needed inside the inner loop).
double local_max_norm(const std::vector<ConsState>& R_local) {
  double m = 0.0;
  for (const ConsState& r : R_local) {
    m = std::max(m, std::abs(r.rho));
    m = std::max(m, std::abs(r.rhou));
    m = std::max(m, std::abs(r.rhov));
    m = std::max(m, std::abs(r.rhoE));
  }
  return m;
}

// Component accessor for the conserved state (0..3: rho, rhou, rhov, rhoE).
inline double comp_of(const ConsState& U, int k) {
  switch (k) {
    case 0: return U.rho;
    case 1: return U.rhou;
    case 2: return U.rhov;
    default: return U.rhoE;
  }
}

// Add a value to one component of the conserved state (0..3).
inline void accum_comp(ConsState& U, int k, double v) {
  switch (k) {
    case 0: U.rho += v; break;
    case 1: U.rhou += v; break;
    case 2: U.rhov += v; break;
    default: U.rhoE += v; break;
  }
}

double residual_orders(double initial, double current) {
  if (initial <= 0.0) {
    return 0.0;
  }
  return std::log10(initial / std::max(current, 1e-300));
}

// Print one line of the per-step table (rank 0 only).
void print_stats_row(const SolverStats& s, double orders) {
  std::printf("%7d  %12.6e  %8.2f  %8d  %12.6e  %12.6e  %12.6e\n", s.step,
              s.residual_l2, orders, s.inner_iter, s.cl, s.cd, s.cmz);
}

// ---------------------------------------------------------------------------
// Implicit LU-SGS helpers
// ---------------------------------------------------------------------------

// Spectral radius of a face (used for the LU-SGS diagonal and sweeps):
// the max over the two adjacent states of (|vn| + a) * area, plus the
// viscous contribution 2*mu/(rho_face * dist) * area for laminar runs.
double face_spectral_radius(const ConsState& UL, const ConsState& UR,
                            const Vec2& normal, const GasConfig& gas,
                            double mu, bool viscous, double dist,
                            double diss_scale) {
  const double area = normal.norm();
  double lambda = 0.0;
  const ConsState* states[2] = {&UL, &UR};
  for (const ConsState* U : states) {
    if (!(U->rho > 0.0)) {
      continue;
    }
    const double vn =
        std::abs((U->rhou * normal.x + U->rhov * normal.y) / U->rho);
    const double a = speed_of_sound(*U, gas);
    // The inviscid wave speed is scaled by the dissipation scale so the
    // implicit operator's diagonal/coupling match the residual's dissipation
    // (the viscous part is not scaled: it is not part of the Rusanov
    // dissipation).
    lambda = std::max(lambda, diss_scale * (vn + a) * area);
  }
  if (viscous && mu > 0.0 && dist > 0.0) {
    const double rho_face = 0.5 * (UL.rho + UR.rho);
    if (rho_face > 0.0) {
      // Viscous spectral radius coefficient: max(4/3, gamma/(Pr*(gamma-1)))
      // covers both the shear (4/3) and the thermal-diffusivity (energy
      // equation) contributions to the wave-speed bound.
      const double visc_factor =
          std::max(4.0 / 3.0, gas.gamma / (gas.prandtl * (gas.gamma - 1.0)));
      lambda += visc_factor * mu / (rho_face * dist) * area;
    }
  }
  return lambda;
}

// Per-face spectral radii, evaluated at the current state. For boundary
// faces the cell's own state is used on both sides and dist is the
// centroid-to-face distance; for internal/inter-rank faces dist is the
// distance between the two cell centroids.
void compute_face_lambda(const std::vector<ConsState>& U_local,
                         const LocalMesh& lm, const GasConfig& gas,
                         double mu, bool viscous, double diss_scale,
                         std::vector<double>& face_lambda) {
  for (size_t fi = 0; fi < lm.faces.size(); ++fi) {
    const LocalMesh::LocalFace& f = lm.faces[fi];
    const ConsState& UL = U_local[f.left];
    double dist = 0.0;
    if (f.right >= 0) {
      dist = (lm.cells[f.right].centroid - lm.cells[f.left].centroid).norm();
      face_lambda[fi] = face_spectral_radius(UL, U_local[f.right], f.normal,
                                             gas, mu, viscous, dist,
                                             diss_scale);
    } else if (f.right == -2) {
      dist = (lm.cells[f.right_local].centroid - lm.cells[f.left].centroid)
                 .norm();
      face_lambda[fi] = face_spectral_radius(UL, U_local[f.right_local],
                                             f.normal, gas, mu, viscous, dist,
                                             diss_scale);
    } else {
      dist = (f.centroid - lm.cells[f.left].centroid).norm();
      face_lambda[fi] =
          face_spectral_radius(UL, UL, f.normal, gas, mu, viscous, dist,
                               diss_scale);
    }
  }
}

// Diagonal of the implicit operator:
//   Lambda_i = sum of the face spectral radii over the faces of cell i
//   D_i      = V_i / dt_i + 0.5 * Lambda_i = Lambda_i * (1/cfl + 0.5)
// with the local pseudo time step dt_i = cfl * V_i / Lambda_i. Also reports
// the mean local pseudo time step (for the stats).
void compute_diagonal(const LocalMesh& lm,
                      const std::vector<double>& face_lambda, double cfl,
                      std::vector<double>& diag, double& mean_dt) {
  double dt_sum = 0.0;
  for (int i = 0; i < lm.n_owned; ++i) {
    double lam = 0.0;
    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    for (int k = begin; k < end; ++k) {
      lam += face_lambda[lm.cell_faces_data[k]];
    }
    const double vol = lm.cells[i].volume;
    double D = 1.0;  // degenerate-cell fallback
    if (lam > 0.0 && vol > 0.0 && cfl > 0.0) {
      D = lam * (1.0 / cfl + 0.5);
      dt_sum += cfl * vol / lam;
    }
    diag[i] = D;
  }
  mean_dt = lm.n_owned > 0 ? dt_sum / lm.n_owned : 0.0;
}

// ---------------------------------------------------------------------------
// Reverse Cuthill-McKee sweep ordering
// ---------------------------------------------------------------------------
//
// The LU-SGS sweeps are a Gauss-Seidel-type iteration whose convergence
// depends on the cell ordering. The raw mesh-file order (or a partition's
// global-id order) can be poor, causing the implicit iteration to stall or
// diverge at high CFL on some meshes (and to behave differently across rank
// counts). The reverse Cuthill-McKee ordering of the cell adjacency graph
// gives a locality-optimized sweep order that is robust and (up to the
// partition) rank-independent.

// Compute the RCM ordering of the OWNED cells. On return:
//   order[oi] = the local cell index at sweep position oi
//   pos[i]    = the sweep position of cell i
// The adjacency is the face-neighbor graph restricted to the owned cells.
void compute_rcm_order(const LocalMesh& lm, std::vector<int>& order,
                       std::vector<int>& pos) {
  const int n = lm.n_owned;
  order.assign(static_cast<size_t>(n), -1);
  pos.assign(static_cast<size_t>(n), -1);

  // Degree of each owned cell in the owned-neighbor graph.
  std::vector<int> degree(static_cast<size_t>(n), 0);
  std::vector<std::vector<int>> adj(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    adj[static_cast<size_t>(i)].reserve(static_cast<size_t>(end - begin));
    for (int kf = begin; kf < end; ++kf) {
      const LocalMesh::LocalFace& f = lm.faces[lm.cell_faces_data[kf]];
      const int j = (f.left == i) ? f.right : f.left;
      if (j >= 0 && j < n) {
        adj[static_cast<size_t>(i)].push_back(j);
      }
    }
    degree[static_cast<size_t>(i)] = static_cast<int>(
        adj[static_cast<size_t>(i)].size());
  }

  // BFS from the lowest-degree unvisited node, visiting neighbors in
  // increasing degree order; the final order is the reverse of the BFS
  // discovery order (the "reverse" in RCM).
  std::vector<int> bfs;
  bfs.reserve(static_cast<size_t>(n));
  std::vector<int> visited(static_cast<size_t>(n), 0);
  std::vector<int> queue;
  for (int start = 0; start < n; ++start) {
    if (visited[static_cast<size_t>(start)]) {
      continue;
    }
    // Pick the lowest-degree unvisited node of this component as the root.
    int root = start;
    for (int i = start; i < n; ++i) {
      if (!visited[static_cast<size_t>(i)] &&
          degree[static_cast<size_t>(i)] < degree[static_cast<size_t>(root)]) {
        root = i;
      }
    }
    queue.clear();
    queue.push_back(root);
    visited[static_cast<size_t>(root)] = 1;
    size_t head = 0;
    while (head < queue.size()) {
      const int u = queue[head++];
      bfs.push_back(u);
      // Neighbors of u, sorted by degree (ascending).
      const std::vector<int>& nb = adj[static_cast<size_t>(u)];
      std::vector<int> unvisited;
      for (const int v : nb) {
        if (!visited[static_cast<size_t>(v)]) {
          unvisited.push_back(v);
        }
      }
      std::sort(unvisited.begin(), unvisited.end(),
                [&degree](int a, int b) {
                  return degree[static_cast<size_t>(a)] <
                         degree[static_cast<size_t>(b)];
                });
      for (const int v : unvisited) {
        visited[static_cast<size_t>(v)] = 1;
        queue.push_back(v);
      }
    }
  }

  // Reverse the BFS discovery order.
  for (int oi = 0; oi < n; ++oi) {
    const int i = bfs[static_cast<size_t>(n - 1 - oi)];
    order[static_cast<size_t>(oi)] = i;
    pos[static_cast<size_t>(i)] = oi;
  }
}

// Matrix-free LU-SGS sweeps with the FLUX-DIFFERENCE coupling:
//
// The off-diagonal Jacobian entry for a face between cell i and its neighbor
// j is dR_i/dU_j = 0.5*(A_j - lambda_ij) (with the face normal pointing from
// i to j). The matrix-free form evaluates the Jacobian action exactly:
//
//   dR_i/dU_j * dU_j = 0.5 * (dF_j - lambda_ij * dU_j)
//
// with dF_j = F(U_j + dU_j) - F(U_j) the change of the inviscid face flux of
// the neighbor's updated state. The sweeps accumulate
//
//   sum = -0.5 * (dF_j - lambda_ij * dU_j)
//
// and solve
//   forward : dU_i = D_i^{-1} * (-R_i + sum over lower neighbors j < i)
//   backward: dU_i += D_i^{-1} * sum over upper neighbors j > i, then
//             U_i  += dU_i
//
// (The pure spectral-radius coupling -0.5*lambda*dU_j is the limiting case
// with the flux change neglected; it makes the sweeps behave explicitly on
// coherent modes and is not used.)
//
// U_step holds the state at the start of the current inner iteration (owned
// cells only; the coupling never touches ghost/boundary neighbors), so the
// flux difference measures the change produced by THIS sweep's updates.

// Flux change of neighbor j across the face shared with cell i:
// dF_j = F(U_j + dU_j) - F(U_j), evaluated with the outward normal of cell i
// (from i to j; the face area vector, so the difference is the
// face-integrated flux change).
inline void flux_difference(const ConsState& U_j, const ConsState& dU_j,
                            const Vec2& normal, const GasConfig& gas,
                            double out[4]) {
  double f0[4];
  double f1[4];
  ConsState U_new;
  U_new.rho = U_j.rho + dU_j.rho;
  U_new.rhou = U_j.rhou + dU_j.rhou;
  U_new.rhov = U_j.rhov + dU_j.rhov;
  U_new.rhoE = U_j.rhoE + dU_j.rhoE;
  inviscid_flux_dot_normal(U_new, normal, gas, f0);
  inviscid_flux_dot_normal(U_j, normal, gas, f1);
  for (int k = 0; k < kConsDoubles; ++k) {
    out[k] = f0[k] - f1[k];
  }
}

// Accumulate the coupling of one neighbor j of cell i into the sweep sum:
//   sum[k] += -0.5 * (dF_j[k] - lambda * dU_j[k])
// with the face normal oriented from i to j (outward of cell i).
inline void lusgs_face_coupling(const LocalMesh::LocalFace& f, int i, int j,
                                const std::vector<ConsState>& U_step,
                                const std::vector<ConsState>& delta_U,
                                double lambda, const GasConfig& gas,
                                double sum[4]) {
  const Vec2 n_ij = (f.left == i) ? f.normal : (f.normal * -1.0);
  double dF[kConsDoubles];
  flux_difference(U_step[j], delta_U[j], n_ij, gas, dF);
  if (!std::isfinite(dF[0]) || !std::isfinite(dF[1]) ||
      !std::isfinite(dF[2]) || !std::isfinite(dF[3])) {
    // The intermediate state U_j + dU_j became nonphysical (the flux
    // evaluation would produce NaN): fall back to the spectral-radius
    // coupling dF = lambda * dU_j, which makes this face's contribution
    // vanish (the diagonal still dominates the sweep).
    for (int k = 0; k < kConsDoubles; ++k) {
      dF[k] = lambda * comp_of(delta_U[j], k);
    }
  }
  for (int k = 0; k < kConsDoubles; ++k) {
    sum[k] += -0.5 * (dF[k] - lambda * comp_of(delta_U[j], k));
  }
}

void lusgs_forward_sweep(const std::vector<ConsState>& R_local,
                         std::vector<ConsState>& delta_U,
                         const std::vector<ConsState>& U_step,
                         const std::vector<double>& face_lambda,
                         const std::vector<double>& diag,
                         const LocalMesh& lm, const GasConfig& gas,
                         const std::vector<int>& order,
                         const std::vector<int>& pos) {
  for (int oi = 0; oi < lm.n_owned; ++oi) {
    const int i = order[static_cast<size_t>(oi)];
    double sum[kConsDoubles] = {0.0, 0.0, 0.0, 0.0};
    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    for (int kf = begin; kf < end; ++kf) {
      const int fi = lm.cell_faces_data[kf];
      const LocalMesh::LocalFace& f = lm.faces[fi];
      const int j = (f.left == i) ? f.right : f.left;
      if (j >= 0 && j < lm.n_owned &&
          pos[static_cast<size_t>(j)] < oi) {  // lower owned neighbor
        lusgs_face_coupling(f, i, j, U_step, delta_U, face_lambda[fi], gas,
                            sum);
      }
    }
    const double inv_d = 1.0 / diag[i];
    delta_U[i].rho = (-R_local[i].rho + sum[0]) * inv_d;
    delta_U[i].rhou = (-R_local[i].rhou + sum[1]) * inv_d;
    delta_U[i].rhov = (-R_local[i].rhov + sum[2]) * inv_d;
    delta_U[i].rhoE = (-R_local[i].rhoE + sum[3]) * inv_d;
  }
}

void lusgs_backward_sweep(std::vector<ConsState>& U_local,
                          const std::vector<ConsState>& U_step,
                          std::vector<ConsState>& delta_U,
                          const std::vector<double>& face_lambda,
                          const std::vector<double>& diag,
                          const LocalMesh& lm, const GasConfig& gas,
                          const std::vector<int>& order,
                          const std::vector<int>& pos) {
  for (int oi = lm.n_owned - 1; oi >= 0; --oi) {
    const int i = order[static_cast<size_t>(oi)];
    double sum[kConsDoubles] = {0.0, 0.0, 0.0, 0.0};
    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    for (int kf = begin; kf < end; ++kf) {
      const int fi = lm.cell_faces_data[kf];
      const LocalMesh::LocalFace& f = lm.faces[fi];
      const int j = (f.left == i) ? f.right : f.left;
      if (j >= 0 && j < lm.n_owned &&
          pos[static_cast<size_t>(j)] > oi) {  // upper neighbor
        lusgs_face_coupling(f, i, j, U_step, delta_U, face_lambda[fi], gas,
                            sum);
      }
    }
    const double inv_d = 1.0 / diag[i];
    delta_U[i].rho += sum[0] * inv_d;
    delta_U[i].rhou += sum[1] * inv_d;
    delta_U[i].rhov += sum[2] * inv_d;
    delta_U[i].rhoE += sum[3] * inv_d;
    U_local[i].rho += delta_U[i].rho;
    U_local[i].rhou += delta_U[i].rhou;
    U_local[i].rhov += delta_U[i].rhov;
    U_local[i].rhoE += delta_U[i].rhoE;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

SolverConfig make_solver_config(const RunConfig& cfg) {
  SolverConfig s;
  // The marching-step cap: steady runs fall back to a default cap when the
  // case sets none; transient runs are NOT capped by default — 0 is
  // preserved so the transient loop runs the full final_time / time_step
  // physical steps (a positive max_steps, e.g. --max-steps, still caps).
  s.max_steps = (cfg.run_type != "transient" && cfg.max_steps <= 0)
                    ? kDefaultMaxSteps
                    : cfg.max_steps;
  // Phase 4: the production CFL schedule from run_control is used as-is
  // (no explicit-stability cap — the implicit solver is stable at high CFL).
  s.cfl_initial = cfg.cfl_initial;
  s.cfl_max = cfg.cfl_max;
  s.pseudo_cfl_ramp_steps = cfg.pseudo_cfl_ramp_steps;
  s.residual_reduction_target = cfg.residual_reduction_target;
  s.min_inner_iterations = cfg.min_inner_iterations;
  s.max_inner_iterations = cfg.max_inner_iterations;
  s.inner_residual_reduction_target = cfg.inner_residual_reduction_target;
  s.rusanov_dissipation_scale = cfg.rusanov_dissipation_scale;
  s.spatial_order = cfg.spatial_order >= 2 ? 2 : 1;
  s.mu = (cfg.mode == "laminar" && cfg.reynolds > 0.0 &&
          cfg.viscous_flux != "disabled")
             ? viscosity_from_reynolds(cfg.freestream.rho, cfg.freestream.u_mag,
                                       cfg.reynolds_length, cfg.reynolds)
             : 0.0;
  s.viscous = s.mu > 0.0;
  s.write_residuals_every =
      cfg.write_residuals_every > 0 ? cfg.write_residuals_every : 1;
  s.write_forces_every = cfg.write_forces_every > 0 ? cfg.write_forces_every : 1;
  return s;
}

// ---------------------------------------------------------------------------
// Residual assembly
// ---------------------------------------------------------------------------

void compute_residual(const std::vector<ConsState>& U_local,
                      std::vector<ConsState>& R_local, const LocalMesh& lm,
                      const GasConfig& gas, const Freestream& fs,
                      double dissipation_scale, bool second_order,
                      const CellGradients* grads,
                      const GhostGradients* ghost_grads,
                      const Limiters* limiters, double mu, bool viscous,
                      const std::vector<ConsState>* boundary_states) {
  std::fill(R_local.begin(), R_local.end(), ConsState{});

  const double cp = gas.gamma * gas.R / (gas.gamma - 1.0);

  for (size_t fi = 0; fi < lm.faces.size(); ++fi) {
    const LocalMesh::LocalFace& face = lm.faces[fi];
    const int li = face.left;

    // ---- Left state: reconstructed (limited) face value ----
    ConsState UL = U_local[li];
    if (second_order) {
      const Vec2 dr = face.centroid - lm.cells[li].centroid;
      reconstruct_limited(U_local[li], *grads, *limiters, li, dr, UL);
      if (!state_is_valid(UL, gas)) {
        UL = U_local[li];  // positivity fallback: first order at this face
      }
    }

    // ---- Right state: internal cell, ghost cell, or boundary ----
    ConsState UR;
    int ri = -1;   // local index of the right cell (owned or ghost)
    int rgi = -1;  // ghost offset (ghost local index - n_owned)
    if (face.right >= 0) {
      ri = face.right;
      UR = U_local[ri];
      if (second_order) {
        const Vec2 dr = face.centroid - lm.cells[ri].centroid;
        reconstruct_limited(U_local[ri], *grads, *limiters, ri, dr, UR);
        if (!state_is_valid(UR, gas)) {
          UR = U_local[ri];
        }
      }
    } else if (face.right == -2) {
      ri = face.right_local;
      rgi = ri - lm.n_owned;
      UR = U_local[ri];
      if (second_order) {
        const Vec2 dr = face.centroid - lm.cells[ri].centroid;
        reconstruct_ghost(U_local[ri], *ghost_grads, rgi, dr, UR);
        if (!state_is_valid(UR, gas)) {
          UR = U_local[ri];
        }
      }
    } else {
      // Boundary: build the ghost state from the RECONSTRUCTED interior
      // state (second-order boundary treatment). When the caller supplies
      // precomputed boundary states (the implicit solver's per-step lagged
      // ghosts), they are used instead so the implicit operator stays
      // consistent with the residual at boundary faces (the boundary-state
      // Jacobian is not part of the LU-SGS operator).
      UR = (boundary_states != nullptr)
               ? (*boundary_states)[fi]
               : boundary_state(UL, face.bc_type, face.normal, fs, gas);
    }

    // ---- Inviscid flux (added to the left cell, subtracted from the
    // right cell; ghost cells never accumulate) ----
    const ConsState flux =
        rusanov_flux(UL, UR, face.normal, gas, dissipation_scale);
    R_local[li].rho += flux.rho;
    R_local[li].rhou += flux.rhou;
    R_local[li].rhov += flux.rhov;
    R_local[li].rhoE += flux.rhoE;
    if (face.right >= 0 && face.right < lm.n_owned) {
      R_local[face.right].rho -= flux.rho;
      R_local[face.right].rhou -= flux.rhou;
      R_local[face.right].rhov -= flux.rhov;
      R_local[face.right].rhoE -= flux.rhoE;
    }

    // ---- Viscous flux (laminar only; zero on slip walls and farfield) ----
    if (viscous && mu > 0.0) {
      double Fv[4] = {0.0, 0.0, 0.0, 0.0};
      bool has_Fv = false;

      if (face.right >= 0 || face.right == -2) {
        // Internal / inter-rank face: average the (limited) cell gradients
        // to the face, and average the reconstructed face states for the
        // face velocity and temperature. (The gradients are always computed
        // for viscous runs — see run_steady_solver — so grads/ghost_grads
        // are valid here even in first-order spatial mode.)
        Vec2 gL[4];
        for (int k = 0; k < kConsDoubles; ++k) {
          gL[k] = limited_grad(*grads, *limiters, k, li);
        }
        Vec2 gR[4];
        for (int k = 0; k < kConsDoubles; ++k) {
          gR[k] = (face.right >= 0)
                      ? limited_grad(*grads, *limiters, k, ri)
                      : ghost_grad(*ghost_grads, k, rgi);
        }
        const PrimGradients PL = cons_grad_to_prim(U_local[li], gL, gas);
        const PrimGradients PR = cons_grad_to_prim(U_local[ri], gR, gas);
        const double grad_u[2] = {0.5 * (PL.grad_u.x + PR.grad_u.x),
                                  0.5 * (PL.grad_u.y + PR.grad_u.y)};
        const double grad_v[2] = {0.5 * (PL.grad_v.x + PR.grad_v.x),
                                  0.5 * (PL.grad_v.y + PR.grad_v.y)};
        const double grad_T[2] = {0.5 * (PL.grad_T.x + PR.grad_T.x),
                                  0.5 * (PL.grad_T.y + PR.grad_T.y)};
        const double u_face =
            0.5 * (UL.rhou / UL.rho + UR.rhou / UR.rho);
        const double v_face =
            0.5 * (UL.rhov / UL.rho + UR.rhov / UR.rho);
        viscous_flux_dot_normal(grad_u, grad_v, grad_T, u_face, v_face,
                                face.normal, mu, cp, gas.prandtl, Fv);
        has_Fv = true;
      } else if (face.bc_type == BCType::NoSlipAdiabaticWall) {
        // One-sided wall treatment: u_wall = v_wall = 0, adiabatic
        // (grad T = 0), gradient from the wall value over the wall distance.
        const Vec2 n_hat = face.normal / face.area;
        const double dist =
            (face.centroid - lm.cells[li].centroid).norm();
        if (dist > 0.0) {
          const PrimState P = cons_to_prim(U_local[li], gas);
          const double gn_u = -P.u / dist;  // (u_wall - u_cell) / dist
          const double gn_v = -P.v / dist;
          const double grad_u[2] = {gn_u * n_hat.x, gn_u * n_hat.y};
          const double grad_v[2] = {gn_v * n_hat.x, gn_v * n_hat.y};
          const double grad_T[2] = {0.0, 0.0};  // adiabatic
          viscous_flux_dot_normal(grad_u, grad_v, grad_T, 0.0, 0.0,
                                  face.normal, mu, cp, gas.prandtl, Fv);
          has_Fv = true;
        }
      }
      // Slip wall / farfield boundary faces: no viscous flux.

      if (has_Fv) {
        R_local[li].rho += Fv[0];
        R_local[li].rhou += Fv[1];
        R_local[li].rhov += Fv[2];
        R_local[li].rhoE += Fv[3];
        if (face.right >= 0 && face.right < lm.n_owned) {
          R_local[face.right].rho -= Fv[0];
          R_local[face.right].rhou -= Fv[1];
          R_local[face.right].rhov -= Fv[2];
          R_local[face.right].rhoE -= Fv[3];
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Forces
// ---------------------------------------------------------------------------

void compute_forces(const std::vector<ConsState>& U_local, SolverStats& stats,
                    const LocalMesh& lm, const GasConfig& gas,
                    const Freestream& fs, const RunConfig& cfg, double mu,
                    bool viscous, const CellGradients* grads) {
  double fx = 0.0;  // pressure force on the body (local sums)
  double fy = 0.0;
  double mz = 0.0;  // pressure moment about cfg.moment_center
  double vfx = 0.0;  // viscous force on the body (laminar wall shear)
  double vfy = 0.0;
  double vmz = 0.0;  // viscous moment about cfg.moment_center

  for (const LocalMesh::LocalFace& face : lm.faces) {
    if (face.right != -1) {
      continue;  // wall faces only
    }
    if (face.bc_type != BCType::SlipWall &&
        face.bc_type != BCType::NoSlipAdiabaticWall) {
      continue;  // farfield etc. are not walls
    }

    const int li = face.left;
    double p = pressure_from_cons(U_local[li], gas);
    if (grads != nullptr) {
      // Second-order wall pressure: reconstruct p to the face centroid with
      // the cell gradient, grad p = (gamma-1) * (grad(rhoE) - grad ke)
      // (same derivation as cons_grad_to_prim).
      const Vec2 dr = face.centroid - lm.cells[li].centroid;
      const ConsState& Uc = U_local[li];
      const double rho = Uc.rho;
      const double kin2 = Uc.rhou * Uc.rhou + Uc.rhov * Uc.rhov;
      const Vec2 g_rho = raw_grad(*grads, 0, li);
      const Vec2 g_rhou = raw_grad(*grads, 1, li);
      const Vec2 g_rhov = raw_grad(*grads, 2, li);
      const Vec2 g_rhoE = raw_grad(*grads, 3, li);
      const Vec2 grad_ke = (g_rhou * Uc.rhou + g_rhov * Uc.rhov) / rho -
                           g_rho * (0.5 * kin2 / (rho * rho));
      const Vec2 grad_p = (g_rhoE - grad_ke) * (gas.gamma - 1.0);
      p += grad_p.x * dr.x + grad_p.y * dr.y;
      if (!(p > 0.0)) {
        p = pressure_from_cons(U_local[li], gas);  // positivity fallback
      }
    }

    // Pressure force on the body: dF = p * n dA, with n the outward area
    // vector of the fluid domain (pointing into the body).
    const double dFx = p * face.normal.x;
    const double dFy = p * face.normal.y;
    fx += dFx;
    fy += dFy;

    const double rx = face.centroid.x - cfg.moment_center.x;
    const double ry = face.centroid.y - cfg.moment_center.y;
    mz += rx * dFy - ry * dFx;  // z-component of r x F

    // Viscous wall shear (no-slip adiabatic walls in laminar runs): the
    // traction of the fluid on the body is dF = -tau . n dA (see viscous.h
    // for the sign convention), with the one-sided wall gradients
    // du/dn = (u_wall - u_cell) / dist, dv/dn = (v_wall - v_cell) / dist.
    if (viscous && mu > 0.0 &&
        face.bc_type == BCType::NoSlipAdiabaticWall) {
      const double dist = (face.centroid - lm.cells[li].centroid).norm();
      if (dist > 0.0) {
        const PrimState P = cons_to_prim(U_local[li], gas);
        const Vec2 n_hat = face.normal / face.area;
        const double gn_u = -P.u / dist;
        const double gn_v = -P.v / dist;
        const double du_dx = gn_u * n_hat.x;
        const double du_dy = gn_u * n_hat.y;
        const double dv_dx = gn_v * n_hat.x;
        const double dv_dy = gn_v * n_hat.y;
        const double div = du_dx + dv_dy;
        const double tau_xx = mu * (2.0 * du_dx - (2.0 / 3.0) * div);
        const double tau_yy = mu * (2.0 * dv_dy - (2.0 / 3.0) * div);
        const double tau_xy = mu * (du_dy + dv_dx);

        // The viscous force on a no-slip wall is the TANGENTIAL (skin
        // friction) component of the traction: project the traction vector
        // tau . n_hat onto the wall tangent before integrating. The normal
        // component of the traction is a pressure-like stress and must not
        // be lumped into the viscous (shear) force.
        const Vec2 traction = {tau_xx * n_hat.x + tau_xy * n_hat.y,
                               tau_xy * n_hat.x + tau_yy * n_hat.y};
        const Vec2 t_hat = {-n_hat.y, n_hat.x};
        const double shear = traction.x * t_hat.x + traction.y * t_hat.y;
        const double dFvx = -shear * t_hat.x * face.area;
        const double dFvy = -shear * t_hat.y * face.area;
        vfx += dFvx;
        vfy += dFvy;
        vmz += rx * dFvy - ry * dFvx;
      }
    }
  }

  const double q = dynamic_pressure(fs);
  const double force_denom = q * cfg.ref_area;
  const double moment_denom = q * cfg.ref_area * cfg.ref_length;

  // Drag/lift are defined relative to the FREESTREAM direction, not the
  // mesh axes: rotate the mesh-frame forces by the angle of attack.
  const double ca = std::cos(fs.aoa_rad);
  const double sa = std::sin(fs.aoa_rad);

  if (force_denom > 0.0) {
    stats.pressure_drag = (fx * ca + fy * sa) / force_denom;
    stats.pressure_lift = (-fx * sa + fy * ca) / force_denom;
    stats.viscous_drag = (vfx * ca + vfy * sa) / force_denom;
    stats.viscous_lift = (-vfx * sa + vfy * ca) / force_denom;
  } else {
    stats.pressure_drag = 0.0;
    stats.pressure_lift = 0.0;
    stats.viscous_drag = 0.0;
    stats.viscous_lift = 0.0;
  }
  stats.cd = stats.pressure_drag + stats.viscous_drag;
  stats.cl = stats.pressure_lift + stats.viscous_lift;
  // The z-component of r x F is invariant under rotation, so the moment
  // (pressure + viscous) needs no rotation.
  stats.cmz = moment_denom > 0.0 ? (mz + vmz) / moment_denom : 0.0;
}

// ---------------------------------------------------------------------------
// Global reductions
// ---------------------------------------------------------------------------

void compute_global_norms(const std::vector<ConsState>& R_local,
                          const SolverStats& local_stats,
                          SolverStats& global_stats, int n_owned_global) {
  LocalNorms ln;
  accumulate_local_norms(R_local, ln);

  double sum_sq[kConsDoubles];
  double max_abs[kConsDoubles];
  MPI_Allreduce(ln.sum_sq, sum_sq, kConsDoubles, MPI_DOUBLE, MPI_SUM,
                MPI_COMM_WORLD);
  MPI_Allreduce(ln.max_abs, max_abs, kConsDoubles, MPI_DOUBLE, MPI_MAX,
                MPI_COMM_WORLD);

  const double inv_n = n_owned_global > 0 ? 1.0 / n_owned_global : 0.0;
  global_stats.residual_rho = std::sqrt(sum_sq[0] * inv_n);
  global_stats.residual_rhou = std::sqrt(sum_sq[1] * inv_n);
  global_stats.residual_rhov = std::sqrt(sum_sq[2] * inv_n);
  global_stats.residual_rhoE = std::sqrt(sum_sq[3] * inv_n);
  global_stats.residual_l2 =
      std::max({global_stats.residual_rho, global_stats.residual_rhou,
                global_stats.residual_rhov, global_stats.residual_rhoE});
  global_stats.residual_linf =
      std::max({max_abs[0], max_abs[1], max_abs[2], max_abs[3]});

  // Force coefficients are linear sums sharing one global denominator, so
  // summing the local coefficients equals the global coefficient.
  double forces[7] = {local_stats.cd,   local_stats.cl,
                      local_stats.cmz,   local_stats.pressure_drag,
                      local_stats.viscous_drag, local_stats.pressure_lift,
                      local_stats.viscous_lift};
  double forces_global[7];
  MPI_Allreduce(forces, forces_global, 7, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  global_stats.cd = forces_global[0];
  global_stats.cl = forces_global[1];
  global_stats.cmz = forces_global[2];
  global_stats.pressure_drag = forces_global[3];
  global_stats.viscous_drag = forces_global[4];
  global_stats.pressure_lift = forces_global[5];
  global_stats.viscous_lift = forces_global[6];

  // Scalar bookkeeping (the residual/force fields above are reduced; the
  // inner-iteration aggregates are running per-rank values, finalized by the
  // summary reduction in run_steady_solver).
  global_stats.step = local_stats.step;
  global_stats.physical_time = local_stats.physical_time;
  global_stats.inner_iter = local_stats.inner_iter;
  global_stats.cfl = local_stats.cfl;
  global_stats.dt = local_stats.dt;
  global_stats.min_inner_its = local_stats.min_inner_its;
  global_stats.max_inner_its = local_stats.max_inner_its;
  global_stats.mean_inner_its = local_stats.mean_inner_its;
  global_stats.inner_target_misses = local_stats.inner_target_misses;
  global_stats.last_inner_residual_ratio =
      local_stats.last_inner_residual_ratio;
}

// ---------------------------------------------------------------------------
// Steady marching loop
// ---------------------------------------------------------------------------

int run_steady_solver(const RunConfig& cfg, const SolverConfig& scfg,
                      std::vector<ConsState>& U_local, const LocalMesh& lm,
                      HaloScratch& scratch, const std::string& out_dir,
                      SolverStats* final_stats) {
  const int rank = lm.rank;
  const int nranks = lm.nranks;
  const bool second_order = scfg.spatial_order >= 2;
  // The gradients (and the halo-exchanged ghost gradients) are needed both
  // for the second-order reconstruction and for the laminar viscous flux, so
  // they are computed whenever either is active.
  const bool need_gradients = second_order || scfg.viscous;

  if (rank == 0) {
    std::printf("\n=== Phase 4: Second-Order FV Solver (LU-SGS) ===\n");
    std::printf("Steady pseudo-time marching with matrix-free LU-SGS\n");
    if (cfg.run_type != "steady") {
      std::printf("note: case run_type='%s'; Phase 4 runs the steady loop "
                  "as a smoke test only\n",
                  cfg.run_type.c_str());
    }
    std::printf("max_steps=%d  residual_target=%.2f orders  spatial_order=%d"
                "  cfl %.3f -> %.3f over %d steps"
                "  inner [%d..%d] target %.3f  dissipation_scale=%.3f"
                "  viscosity mu=%.6e (%s)\n",
                scfg.max_steps, scfg.residual_reduction_target,
                scfg.spatial_order, scfg.cfl_initial, scfg.cfl_max,
                scfg.pseudo_cfl_ramp_steps,
                std::max(1, scfg.min_inner_iterations),
                std::max(1, scfg.max_inner_iterations),
                scfg.inner_residual_reduction_target,
                scfg.rusanov_dissipation_scale, scfg.mu,
                scfg.viscous ? "laminar" : "inviscid");
  }

  // Total owned cells over all ranks (L2 normalization).
  int n_owned_global = 0;
  {
    const int n_owned_local = lm.n_owned;
    MPI_Allreduce(&n_owned_local, &n_owned_global, 1, MPI_INT, MPI_SUM,
                  MPI_COMM_WORLD);
  }

  // CSV output (rank 0 only).
  const std::string residual_path =
      (std::filesystem::path(out_dir) / "residuals.csv").string();
  const std::string forces_path =
      (std::filesystem::path(out_dir) / "forces.csv").string();
  if (rank == 0) {
    try {
      std::filesystem::create_directories(out_dir);
      write_residual_header(residual_path);
      write_force_header(forces_path);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[rank %d] warning: cannot initialize CSV "
                           "output: %s\n",
                   rank, e.what());
    }
  }

  // Initial condition: freestream everywhere (unless the caller loaded a
  // restart state via scfg.restart); refresh the ghost layer.
  if (!scfg.restart) {
    const ConsState U_fs = freestream_to_cons(cfg.freestream, cfg.gas);
    for (ConsState& u : U_local) {
      u = U_fs;
    }
  }
  exchange_halo_data(U_local, lm, scratch);

  // Persistent per-step scratch (allocated once; reused across steps).
  std::vector<ConsState> R_local(lm.n_owned);
  std::vector<ConsState> U_prev(lm.n_owned);   // pre-step snapshot
  std::vector<ConsState> U_step(lm.n_owned);  // inner-iteration snapshot
  std::vector<ConsState> delta_U(lm.n_owned);
  // Per-step lagged boundary ghost states (one per local face; only the
  // boundary faces are filled). The implicit solver freezes the boundary
  // ghosts at the step-start state so the residual and its implicit
  // linearization stay consistent at boundary faces.
  std::vector<ConsState> boundary_states(lm.faces.size());
  std::vector<double> face_lambda(lm.faces.size());
  std::vector<double> diag(lm.n_owned);
  // Reverse Cuthill-McKee sweep ordering (computed once at setup).
  std::vector<int> sweep_order;
  std::vector<int> sweep_pos;
  compute_rcm_order(lm, sweep_order, sweep_pos);
  CellGradients grads;
  GhostGradients ghost_grads;
  Limiters limiters;
  grads.resize(static_cast<size_t>(lm.n_owned));
  ghost_grads.resize(static_cast<size_t>(lm.n_ghost));
  limiters.resize(static_cast<size_t>(lm.n_owned));
  GradientHaloScratch grad_scratch;
  grad_scratch.init(lm);

  // Reconstruction data pointers: valid whenever the gradients are needed
  // (second-order reconstruction and/or viscous terms); the explicit
  // second_order flag passed to compute_residual gates the reconstruction.
  const CellGradients* grads_p = need_gradients ? &grads : nullptr;
  const GhostGradients* ghost_p = need_gradients ? &ghost_grads : nullptr;
  const Limiters* limiters_p = need_gradients ? &limiters : nullptr;

  SolverStats local_stats;
  SolverStats global_stats;

  // CFL schedule from the case: ramp cfl_initial -> cfl_max over
  // pseudo_cfl_ramp_steps, then hold cfl_max.
  auto current_cfl = [&scfg](int step) {
    double cfl0 = scfg.cfl_initial;
    double cfl1 = std::max(scfg.cfl_max, cfl0);
    if (scfg.pseudo_cfl_ramp_steps <= 0 || step >= scfg.pseudo_cfl_ramp_steps) {
      return cfl1;
    }
    const double t =
        static_cast<double>(step - 1) / scfg.pseudo_cfl_ramp_steps;
    return cfl0 + (cfl1 - cfl0) * t;
  };

  // Refresh the lagged boundary ghost states from the current state (the
  // cell-center state is used; the boundary ghosts are lagged at the
  // step-start state so the residual and its implicit linearization stay
  // consistent at boundary faces). Called after update_reconstruction() so
  // the reconstruction data is fresh if a caller needs it.
  auto refresh_boundary_states = [&]() {
    for (size_t fi = 0; fi < lm.faces.size(); ++fi) {
      const LocalMesh::LocalFace& f = lm.faces[fi];
      if (f.right == -1) {
        boundary_states[fi] =
            boundary_state(U_local[f.left], f.bc_type, f.normal, cfg.freestream,
                           cfg.gas);
      }
    }
  };

  // Refresh the second-order reconstruction data at the current state.
  auto update_reconstruction = [&]() {
    compute_gradients(U_local, lm, grads);
    compute_limiters(U_local, lm, grads, limiters);
    // The halo exchange sends the LIMITED gradients (phi * grad per
    // variable), so ghost cells reconstruct with the same gradients their
    // owners use — the inter-rank faces are conservative.
    exchange_gradient_halo(
        grads.grad_rho, grads.grad_rhou, grads.grad_rhov, grads.grad_rhoE,
        limiters.phi_rho, limiters.phi_rhou, limiters.phi_rhov,
        limiters.phi_rhoE, ghost_grads.grad_rho, ghost_grads.grad_rhou,
        ghost_grads.grad_rhov, ghost_grads.grad_rhoE, lm, grad_scratch);
  };

  // Step-0 stats (residual and forces of the initial condition).
  if (need_gradients) {
    update_reconstruction();
  }
  refresh_boundary_states();
  compute_residual(U_local, R_local, lm, cfg.gas, cfg.freestream,
                   scfg.rusanov_dissipation_scale, second_order, grads_p,
                   ghost_p, limiters_p, scfg.mu, scfg.viscous,
                   &boundary_states);
  compute_forces(U_local, local_stats, lm, cfg.gas, cfg.freestream, cfg,
                 scfg.mu, scfg.viscous, need_gradients ? &grads : nullptr);
  local_stats.step = 0;
  local_stats.inner_iter = 1;
  local_stats.cfl = current_cfl(0);
  local_stats.dt = 0.0;  // no update performed yet
  compute_global_norms(R_local, local_stats, global_stats, n_owned_global);

  const double initial_residual = global_stats.residual_l2;
  if (rank == 0) {
    std::printf(" step     residual(L2)   orders     inner        cl"
                "             cd             cmz\n");
    print_stats_row(global_stats, 0.0);
  }

  bool converged = false;
  bool failed = false;
  double best_res = -1.0;
  int steps_since_improvement = 0;
  double cfl_multiplier = 1.0;  // persistent CFL scale (stall handling)
  int step = 0;
  int last_written_step = 0;
  const int print_every = std::max(1, scfg.max_steps / 20);

  // Inner-iteration aggregate statistics (running per-rank values).
  const int max_inner_eff = std::max(1, scfg.max_inner_iterations);
  const int min_inner_eff = std::max(1, scfg.min_inner_iterations);
  const double inner_target = scfg.inner_residual_reduction_target;
  int min_inner_all = 0;
  int max_inner_all = 0;
  double mean_inner_acc = 0.0;
  int n_steps_done = 0;
  int misses = 0;
  double last_ratio = 1.0;

  // Write the step-0 row.
  if (rank == 0) {
    write_residual_row(residual_path, global_stats);
    write_force_row(forces_path, global_stats);
  }

  while (step < scfg.max_steps && !converged && !failed) {
    ++step;

    // Pseudo-time step with the positivity safety net. EVERY rank runs the
    // same number of attempts: the invalid-cell count is reduced with an
    // unconditional MPI_Allreduce, and the rollback restores all owned cells
    // on all ranks.
    double cfl = current_cfl(step) * cfl_multiplier;
    double mean_dt = 0.0;
    int n_invalid_global = 0;
    int retries = 0;
    int step_inner = 0;
    double step_ratio = 1.0;
    do {
      // Snapshot the pre-update owned states for the rollback.
      for (int i = 0; i < lm.n_owned; ++i) {
        U_prev[i] = U_local[i];
      }

      // Fresh ghost states, then the lagged boundary ghosts, the
      // second-order reconstruction data, and the implicit operator at the
      // current state.
      exchange_halo_data(U_local, lm, scratch);
      if (need_gradients) {
        update_reconstruction();
      }
      refresh_boundary_states();

      compute_face_lambda(U_local, lm, cfg.gas, scfg.mu, scfg.viscous,
                          scfg.rusanov_dissipation_scale, face_lambda);
      compute_diagonal(lm, face_lambda, cfl, diag, mean_dt);

      // Residual at the start of the inner loop (also defines the reference
      // norm for the inner residual-reduction test).
      compute_residual(U_local, R_local, lm, cfg.gas, cfg.freestream,
                       scfg.rusanov_dissipation_scale, second_order, grads_p,
                       ghost_p, limiters_p, scfg.mu, scfg.viscous,
                       &boundary_states);
      const double r0 = local_max_norm(R_local);

      // Inner LU-SGS iterations: each iteration is one forward + one
      // backward sweep with a fresh delta_U, followed by a halo exchange and
      // a residual recomputation (nonlinear LU-SGS; the spatial operator and
      // the diagonal are frozen at the start of the step).
      int inner = 0;
      step_ratio = 1.0;
      double best_ratio = 1.0;
      for (inner = 1; inner <= max_inner_eff; ++inner) {
        // Snapshot the state at the start of this inner iteration: the
        // flux-difference coupling is measured against it.
        for (int i = 0; i < lm.n_owned; ++i) {
          U_step[i] = U_local[i];
        }
        std::fill(delta_U.begin(), delta_U.end(), ConsState{});
        lusgs_forward_sweep(R_local, delta_U, U_step, face_lambda, diag, lm,
                            cfg.gas, sweep_order, sweep_pos);
        lusgs_backward_sweep(U_local, U_step, delta_U, face_lambda, diag, lm,
                             cfg.gas, sweep_order, sweep_pos);

        exchange_halo_data(U_local, lm, scratch);
        compute_residual(U_local, R_local, lm, cfg.gas, cfg.freestream,
                         scfg.rusanov_dissipation_scale, second_order, grads_p,
                         ghost_p, limiters_p, scfg.mu, scfg.viscous,
                         &boundary_states);
        // The residual reduction ratio is rank-local; reduce it (max over
        // ranks) so every rank makes the SAME break decision. This keeps the
        // per-iteration halo exchanges symmetric across ranks (a rank-local
        // break would stop posting the matching Irecvs and deadlock).
        // Early exit if the update produced an invalid state: continuing
        // would feed NaN through the sweeps and the residual. The step is
        // rolled back by the safety net below.
        bool invalid_inner = false;
        for (int i = 0; i < lm.n_owned; ++i) {
          if (!state_is_valid(U_local[i], cfg.gas)) {
            invalid_inner = true;
            break;
          }
        }
        step_ratio = r0 > 0.0 ? local_max_norm(R_local) / r0 : 0.0;
        MPI_Allreduce(MPI_IN_PLACE, &step_ratio, 1, MPI_DOUBLE, MPI_MAX,
                      MPI_COMM_WORLD);
        // The invalid-state decision must be COLLECTIVE: a rank-local break
        // would stop this rank from posting the matching halo exchanges of
        // the next inner iteration and deadlock the run.
        int invalid_global = invalid_inner ? 1 : 0;
        MPI_Allreduce(&invalid_global, &invalid_global, 1, MPI_INT, MPI_MAX,
                      MPI_COMM_WORLD);
        if (invalid_global) {
          break;
        }
        const bool improved = step_ratio < 0.95 * best_ratio;
        if (step_ratio < best_ratio) {
          best_ratio = step_ratio;
        }
        if (inner >= min_inner_eff && inner_target > 0.0 &&
            step_ratio < inner_target) {
          break;  // target met
        }
        if (inner >= min_inner_eff && !improved && best_ratio < 0.99) {
          break;  // stagnation: the residual stopped decreasing
        }
      }
      // The loop counter is one past the last executed iteration when the
      // loop runs to completion; clamp to the effective maximum.
      step_inner = std::min(inner, max_inner_eff);

      // Positivity/finiteness safety net (identical decision on all ranks).
      int n_invalid = 0;
      for (int i = 0; i < lm.n_owned; ++i) {
        if (!state_is_valid(U_local[i], cfg.gas)) {
          ++n_invalid;
        }
      }
      MPI_Allreduce(&n_invalid, &n_invalid_global, 1, MPI_INT, MPI_MAX,
                    MPI_COMM_WORLD);

      if (n_invalid_global > 0) {
        // Roll the step back on ALL ranks — including ranks whose own cells
        // were valid, which must not keep a half-updated state while the
        // step is retried globally.
        for (int i = 0; i < lm.n_owned; ++i) {
          U_local[i] = U_prev[i];
        }
        if (rank == 0) {
          std::printf("warning: step %d: %d invalid cell state(s); "
                      "reducing CFL %.3f -> %.3f\n",
                      step, n_invalid_global, cfl, cfl * 0.5);
        }
        cfl = std::max(cfl * 0.5, kMinSafetyCfl);
        ++retries;
      }
    } while (n_invalid_global > 0 && retries < kMaxSafetyRetries);

    if (n_invalid_global > 0) {
      // Retries exhausted: the step is rejected. The state is the pre-update
      // state (no progress this step); restore a consistent residual for the
      // stats and record the step as a no-op.
      if (rank == 0) {
        std::printf("warning: step %d: %d invalid cell state(s) persist "
                    "after %d CFL-halving retries; step skipped "
                    "(state unchanged)\n",
                    step, n_invalid_global, retries);
      }
      mean_dt = 0.0;
      exchange_halo_data(U_local, lm, scratch);
      refresh_boundary_states();
      compute_residual(U_local, R_local, lm, cfg.gas, cfg.freestream,
                       scfg.rusanov_dissipation_scale, second_order, grads_p,
                       ghost_p, limiters_p, scfg.mu, scfg.viscous,
                       &boundary_states);
    }

    // Inner-iteration statistics for this step.
    if (n_steps_done == 0) {
      min_inner_all = step_inner;
    } else {
      min_inner_all = std::min(min_inner_all, step_inner);
    }
    max_inner_all = std::max(max_inner_all, step_inner);
    mean_inner_acc += step_inner;
    ++n_steps_done;
    if (inner_target > 0.0 && step_inner >= max_inner_eff &&
        step_ratio >= inner_target) {
      ++misses;  // ran out of inner iterations before meeting the target
    }
    if (std::isfinite(step_ratio)) {
      last_ratio = step_ratio;  // never record NaN/inf in the stats
    }
    local_stats.min_inner_its = min_inner_all;
    local_stats.max_inner_its = max_inner_all;
    local_stats.mean_inner_its =
        n_steps_done > 0 ? mean_inner_acc / n_steps_done : 0.0;
    local_stats.inner_target_misses = misses;
    local_stats.last_inner_residual_ratio = last_ratio;

    // Fresh ghost layer for the residual/force evaluation.
    exchange_halo_data(U_local, lm, scratch);

    compute_forces(U_local, local_stats, lm, cfg.gas, cfg.freestream, cfg,
                   scfg.mu, scfg.viscous, need_gradients ? &grads : nullptr);
    local_stats.step = step;
    local_stats.inner_iter = step_inner;
    local_stats.cfl = cfl;
    local_stats.dt = mean_dt;
    compute_global_norms(R_local, local_stats, global_stats, n_owned_global);

    // Failure check: the residual must stay finite (identical on all ranks).
    if (!(std::isfinite(global_stats.residual_l2) &&
          std::isfinite(global_stats.residual_linf))) {
      failed = true;
      if (rank == 0) {
        std::printf("error: non-finite residual at step %d\n", step);
      }
      break;
    }

    // CSV rows (rank 0).
    if (rank == 0 && (step % scfg.write_residuals_every == 0 ||
                      step % scfg.write_forces_every == 0)) {
      write_residual_row(residual_path, global_stats);
      write_force_row(forces_path, global_stats);
      last_written_step = step;
    }

    // Progress + convergence.
    const double orders = residual_orders(initial_residual,
                                          global_stats.residual_l2);
    if (rank == 0 && step % print_every == 0) {
      print_stats_row(global_stats, orders);
    }

    // Outer-loop stall detection: if the best residual has not improved for
    // a long window (e.g. a high-CFL limit cycle on a fine partition), halve
    // the CFL and let the iteration tighten. The window is evaluated against
    // the best residual seen so far so that a slow oscillation cannot evade
    // it.
    if (best_res < 0.0 || global_stats.residual_l2 < best_res) {
      best_res = global_stats.residual_l2;
      steps_since_improvement = 0;
    } else {
      ++steps_since_improvement;
    }
    constexpr int kStallWindow = 200;
    constexpr double kMinStallCfl = 1.0;
    if (steps_since_improvement >= kStallWindow && cfl > kMinStallCfl) {
      // The ramp function current_cfl(step) is recomputed at the top of
      // every step, so halving the step-local cfl would be undone by the
      // next step. Instead scale the ramp persistently: the multiplier
      // applies to every subsequent step until the residual improves.
      cfl_multiplier *= 0.5;
      steps_since_improvement = 0;
      best_res = global_stats.residual_l2;  // don't re-trigger immediately
      if (rank == 0) {
        std::printf("note: step %d: residual stalled (best %.3e); "
                    "reducing CFL multiplier to %.4f (cfl %.3f)\n",
                    step, best_res, cfl_multiplier,
                    current_cfl(step) * cfl_multiplier);
      }
    }
    // Gradually restore the multiplier when the residual improves markedly
    // (a 2x improvement over the best residual).
    if (global_stats.residual_l2 < best_res * 0.5) {
      cfl_multiplier = std::min(1.0, cfl_multiplier * 2.0);
    }

    if (scfg.residual_reduction_target > 0.0 &&
        orders >= scfg.residual_reduction_target) {
      converged = true;
    } else if (global_stats.residual_l2 <= 1e-14) {
      converged = true;  // machine-level residual
    }
  }

  // Make sure the final row is written even if the write cadence skipped it.
  if (rank == 0 && step > last_written_step && !failed) {
    write_residual_row(residual_path, global_stats);
    write_force_row(forces_path, global_stats);
  }

  // Finalize the inner-iteration aggregates across ranks.
  {
    int g_min = 0;
    int g_max = 0;
    double g_mean = 0.0;
    int g_misses = 0;
    double g_ratio = 1.0;
    const double mean_local =
        n_steps_done > 0 ? mean_inner_acc / n_steps_done : 0.0;
    MPI_Allreduce(&min_inner_all, &g_min, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&max_inner_all, &g_max, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&mean_local, &g_mean, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    // All ranks make the same inner-iteration break decisions (collective
    // ratio reduction), so the miss count is identical on every rank: use
    // MPI_MAX (a sum would multiply the count by the number of ranks).
    MPI_Allreduce(&misses, &g_misses, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&last_ratio, &g_ratio, 1, MPI_DOUBLE, MPI_MAX,
                  MPI_COMM_WORLD);
    global_stats.min_inner_its = g_min;
    global_stats.max_inner_its = g_max;
    global_stats.mean_inner_its = nranks > 0 ? g_mean / nranks : 0.0;
    global_stats.inner_target_misses = g_misses;
    global_stats.last_inner_residual_ratio = g_ratio;
  }

  const double orders =
      residual_orders(initial_residual, global_stats.residual_l2);

  if (rank == 0) {
    std::printf(" step     residual(L2)   orders     inner        cl"
                "             cd             cmz\n");
    print_stats_row(global_stats, orders);
    if (failed) {
      std::printf("Solver FAILED at step %d (non-finite state/residual)\n",
                  step);
    } else if (converged) {
      std::printf("Solver converged after %d steps: residual reduced by "
                  "%.2f orders\n",
                  step, orders);
    } else {
      std::printf("Solver stopped at max_steps (%d): residual reduced by "
                  "%.2f orders (not converged)\n",
                  step, orders);
    }
    std::printf("Final forces: cl = %.8e  cd = %.8e  cmz = %.8e  "
                "(pressure drag %.8e, viscous drag %.8e, "
                "pressure lift %.8e, viscous lift %.8e)\n",
                global_stats.cl, global_stats.cd, global_stats.cmz,
                global_stats.pressure_drag, global_stats.viscous_drag,
                global_stats.pressure_lift, global_stats.viscous_lift);
    std::printf("Inner iterations: min %d, max %d, mean %.2f, target "
                "misses %d, last residual ratio %.4e\n",
                global_stats.min_inner_its, global_stats.max_inner_its,
                global_stats.mean_inner_its,
                global_stats.inner_target_misses,
                global_stats.last_inner_residual_ratio);
    std::printf("Residuals written to '%s', forces to '%s'\n",
                residual_path.c_str(), forces_path.c_str());
  }

  // End-of-run summary fields (consumed by main for metadata.json /
  // run_status.json).
  global_stats.initial_residual_l2 = initial_residual;
  global_stats.residual_reduction_orders = orders;
  global_stats.converged = converged && !failed;
  if (final_stats != nullptr) {
    *final_stats = global_stats;
  }

  return failed ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Transient BDF2 marching loop (Phase 5)
// ---------------------------------------------------------------------------

void run_transient_solver(std::vector<ConsState>& U_local, const LocalMesh& lm,
                          const RunConfig& cfg, const SolverConfig& scfg,
                          const GasConfig& gas, const Freestream& fs,
                          HaloScratch& scratch, GradientHaloScratch& gscratch,
                          const std::string& out_dir, SolverStats* final_stats) {
  const int rank = lm.rank;
  const int nranks = lm.nranks;
  const bool second_order = scfg.spatial_order >= 2;
  // Gradients are needed for the second-order reconstruction and for the
  // laminar viscous flux (same criterion as the steady loop).
  const bool need_gradients = second_order || scfg.viscous;

  const double dt = cfg.time_step;
  if (!(dt > 0.0)) {
    if (rank == 0) {
      std::fprintf(stderr,
                   "error: transient run requires time_step > 0 "
                   "(case time_step = %.8g)\n",
                   dt);
    }
    return;
  }
  // Number of physical steps: final_time / time_step, rounded (not
  // truncated, so e.g. final_time=300, dt=0.01 gives exactly 30000 steps).
  int n_steps = static_cast<int>(std::lround(cfg.final_time / dt));
  if (n_steps <= 0) {
    if (rank == 0) {
      std::fprintf(stderr,
                   "error: transient run requires final_time >= time_step "
                   "(final_time = %.8g, time_step = %.8g)\n",
                   cfg.final_time, dt);
    }
    return;
  }
  // A positive max_steps caps the number of PHYSICAL steps (--max-steps
  // smoke tests); max_steps == 0 means uncapped: run to final_time.
  if (scfg.max_steps > 0 && n_steps > scfg.max_steps) {
    n_steps = scfg.max_steps;
  }

  if (rank == 0) {
    std::printf("\n=== Phase 5: Transient BDF2 Solver ===\n");
    std::printf("Physical-time marching: dt = %.8g, %d steps to t = %.8g\n",
                dt, n_steps, dt * n_steps);
    std::printf("First step: backward Euler (1st order); later steps: BDF2. "
                "Inner point-implicit iterations [%d..%d], target ratio %.3g "
                "on the total (spatial + physical-time) residual\n",
                std::max(1, scfg.min_inner_iterations),
                std::max(1, scfg.max_inner_iterations),
                scfg.inner_residual_reduction_target);
    // Make the step count visible immediately in stdout.log even when the
    // run is long (stdout is fully buffered when redirected to a file).
    std::fflush(stdout);
  }

  // Total owned cells over all ranks (residual L2 normalization).
  int n_owned_global = 0;
  {
    const int n_owned_local = lm.n_owned;
    MPI_Allreduce(&n_owned_local, &n_owned_global, 1, MPI_INT, MPI_SUM,
                  MPI_COMM_WORLD);
  }

  // CSV output (rank 0 only).
  const std::string residual_path =
      (std::filesystem::path(out_dir) / "residuals.csv").string();
  const std::string forces_path =
      (std::filesystem::path(out_dir) / "forces.csv").string();
  if (rank == 0) {
    try {
      std::filesystem::create_directories(out_dir);
      write_residual_header(residual_path);
      write_force_header(forces_path);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[rank %d] warning: cannot initialize CSV "
                           "output: %s\n",
                   rank, e.what());
    }
  }

  // Initial condition: freestream everywhere (unless the caller loaded a
  // restart state via scfg.restart); refresh the ghost layer.
  if (!scfg.restart) {
    const ConsState U_fs = freestream_to_cons(fs, gas);
    for (ConsState& u : U_local) {
      u = U_fs;
    }
  }
  exchange_halo_data(U_local, lm, scratch);

  // History states for BDF2: U_n = U at the previous physical step, U_nm1 =
  // U at two steps back. Both are FROZEN during the inner iterations of a
  // physical step and updated only after the inner solve converges.
  std::vector<ConsState> U_n(lm.n_owned);
  std::vector<ConsState> U_nm1;  // empty during the first physical step
  for (int i = 0; i < lm.n_owned; ++i) {
    U_n[i] = U_local[i];
  }

  // Per-step scratch (allocated once; reused across steps).
  std::vector<ConsState> R(lm.n_owned);
  std::vector<ConsState> U_prev(lm.n_owned);  // step-start snapshot (rollback)
  std::vector<double> face_lambda(lm.faces.size());
  std::vector<double> diag(lm.n_owned);
  CellGradients grads;
  GhostGradients ghost_grads;
  Limiters limiters;
  grads.resize(static_cast<size_t>(lm.n_owned));
  ghost_grads.resize(static_cast<size_t>(lm.n_ghost));
  limiters.resize(static_cast<size_t>(lm.n_owned));
  gscratch.init(lm);

  // Reconstruction data pointers: valid whenever the gradients are needed.
  const CellGradients* grads_p = need_gradients ? &grads : nullptr;
  const GhostGradients* ghost_p = need_gradients ? &ghost_grads : nullptr;
  const Limiters* limiters_p = need_gradients ? &limiters : nullptr;

  auto update_reconstruction = [&]() {
    compute_gradients(U_local, lm, grads);
    compute_limiters(U_local, lm, grads, limiters);
    // The halo exchange sends the LIMITED gradients (phi * grad per
    // variable), so ghost cells reconstruct with the same gradients their
    // owners use — the inter-rank faces are conservative.
    exchange_gradient_halo(
        grads.grad_rho, grads.grad_rhou, grads.grad_rhov, grads.grad_rhoE,
        limiters.phi_rho, limiters.phi_rhou, limiters.phi_rhov,
        limiters.phi_rhoE, ghost_grads.grad_rho, ghost_grads.grad_rhou,
        ghost_grads.grad_rhov, ghost_grads.grad_rhoE, lm, gscratch);
  };

  SolverStats local_stats;
  SolverStats global_stats;

  // Inner-iteration aggregate statistics (running per-rank values; identical
  // break decisions on all ranks via the reduced ratio).
  const int max_inner_eff = std::max(1, scfg.max_inner_iterations);
  const int min_inner_eff = std::max(1, scfg.min_inner_iterations);
  const double inner_target = scfg.inner_residual_reduction_target;
  int min_inner_all = 0;
  int max_inner_all = 0;
  double mean_inner_acc = 0.0;
  int n_steps_done = 0;
  int misses = 0;
  double last_ratio = 1.0;

  // Step-0 stats: residual and forces of the initial condition. The
  // physical-time term is zero at step 0 (U^{n+1} = U^n = IC), so the total
  // residual equals the spatial residual of the freestream state.
  exchange_halo_data(U_local, lm, scratch);
  if (need_gradients) {
    update_reconstruction();
  }
  compute_residual(U_local, R, lm, gas, fs, scfg.rusanov_dissipation_scale,
                   second_order, grads_p, ghost_p, limiters_p, scfg.mu,
                   scfg.viscous);
  compute_forces(U_local, local_stats, lm, gas, fs, cfg, scfg.mu, scfg.viscous,
                 need_gradients ? &grads : nullptr);
  local_stats.step = 0;
  local_stats.inner_iter = 0;
  local_stats.cfl = 0.0;
  local_stats.dt = 0.0;
  compute_global_norms(R, local_stats, global_stats, n_owned_global);

  const double initial_residual = global_stats.residual_l2;
  if (rank == 0) {
    std::printf(" step     residual(L2)   orders     inner        cl"
                "             cd             cmz\n");
    print_stats_row(global_stats, 0.0);
    write_residual_row(residual_path, global_stats);
    write_force_row(forces_path, global_stats);
  }

  double t = 0.0;
  bool failed = false;
  int failed_at_step = 0;
  int last_written_step = 0;
  const int print_every = std::max(1, n_steps / 20);

  for (int step = 1; step <= n_steps; ++step) {
    // BDF2 coefficients: backward Euler on the first physical step (no
    // U^{n-1} history yet), BDF2 afterwards.
    const double alpha0 = (step == 1) ? 1.0 : 1.5;
    const double alpha1 = (step == 1) ? -1.0 : -2.0;
    const double alpha2 = (step == 1) ? 0.0 : 0.5;

    // Physical-time step of THIS step with a positivity safety net: if the
    // inner iterations produce a non-physical state, the step is rolled back
    // to U^n and retried with a halved dt (the histories stay frozen; only
    // this step's increment changes). If the retries are exhausted the run
    // fails.
    double dt_eff = dt;
    int retries = 0;
    bool step_ok = false;
    double inner_ratio = 1.0;   // residual ratio of the accepted attempt
    int inner_iter = 0;         // updates applied by the accepted attempt
    double mean_eff_cfl = 0.0;
    while (!step_ok && retries < kMaxSafetyRetries) {
      // Snapshot the step-start owned states (U^n) for the rollback.
      for (int i = 0; i < lm.n_owned; ++i) {
        U_prev[i] = U_local[i];
      }

      // Add the physical-time source term of the CURRENT state against the
      // FROZEN histories to the spatial residual:
      //   R += V/dt * (alpha0*U^{n+1} + alpha1*U^n + alpha2*U^{n-1})
      auto add_physical_time_term = [&](std::vector<ConsState>& Rv) {
        for (int i = 0; i < lm.n_owned; ++i) {
          const double coeff = lm.cells[i].volume / dt_eff;
          for (int k = 0; k < kConsDoubles; ++k) {
            double phys_term =
                coeff * (alpha0 * comp_of(U_local[i], k) +
                         alpha1 * comp_of(U_n[i], k));
            if (!U_nm1.empty()) {
              phys_term += coeff * alpha2 * comp_of(U_nm1[i], k);
            }
            accum_comp(Rv[i], k, phys_term);
          }
        }
      };

      // Fresh ghosts + frozen reconstruction and implicit operator for this
      // attempt (the reconstruction data is NOT refreshed inside the inner
      // loop).
      exchange_halo_data(U_local, lm, scratch);
      if (need_gradients) {
        update_reconstruction();
      }
      compute_face_lambda(U_local, lm, gas, scfg.mu, scfg.viscous,
                          scfg.rusanov_dissipation_scale, face_lambda);

      // Point-implicit diagonal: D_i = alpha0*V_i/dt + 0.5*Lambda_i with
      // Lambda_i the cell spectral radius (sum of the face spectral radii).
      // The alpha0*V/dt term is the diagonal of the physical-time operator's
      // Jacobian contribution (alpha0 = 1 for backward Euler, 1.5 for BDF2),
      // keeping the implicit update consistent with the residual. Also track
      // the mean effective CFL dt*Lambda/V for the stats.
      double cfl_sum = 0.0;
      for (int i = 0; i < lm.n_owned; ++i) {
        double lam = 0.0;
        const int begin = lm.cell_faces_offsets[i];
        const int end = lm.cell_faces_offsets[i + 1];
        for (int k = begin; k < end; ++k) {
          lam += face_lambda[lm.cell_faces_data[k]];
        }
        const double vol = lm.cells[i].volume;
        diag[i] = (vol > 0.0) ? alpha0 * vol / dt_eff + 0.5 * lam : 1.0;
        if (lam > 0.0 && vol > 0.0) {
          cfl_sum += dt_eff * lam / vol;
        }
      }
      mean_eff_cfl = lm.n_owned > 0 ? cfl_sum / lm.n_owned : 0.0;

      // Inner iterations for U^{n+1}: each iteration is one halo exchange,
      // one total-residual evaluation, and one point-implicit update. The
      // convergence test compares the total residual norm against the first
      // iteration's norm; at least min_inner and at most max_inner
      // iterations are performed. All break decisions are collective (the
      // norm/ratio are MPI-reduced and the invalid-state test is
      // MPI-reduced), so every rank performs the same number of exchanges.
      double res0 = 1e300;
      inner_ratio = 1.0;
      double best_ratio = 1.0;
      int stagnation_count = 0;
      inner_iter = 0;
      for (; inner_iter < max_inner_eff; ++inner_iter) {
        exchange_halo_data(U_local, lm, scratch);

        // Spatial residual at the current state, then the physical-time term.
        compute_residual(U_local, R, lm, gas, fs,
                         scfg.rusanov_dissipation_scale, second_order, grads_p,
                         ghost_p, limiters_p, scfg.mu, scfg.viscous);
        add_physical_time_term(R);

        double res_norm = 0.0;
        for (int i = 0; i < lm.n_owned; ++i) {
          for (int k = 0; k < kConsDoubles; ++k) {
            const double r = comp_of(R[i], k);
            res_norm += r * r;
          }
        }
        MPI_Allreduce(MPI_IN_PLACE, &res_norm, 1, MPI_DOUBLE, MPI_SUM,
                      MPI_COMM_WORLD);
        res_norm = std::sqrt(
            res_norm / static_cast<double>(std::max(1, n_owned_global)));

        if (inner_iter == 0) {
          res0 = res_norm;
        }
        inner_ratio = (res0 > 0.0) ? res_norm / res0 : 0.0;

        if (inner_iter >= min_inner_eff && inner_target > 0.0 &&
            inner_ratio < inner_target) {
          break;  // inner convergence target met
        }
        // Stagnation break (after the minimum iteration count): the ratio
        // must improve by at least 1% over the best ratio seen in this
        // physical step; 3 consecutive non-improving iterations stop the
        // loop early (identical decision on all ranks — the ratio is
        // MPI-reduced).
        if (inner_iter >= min_inner_eff) {
          if (inner_ratio < best_ratio * 0.99) {
            best_ratio = inner_ratio;
            stagnation_count = 0;
          } else {
            ++stagnation_count;
            if (stagnation_count >= 3) {
              break;
            }
          }
        }

        // Point-implicit update: U^{n+1,k+1} = U^{n+1,k} - R / D.
        for (int i = 0; i < lm.n_owned; ++i) {
          const double inv_d = 1.0 / diag[i];
          U_local[i].rho -= R[i].rho * inv_d;
          U_local[i].rhou -= R[i].rhou * inv_d;
          U_local[i].rhov -= R[i].rhov * inv_d;
          U_local[i].rhoE -= R[i].rhoE * inv_d;
        }

        // Positivity/finiteness check (COLLECTIVE so every rank breaks
        // together and keeps posting the matching halo exchanges).
        int n_invalid = 0;
        for (int i = 0; i < lm.n_owned; ++i) {
          if (!state_is_valid(U_local[i], gas)) {
            ++n_invalid;
          }
        }
        MPI_Allreduce(&n_invalid, &n_invalid, 1, MPI_INT, MPI_MAX,
                      MPI_COMM_WORLD);
        if (n_invalid > 0) {
          break;  // non-physical state; the step-level check retries
        }
      }
      // `inner_iter` is the number of updates applied to U_local by this
      // attempt (each non-breaking iteration applies exactly one).

      // Step-level positivity check (collective): roll the attempt back and
      // retry the whole physical step with a halved dt.
      int n_invalid = 0;
      for (int i = 0; i < lm.n_owned; ++i) {
        if (!state_is_valid(U_local[i], gas)) {
          ++n_invalid;
        }
      }
      MPI_Allreduce(&n_invalid, &n_invalid, 1, MPI_INT, MPI_MAX,
                    MPI_COMM_WORLD);
      if (n_invalid > 0) {
        for (int i = 0; i < lm.n_owned; ++i) {
          U_local[i] = U_prev[i];
        }
        if (rank == 0) {
          std::printf("warning: physical step %d: %d invalid cell state(s); "
                      "reducing dt %.6g -> %.6g\n",
                      step, n_invalid, dt_eff, dt_eff * 0.5);
        }
        dt_eff *= 0.5;
        ++retries;
        continue;
      }

      // ---- Residual of the ACCEPTED state: the inner loop's last R is
      // stale by one update when the loop ran to completion, so recompute it
      // with fresh ghosts and a fresh reconstruction (the forces and the CSV
      // residual row must correspond to the state that is kept).
      exchange_halo_data(U_local, lm, scratch);
      if (need_gradients) {
        update_reconstruction();
      }
      compute_residual(U_local, R, lm, gas, fs, scfg.rusanov_dissipation_scale,
                       second_order, grads_p, ghost_p, limiters_p, scfg.mu,
                       scfg.viscous);
      add_physical_time_term(R);
      step_ok = true;
    }

    if (!step_ok) {
      // Retries exhausted: the step is rejected and the run fails. The
      // state is restored to the pre-step state (no progress on this step).
      if (rank == 0) {
        std::printf("warning: physical step %d: invalid cell states persist "
                    "after %d dt-halving retries; run failed\n",
                    step, retries);
      }
      for (int i = 0; i < lm.n_owned; ++i) {
        U_local[i] = U_prev[i];
      }
      failed = true;
      failed_at_step = step;
      break;
    }

    // ---- History update: once per physical step, after the inner solve.
    if (step == 1) {
      U_nm1.assign(static_cast<size_t>(lm.n_owned), ConsState{});
    }
    for (int i = 0; i < lm.n_owned; ++i) {
      U_nm1[i] = U_n[i];    // shift: U^{n-1} <- U^n
      U_n[i] = U_local[i];  // U^n <- U^{n+1}
    }

    // ---- Advance physical time.
    t += dt_eff;

    // ---- Forces + stats of this physical step.
    compute_forces(U_local, local_stats, lm, gas, fs, cfg, scfg.mu,
                   scfg.viscous, need_gradients ? &grads : nullptr);
    local_stats.step = step;
    local_stats.physical_time = t;
    local_stats.inner_iter = inner_iter;
    local_stats.cfl = mean_eff_cfl;
    local_stats.dt = dt_eff;

    // Inner-iteration statistics for this step, aggregated BEFORE the
    // global reduction so the CURRENT step's values appear in this step's
    // CSV row (not lagged by one step).
    if (n_steps_done == 0) {
      min_inner_all = inner_iter;
    } else {
      min_inner_all = std::min(min_inner_all, inner_iter);
    }
    max_inner_all = std::max(max_inner_all, inner_iter);
    mean_inner_acc += inner_iter;
    ++n_steps_done;
    if (inner_target > 0.0 && inner_iter >= max_inner_eff &&
        inner_ratio >= inner_target) {
      ++misses;  // ran out of inner iterations before meeting the target
    }
    if (std::isfinite(inner_ratio)) {
      last_ratio = inner_ratio;  // never record NaN/inf in the stats
    }
    local_stats.min_inner_its = min_inner_all;
    local_stats.max_inner_its = max_inner_all;
    local_stats.mean_inner_its =
        n_steps_done > 0 ? mean_inner_acc / n_steps_done : 0.0;
    local_stats.inner_target_misses = misses;
    local_stats.last_inner_residual_ratio = last_ratio;

    compute_global_norms(R, local_stats, global_stats, n_owned_global);

    // Failure check (identical on all ranks).
    if (!(std::isfinite(global_stats.residual_l2) &&
          std::isfinite(global_stats.residual_linf))) {
      failed = true;
      failed_at_step = step;
      if (rank == 0) {
        std::printf("error: non-finite residual at physical step %d\n", step);
      }
      break;
    }

    // CSV rows (rank 0).
    if (rank == 0 && (step % scfg.write_residuals_every == 0 ||
                      step % scfg.write_forces_every == 0)) {
      write_residual_row(residual_path, global_stats);
      write_force_row(forces_path, global_stats);
      last_written_step = step;
    }

    // Progress.
    if (rank == 0 && step % print_every == 0) {
      const double orders =
          residual_orders(initial_residual, global_stats.residual_l2);
      print_stats_row(global_stats, orders);
    }
  }

  // Make sure the final row is written even if the write cadence skipped it.
  if (rank == 0 && global_stats.step > last_written_step && !failed) {
    write_residual_row(residual_path, global_stats);
    write_force_row(forces_path, global_stats);
  }

  // Finalize the inner-iteration aggregates across ranks (all ranks make the
  // same inner-loop decisions, so the counts are identical; MPI_MAX keeps
  // them from being multiplied by the number of ranks).
  {
    int g_min = 0;
    int g_max = 0;
    int g_misses = 0;
    double g_mean = 0.0;
    double g_ratio = 1.0;
    const double mean_local =
        n_steps_done > 0 ? mean_inner_acc / n_steps_done : 0.0;
    MPI_Allreduce(&min_inner_all, &g_min, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&max_inner_all, &g_max, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&mean_local, &g_mean, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&misses, &g_misses, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&last_ratio, &g_ratio, 1, MPI_DOUBLE, MPI_MAX,
                  MPI_COMM_WORLD);
    global_stats.min_inner_its = g_min;
    global_stats.max_inner_its = g_max;
    global_stats.mean_inner_its = nranks > 0 ? g_mean / nranks : 0.0;
    global_stats.inner_target_misses = g_misses;
    global_stats.last_inner_residual_ratio = g_ratio;
  }

  const double orders =
      residual_orders(initial_residual, global_stats.residual_l2);

  // End-of-run summary fields (consumed by main for metadata.json /
  // run_status.json).
  global_stats.initial_residual_l2 = initial_residual;
  global_stats.residual_reduction_orders = orders;
  global_stats.converged = !failed;

  if (rank == 0) {
    std::printf(" step     residual(L2)   orders     inner        cl"
                "             cd             cmz\n");
    print_stats_row(global_stats, orders);
    if (failed) {
      std::printf("Solver FAILED at physical step %d (non-finite "
                  "state/residual or exhausted dt-halving retries)\n",
                  failed_at_step);
    } else {
      std::printf("Transient solve complete: %d physical steps to t = %.8g\n",
                  n_steps, t);
    }
    std::printf("Final forces: cl = %.8e  cd = %.8e  cmz = %.8e  "
                "(pressure drag %.8e, viscous drag %.8e, "
                "pressure lift %.8e, viscous lift %.8e)\n",
                global_stats.cl, global_stats.cd, global_stats.cmz,
                global_stats.pressure_drag, global_stats.viscous_drag,
                global_stats.pressure_lift, global_stats.viscous_lift);
    std::printf("Inner iterations: min %d, max %d, mean %.2f, target "
                "misses %d, last residual ratio %.4e\n",
                global_stats.min_inner_its, global_stats.max_inner_its,
                global_stats.mean_inner_its,
                global_stats.inner_target_misses,
                global_stats.last_inner_residual_ratio);
    std::printf("Residuals written to '%s', forces to '%s'\n",
                residual_path.c_str(), forces_path.c_str());
  }

  if (final_stats != nullptr) {
    *final_stats = global_stats;
  }
}

}  // namespace cfd
