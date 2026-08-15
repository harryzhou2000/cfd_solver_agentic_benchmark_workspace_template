#pragma once

#include <cmath>

#include "common.hpp"
#include "config.hpp"
#include "mesh.hpp"

namespace cfd {

// Gradient of primitive variables (rho, u, v, p) at a cell.
struct PrimGrad {
  double gr[2], gu[2], gv[2], gp[2];
  double& operator[](int i) { return (&gr[0])[i]; }
  const double& operator[](int i) const { return (&gr[0])[i]; }
};

// Barth-Jespersen limiters per primitive at a cell.
struct Limiters {
  double lr, lu, lv, lp;
  double& operator[](int i) { return (&lr)[i]; }
  const double& operator[](int i) const { return (&lr)[i]; }
};

// ---------------------------------------------------------------------------
// Gas model: calorically perfect, gamma = const, R = const.
// ---------------------------------------------------------------------------
struct GasState {
  double rho, u, v, p, T, e, a, M;
};

// Conservative -> primitive.
inline GasState cons2prim(const Vec4& U, double gamma) {
  GasState gs;
  gs.rho = U[0];
  double inv_rho = 1.0 / U[0];
  gs.u = U[1] * inv_rho;
  gs.v = U[2] * inv_rho;
  double ke = 0.5 * (gs.u * gs.u + gs.v * gs.v);
  double rhoE = U[3];
  gs.e = rhoE * inv_rho - ke;
  gs.p = (gamma - 1.0) * U[0] * gs.e;
  gs.T = gs.p / (gs.rho);  // R=1 in nondim
  gs.a = std::sqrt(gamma * gs.p * inv_rho);
  gs.M = std::sqrt(gs.u * gs.u + gs.v * gs.v) / gs.a;
  return gs;
}

// Primitive -> conservative.
inline Vec4 prim2cons(double rho, double u, double v, double p, double gamma) {
  double ke = 0.5 * (u * u + v * v);
  double e = p / ((gamma - 1.0) * rho);
  double rhoE = rho * (e + ke);
  return {rho, rho * u, rho * v, rhoE};
}

// ---------------------------------------------------------------------------
// Inviscid flux (Rusanov / local Lax-Friedrichs)
// ---------------------------------------------------------------------------
Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, double nx, double ny,
                  double gamma, double scale = 1.0);

// Roe flux with Harten-Yee entropy fix (eps = 0.1 default).
Vec4 roe_flux(const Vec4& UL, const Vec4& UR, double nx, double ny,
              double gamma, double eps = 0.1);

// ---------------------------------------------------------------------------
// Viscous flux at a face given left/right gradients.
// Returns the viscous flux vector for the face.
// ---------------------------------------------------------------------------
Vec4 viscous_flux_face(const GasState& gs_L, const GasState& gs_R,
                       double grad_u_L[2], double grad_u_R[2],
                       double grad_v_L[2], double grad_v_R[2],
                       double grad_T_L[2], double grad_T_R[2],
                       double nx, double ny, double area,
                       double gamma, double mu, double Pr);

// ---------------------------------------------------------------------------
// Least-squares reconstruction coefficients (precomputed per cell).
// ---------------------------------------------------------------------------
struct LSQCoef {
  double cx = 0.0, cy = 0.0;
  double det = 0.0;
  double cxx = 0.0, cxy = 0.0, cyy = 0.0;
  bool valid = false;
  // Per-neighbor weight array (indexed by local cell id)
  std::vector<double> w_dx, w_dy;
  std::vector<int> nbrs;  // neighbor cell local ids (owned or ghost)
};

void compute_lsq_coefs(LocalMesh& mesh, std::vector<LSQCoef>& coefs);

// Gradients of primitives for owned cells; ghost slots are filled by
// exchange_gradients.
void compute_gradients(const LocalMesh& mesh, const std::vector<Vec4>& U,
                       const std::vector<LSQCoef>& coefs, double gamma,
                       std::vector<PrimGrad>& grads);

void exchange_gradients(const LocalMesh& mesh, std::vector<PrimGrad>& grads);

// Barth-Jespersen limiter for owned cells; ghost slots via exchange_limiters.
void compute_limiter(const LocalMesh& mesh, const std::vector<Vec4>& U,
                     const std::vector<PrimGrad>& grads, std::vector<Limiters>& lmt,
                     double gamma);

void exchange_limiters(const LocalMesh& mesh, std::vector<Limiters>& lmt);

// ---------------------------------------------------------------------------
// Residual assembly: adds inviscid + viscous + boundary contributions.
// ---------------------------------------------------------------------------
void assemble_residual(const LocalMesh& mesh, const std::vector<Vec4>& U,
                       const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                       const CaseConfig& cfg, bool viscous, std::vector<Vec4>& res,
                       double* res_l2, double* res_linf);

// ---------------------------------------------------------------------------
// Halo exchange: fill U_ghost from owned cells of neighbor ranks.
// ---------------------------------------------------------------------------
void exchange_halo(const LocalMesh& mesh, std::vector<Vec4>& U);

// ---------------------------------------------------------------------------
// LU-SGS sweep on the rank-local cell set (forward + backward).
// ---------------------------------------------------------------------------
void lusgs_sweep(const LocalMesh& mesh, const std::vector<Vec4>& U, std::vector<Vec4>& dU,
                 const std::vector<Vec4>& res, const std::vector<double>& D,
                 const CaseConfig& cfg);

// Nonlinear symmetric Gauss-Seidel relaxation (point-implicit): recomputes the
// local residual with the latest neighbor states and updates each cell in
// place. Robust for any CFL; used as an alternative inner solver.
void sgs_sweep(const LocalMesh& mesh, std::vector<Vec4>& U,
               const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
               const CaseConfig& cfg, bool viscous, const std::vector<double>& D,
               const std::vector<Vec4>* extra = nullptr);
void sgs_sweep_transient(const LocalMesh& mesh, std::vector<Vec4>& U,
                         const std::vector<Vec4>& Un, const std::vector<Vec4>& Unm1,
                         const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                         const CaseConfig& cfg, double dt, int ps, const std::vector<double>& D);

// ---------------------------------------------------------------------------
// Compute spectral radius for pseudo-time step (local).
// ---------------------------------------------------------------------------
double compute_dt(const LocalMesh& mesh, const std::vector<Vec4>& U,
                  const CaseConfig& cfg, bool viscous, double cfl);

// Diagonal D_i = Vol_i / dt_pseudo,i for the LU-SGS relaxation.
void compute_diagonal(const LocalMesh& mesh, const std::vector<Vec4>& U,
                      const CaseConfig& cfg, bool viscous, double cfl,
                      std::vector<double>& D);

// ---------------------------------------------------------------------------
// Compute forces on wall boundaries.
// ---------------------------------------------------------------------------
void compute_forces(const LocalMesh& mesh, const std::vector<Vec4>& U,
                    const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                    const CaseConfig& cfg, bool viscous, double mu, double& cl, double& cd,
                    double& cmz, double& pressure_drag, double& viscous_drag,
                    double& pressure_lift, double& viscous_lift);

// Surface wall data for surface.csv: gathered text produced by the driver.
std::string wall_surface_rows(const LocalMesh& mesh, const std::vector<Vec4>& U,
                              const std::vector<PrimGrad>& grads,
                              const std::vector<Limiters>& lmt, const CaseConfig& cfg,
                              bool viscous, double mu);

}  // namespace cfd
