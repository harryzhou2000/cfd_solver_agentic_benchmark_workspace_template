#include "numerics/SpatialOperator.hpp"

#include <algorithm>
#include <cmath>

#include "core/Exception.hpp"
#include "core/Log.hpp"
#include "numerics/Limiter.hpp"

namespace cfd {
SpatialOperator::SpatialOperator(const LocalMesh& mesh, const CaseConfig& cfg,
                                 const SolverOptions& opt, MPI_Comm comm)
    : mesh_(mesh), cfg_(cfg), opt_(opt), comm_(comm),
      gas_(cfg.gas.gamma, cfg.gas.R, cfg.gas.prandtl) {
  const Real mu = cfg_.molecularViscosity();
  const TransportModel::Law law = (cfg_.viscosity_model == "sutherland")
                                      ? TransportModel::Law::kSutherland
                                      : TransportModel::Law::kConstant;
  transport_ = TransportModel(law, mu, cfg_.freestreamTemperature(), 110.4);

  flux_opt_.scheme = opt_.flux;
  flux_opt_.entropy_fix = opt_.entropy_fix;
  flux_opt_.dissipation_scale = cfg_.run.rusanov_dissipation_scale;

  halo_.setup(mesh_, comm_);

  // Resolve the case boundary-condition mapping against the mesh families.
  patch_bc_.resize(mesh_.patch_names.size());
  for (std::size_t p = 0; p < mesh_.patch_names.size(); ++p) {
    auto it = cfg_.boundary_conditions.find(mesh_.patch_names[p]);
    CFD_CHECK(it != cfg_.boundary_conditions.end(),
              "mesh boundary family '" << mesh_.patch_names[p]
              << "' has no entry in the case boundary_conditions map");
    patch_bc_[p] = it->second;
  }
  for (const auto& kv : cfg_.boundary_conditions) {
    const bool found = std::find(mesh_.patch_names.begin(), mesh_.patch_names.end(), kv.first) !=
                       mesh_.patch_names.end();
    CFD_CHECK(found, "case maps boundary family '" << kv.first
              << "' which does not exist in the mesh");
  }

  uinf_ = cfg_.freestreamConservative();
  winf_ = gas_.toPrimitive(uinf_);
  rho_floor_ = opt_.positivity_floor * cfg_.freestream.rho;
  p_floor_ = opt_.positivity_floor * cfg_.freestream.pressure;

  const Real ainf = cfg_.freestreamSoundSpeed();
  const Real rinf = cfg_.freestream.rho;
  res_scale_ = {rinf, rinf * ainf, rinf * ainf, rinf * ainf * ainf};
  // Reference magnitudes of the primitive variables; the Venkatakrishnan
  // smoothing length is dimensional, so epsilon^2 is scaled per variable.
  var_scale_ = {rinf, ainf, ainf, cfg_.freestream.pressure};
  global_cells_ = mesh_.global_num_cells;
  {
    Real vol = 0.0;
    for (Index c = 0; c < mesh.num_owned; ++c) vol += mesh.cell_volume[c];
    global_volume_ = globalSum(vol, comm_);
  }

  const std::size_t nt = static_cast<std::size_t>(mesh_.numTotalCells());
  u_.assign(nt * kNVar, 0.0);
  w_.assign(nt * kNVar, 0.0);
  grad_.assign(nt * kNVar * kDim, 0.0);
  phi_.assign(nt * kNVar, 1.0);
  res_.assign(nt * kNVar, 0.0);
  dtau_.assign(nt, 0.0);
  diag_.assign(nt, 0.0);
  face_lambda_.assign(static_cast<std::size_t>(mesh_.numFaces()), 0.0);
  bface_lambda_.assign(static_cast<std::size_t>(mesh_.numBoundaryFaces()), 0.0);
  bstate_.assign(static_cast<std::size_t>(mesh_.numBoundaryFaces()) * kNVar, 0.0);
  bshear_.assign(static_cast<std::size_t>(mesh_.numBoundaryFaces()) * kDim, 0.0);

  length_scale_.resize(nt);
  for (Index c = 0; c < mesh_.numTotalCells(); ++c)
    length_scale_[c] = std::sqrt(mesh_.cell_volume[c]);

  buildLeastSquares();
}

void SpatialOperator::buildLeastSquares() {
  const Index no = mesh_.num_owned;
  // Cell -> stencil entries.  Interior neighbours are stored as the local cell
  // index; boundary faces as -(bface+1) so that both kinds share one CSR array.
  struct StencilEntry { Index nb; Vec2 d; Vec2 xf; };
  std::vector<std::vector<StencilEntry>> stencil(static_cast<std::size_t>(no));
  for (Index f = 0; f < mesh_.numFaces(); ++f) {
    const Index l = mesh_.face_l[f], r = mesh_.face_r[f];
    if (l < no) stencil[l].push_back({r, mesh_.cell_center[r] - mesh_.cell_center[l],
                                      mesh_.face_center[f]});
    if (r < no) stencil[r].push_back({l, mesh_.cell_center[l] - mesh_.cell_center[r],
                                      mesh_.face_center[f]});
  }
  for (Index b = 0; b < mesh_.numBoundaryFaces(); ++b) {
    const Index c = mesh_.bface_cell[b];
    stencil[c].push_back({static_cast<Index>(-(b + 1)),
                          mesh_.bface_center[b] - mesh_.cell_center[c], mesh_.bface_center[b]});
  }

  lsq_off_.assign(static_cast<std::size_t>(no) + 1, 0);
  for (Index c = 0; c < no; ++c) lsq_off_[c + 1] = lsq_off_[c] + static_cast<Index>(stencil[c].size());
  lsq_nb_.resize(lsq_off_[no]);
  lsq_wd_.resize(2 * static_cast<std::size_t>(lsq_off_[no]));
  lsq_xf_.resize(2 * static_cast<std::size_t>(lsq_off_[no]));
  lsq_inv_.assign(3 * static_cast<std::size_t>(no), 0.0);

  for (Index c = 0; c < no; ++c) {
    Real a11 = 0.0, a12 = 0.0, a22 = 0.0;
    Index k = lsq_off_[c];
    for (const auto& s : stencil[c]) {
      const Vec2& d = s.d;
      const Real d2 = dot(d, d);
      CFD_CHECK(d2 > 0.0, "coincident cell centres in the least-squares stencil of cell " << c);
      const Real wgt = 1.0 / d2;   // inverse-distance-squared weighting
      lsq_nb_[k] = s.nb;
      lsq_wd_[2 * k] = wgt * d[0];
      lsq_wd_[2 * k + 1] = wgt * d[1];
      lsq_xf_[2 * k] = s.xf[0];
      lsq_xf_[2 * k + 1] = s.xf[1];
      a11 += wgt * d[0] * d[0];
      a12 += wgt * d[0] * d[1];
      a22 += wgt * d[1] * d[1];
      ++k;
    }
    Real det = a11 * a22 - a12 * a12;
    if (!(std::abs(det) > 0.0)) {
      // Degenerate (collinear) stencil: fall back to a Tikhonov-regularised
      // inverse so the gradient stays bounded instead of producing NaNs.
      const Real reg = 1.0e-12 * (a11 + a22 + 1.0);
      a11 += reg; a22 += reg;
      det = a11 * a22 - a12 * a12;
    }
    lsq_inv_[3 * c + 0] = a22 / det;
    lsq_inv_[3 * c + 1] = -a12 / det;
    lsq_inv_[3 * c + 2] = a11 / det;
  }
}

void SpatialOperator::initializeFreestream() {
  for (Index c = 0; c < mesh_.numTotalCells(); ++c)
    for (int k = 0; k < kNVar; ++k) u_[c * kNVar + k] = uinf_[k];
  updatePrimitives();
}

void SpatialOperator::setConservative(const std::vector<Real>& u_owned) {
  CFD_CHECK(u_owned.size() >= static_cast<std::size_t>(mesh_.num_owned) * kNVar,
            "restart state has the wrong size");
  std::copy(u_owned.begin(), u_owned.begin() + static_cast<std::size_t>(mesh_.num_owned) * kNVar,
            u_.begin());
  syncState();
}

void SpatialOperator::updatePrimitives() {
  for (Index c = 0; c < mesh_.numTotalCells(); ++c) {
    ConsVec uc{};
    for (int k = 0; k < kNVar; ++k) uc[k] = u_[c * kNVar + k];
    PrimVec wc = gas_.toPrimitive(uc);
    wc[0] = std::max(wc[0], rho_floor_);
    wc[3] = std::max(wc[3], p_floor_);
    for (int k = 0; k < kNVar; ++k) w_[c * kNVar + k] = wc[k];
  }
}

void SpatialOperator::syncState() {
  halo_.exchange(u_.data(), kNVar);
  updatePrimitives();
}

void SpatialOperator::computeBoundaryStates() {
  for (Index b = 0; b < mesh_.numBoundaryFaces(); ++b) {
    const Index c = mesh_.bface_cell[b];
    PrimVec wi{};
    for (int k = 0; k < kNVar; ++k) wi[k] = w_[c * kNVar + k];
    const BcType bc = patch_bc_[mesh_.bface_patch[b]];
    const PrimVec wb = boundaryValue(bc, wi, mesh_.bface_normal[b], winf_, gas_);
    for (int k = 0; k < kNVar; ++k) bstate_[b * kNVar + k] = wb[k];
  }
}

void SpatialOperator::computeGradients() {
  const Index no = mesh_.num_owned;
  std::fill(grad_.begin(), grad_.end(), 0.0);
  for (Index c = 0; c < no; ++c) {
    std::array<Real, kNVar> bx{}, by{};
    const Real* wc = &w_[c * kNVar];
    for (Index k = lsq_off_[c]; k < lsq_off_[c + 1]; ++k) {
      const Index nb = lsq_nb_[k];
      const Real* wn = (nb >= 0) ? &w_[nb * kNVar] : &bstate_[(-nb - 1) * kNVar];
      const Real wdx = lsq_wd_[2 * k], wdy = lsq_wd_[2 * k + 1];
      for (int v = 0; v < kNVar; ++v) {
        const Real dw = wn[v] - wc[v];
        bx[v] += wdx * dw;
        by[v] += wdy * dw;
      }
    }
    const Real i11 = lsq_inv_[3 * c], i12 = lsq_inv_[3 * c + 1], i22 = lsq_inv_[3 * c + 2];
    for (int v = 0; v < kNVar; ++v) {
      grad_[(c * kNVar + v) * kDim + 0] = i11 * bx[v] + i12 * by[v];
      grad_[(c * kNVar + v) * kDim + 1] = i12 * bx[v] + i22 * by[v];
    }
  }
}

void SpatialOperator::computeLimiter() {
  const Index no = mesh_.num_owned;
  if (limiter_frozen_) return;   // keep the limiter values of the freeze step
  if (!opt_.second_order || opt_.limiter == LimiterType::kNone) {
    std::fill(phi_.begin(), phi_.end(), opt_.second_order ? 1.0 : 0.0);
    return;
  }
  const Real kv = opt_.venkatakrishnan_k;
  for (Index c = 0; c < no; ++c) {
    const Real* wc = &w_[c * kNVar];
    std::array<Real, kNVar> wmin{}, wmax{};
    for (int v = 0; v < kNVar; ++v) { wmin[v] = wc[v]; wmax[v] = wc[v]; }
    for (Index k = lsq_off_[c]; k < lsq_off_[c + 1]; ++k) {
      const Index nb = lsq_nb_[k];
      const Real* wn = (nb >= 0) ? &w_[nb * kNVar] : &bstate_[(-nb - 1) * kNVar];
      for (int v = 0; v < kNVar; ++v) {
        wmin[v] = std::min(wmin[v], wn[v]);
        wmax[v] = std::max(wmax[v], wn[v]);
      }
    }
    const Real h = length_scale_[c];
    const Real eps_base = (opt_.limiter == LimiterType::kVenkatakrishnan)
                              ? (kv * h) * (kv * h) * (kv * h)
                              : 0.0;
    std::array<Real, kNVar> eps2{};
    for (int v = 0; v < kNVar; ++v) eps2[v] = eps_base * var_scale_[v] * var_scale_[v];
    std::array<Real, kNVar> phi{};
    for (int v = 0; v < kNVar; ++v) phi[v] = 1.0;

    auto apply = [&](const Vec2& xf) {
      const Vec2 d = xf - mesh_.cell_center[c];
      for (int v = 0; v < kNVar; ++v) {
        const Real dm = grad_[(c * kNVar + v) * kDim + 0] * d[0] +
                        grad_[(c * kNVar + v) * kDim + 1] * d[1];
        Real p = 1.0;
        if (dm > 1.0e-14 * (std::abs(wc[v]) + 1.0e-30)) {
          const Real dp = wmax[v] - wc[v];
          p = (opt_.limiter == LimiterType::kVenkatakrishnan)
                  ? venkatakrishnanPhi(dp, dm, eps2[v])
                  : barthJespersenPhi(dp, dm);
        } else if (dm < -1.0e-14 * (std::abs(wc[v]) + 1.0e-30)) {
          const Real dp = wmin[v] - wc[v];
          p = (opt_.limiter == LimiterType::kVenkatakrishnan)
                  ? venkatakrishnanPhi(dp, dm, eps2[v])
                  : barthJespersenPhi(dp, dm);
        }
        phi[v] = std::min(phi[v], std::max(0.0, p));
      }
    };
    for (Index k = lsq_off_[c]; k < lsq_off_[c + 1]; ++k) {
      apply(Vec2{lsq_xf_[2 * k], lsq_xf_[2 * k + 1]});
    }
    for (int v = 0; v < kNVar; ++v) phi_[c * kNVar + v] = phi[v];
  }
}

void SpatialOperator::reconstructFace(Index c, const Vec2& xf, PrimVec& w) const {
  const Real* wc = &w_[c * kNVar];
  for (int v = 0; v < kNVar; ++v) w[v] = wc[v];
  if (!opt_.second_order) return;
  const Vec2 d = xf - mesh_.cell_center[c];
  for (int v = 0; v < kNVar; ++v) {
    w[v] += phi_[c * kNVar + v] * (grad_[(c * kNVar + v) * kDim + 0] * d[0] +
                                   grad_[(c * kNVar + v) * kDim + 1] * d[1]);
  }
}

void SpatialOperator::evaluateResidual() {
  halo_.exchange(u_.data(), kNVar);
  updatePrimitives();
  computeBoundaryStates();
  computeGradients();
  computeLimiter();
  halo_.exchange(grad_.data(), kNVar * kDim);
  halo_.exchange(phi_.data(), kNVar);

  std::fill(res_.begin(), res_.end(), 0.0);
  const Index no = mesh_.num_owned;
  const bool viscous = (cfg_.mode == PhysicsMode::kLaminar) && transport_.active();
  const Real cp = gas_.cp();
  const Real Rgas = gas_.R();
  const Real gamma = gas_.gamma();
  const Real pr = gas_.prandtl();

  auto safeState = [&](Index c, const Vec2& xf, PrimVec& w) {
    reconstructFace(c, xf, w);
    if (w[0] < rho_floor_ || w[3] < p_floor_) {
      for (int v = 0; v < kNVar; ++v) w[v] = w_[c * kNVar + v];
      // Count owned cells only: a face on a partition cut is evaluated on both
      // ranks, so counting ghost-side fallbacks would make the diagnostic
      // depend on the rank count.
      if (c < no) ++stats_.positivity_fallbacks;
    }
  };

  // Face gradient with the standard directional-derivative correction.
  auto faceGradients = [&](const Real* gl, const Real* gr, const PrimVec& wl, const PrimVec& wr,
                           const Vec2& dvec, ViscousGradients& vg, Real& rho_f, Real& p_f,
                           Grad& drho, Grad& dp) {
    const Real dlen = norm(dvec);
    const Vec2 e{dvec[0] / dlen, dvec[1] / dlen};
    std::array<Grad, kNVar> g{};
    for (int v = 0; v < kNVar; ++v) {
      Grad avg{0.5 * (gl[v * kDim + 0] + gr[v * kDim + 0]),
               0.5 * (gl[v * kDim + 1] + gr[v * kDim + 1])};
      const Real dirderiv = (wr[v] - wl[v]) / dlen;
      const Real corr = dirderiv - (avg[0] * e[0] + avg[1] * e[1]);
      g[v] = {avg[0] + corr * e[0], avg[1] + corr * e[1]};
    }
    drho = g[0];
    vg.du = g[1];
    vg.dv = g[2];
    dp = g[3];
    rho_f = 0.5 * (wl[0] + wr[0]);
    p_f = 0.5 * (wl[3] + wr[3]);
    const Real T = p_f / (rho_f * Rgas);
    vg.dT = {(dp[0] - T * Rgas * drho[0]) / (rho_f * Rgas),
             (dp[1] - T * Rgas * drho[1]) / (rho_f * Rgas)};
  };

  // ---------------- interior faces ----------------
  for (Index f = 0; f < mesh_.numFaces(); ++f) {
    const Index l = mesh_.face_l[f], r = mesh_.face_r[f];
    const Vec2& n = mesh_.face_normal[f];
    const Real area = mesh_.face_area[f];
    PrimVec wl{}, wr{};
    safeState(l, mesh_.face_center[f], wl);
    safeState(r, mesh_.face_center[f], wr);
    ConsVec flux = inviscidFlux(wl, wr, n, gas_, flux_opt_);

    if (viscous) {
      const PrimVec wcl{w_[l * kNVar], w_[l * kNVar + 1], w_[l * kNVar + 2], w_[l * kNVar + 3]};
      const PrimVec wcr{w_[r * kNVar], w_[r * kNVar + 1], w_[r * kNVar + 2], w_[r * kNVar + 3]};
      ViscousGradients vg;
      Real rho_f = 0.0, p_f = 0.0;
      Grad drho{}, dp{};
      faceGradients(&grad_[l * kNVar * kDim], &grad_[r * kNVar * kDim], wcl, wcr,
                    mesh_.cell_center[r] - mesh_.cell_center[l], vg, rho_f, p_f, drho, dp);
      const Real T = p_f / (rho_f * Rgas);
      const Real mu = transport_.viscosity(T);
      const Real kcond = mu * cp / pr;
      const Real uf = 0.5 * (wcl[1] + wcr[1]);
      const Real vf = 0.5 * (wcl[2] + wcr[2]);
      const ConsVec fv = viscousNormalFlux(vg, mu, kcond, uf, vf, n, false);
      for (int k = 0; k < kNVar; ++k) flux[k] -= fv[k];
    }

    for (int k = 0; k < kNVar; ++k) {
      const Real contrib = flux[k] * area;
      if (l < no) res_[l * kNVar + k] += contrib;
      if (r < no) res_[r * kNVar + k] -= contrib;
    }
  }

  // ---------------- boundary faces ----------------
  for (Index b = 0; b < mesh_.numBoundaryFaces(); ++b) {
    const Index c = mesh_.bface_cell[b];
    const Vec2& n = mesh_.bface_normal[b];
    const Real area = mesh_.bface_area[b];
    const BcType bc = patch_bc_[mesh_.bface_patch[b]];

    PrimVec wi{};
    safeState(c, mesh_.bface_center[b], wi);
    const PrimVec wg = ghostState(bc, wi, n, winf_, gas_);
    // Refresh the boundary value from the *reconstructed* interior state so
    // that wall pressure, skin friction and the surface output are second
    // order.  (The gradient stencil above deliberately uses the lagged
    // cell-centre version, which breaks the circular dependency.)
    {
      const PrimVec wb2 = boundaryValue(bc, wi, n, winf_, gas_);
      for (int k = 0; k < kNVar; ++k) bstate_[b * kNVar + k] = wb2[k];
    }

    ConsVec flux = inviscidFlux(wi, wg, n, gas_, flux_opt_);

    Real tx = 0.0, ty = 0.0;
    if (viscous && bc != BcType::kSlipWall) {
      const PrimVec wcc{w_[c * kNVar], w_[c * kNVar + 1], w_[c * kNVar + 2], w_[c * kNVar + 3]};
      PrimVec wb{};
      for (int k = 0; k < kNVar; ++k) wb[k] = bstate_[b * kNVar + k];
      ViscousGradients vg;
      Real rho_f = 0.0, p_f = 0.0;
      Grad drho{}, dp{};
      const Real* gc = &grad_[c * kNVar * kDim];
      faceGradients(gc, gc, wcc, wb, mesh_.bface_center[b] - mesh_.cell_center[c], vg, rho_f, p_f,
                    drho, dp);
      rho_f = wb[0];
      p_f = wb[3];
      const Real T = p_f / (rho_f * Rgas);
      const Real mu = transport_.viscosity(T);
      const Real kcond = mu * cp / pr;
      const bool adiabatic = (bc == BcType::kNoSlipAdiabaticWall);
      const ConsVec fv = viscousNormalFlux(vg, mu, kcond, wb[1], wb[2], n, adiabatic);
      for (int k = 0; k < kNVar; ++k) flux[k] -= fv[k];
      tx = fv[1];
      ty = fv[2];
    }
    bshear_[b * kDim + 0] = tx;
    bshear_[b * kDim + 1] = ty;

    for (int k = 0; k < kNVar; ++k) res_[c * kNVar + k] += flux[k] * area;
  }
  (void)gamma;
}

ResidualNorms SpatialOperator::computeNorms(const std::vector<Real>& r) const {
  // Volume-weighted (mass-matrix) L2 norm of the scaled residual, i.e. the
  // discrete L2(Omega) norm of dU/dt divided by the domain volume:
  //
  //     ||r||_2^2 = ( sum_c V_c sum_k r_kc^2 ) / ( |Omega| * nvar ),
  //     r_kc      = R_kc / (V_c s_k).
  //
  // The unweighted RMS of r would be dominated by a handful of degenerate
  // sliver cells at the sharp aerofoil trailing edge (46 cells out of 20816
  // carry 90% of it), where the inviscid solution is genuinely singular and no
  // scheme converges.  The L_infinity column of residuals.csv still reports the
  // worst single cell, so that behaviour remains visible rather than hidden.
  std::array<Real, kNVar> sums{};
  Real linf = 0.0;
  for (Index c = 0; c < mesh_.num_owned; ++c) {
    const Real vol = mesh_.cell_volume[c];
    const Real inv_v = 1.0 / vol;
    for (int k = 0; k < kNVar; ++k) {
      const Real v = r[c * kNVar + k] * inv_v / res_scale_[k];
      sums[k] += vol * v * v;
      linf = std::max(linf, std::abs(v));
    }
  }
  std::array<Real, kNVar> gsum = sums;
  globalSumArray(gsum.data(), kNVar, comm_);
  const Real glinf = globalMax(linf, comm_);
  ResidualNorms out;
  const Real vol_total = std::max(global_volume_, 1e-300);
  Real tot = 0.0;
  for (int k = 0; k < kNVar; ++k) {
    out.per_equation[k] = std::sqrt(gsum[k] / vol_total);
    tot += gsum[k];
  }
  out.l2 = std::sqrt(tot / (vol_total * kNVar));
  out.linf = glinf;
  return out;
}

void SpatialOperator::computeTimeStep(Real cfl, Real physical_diag, Real spatial_scale) {
  const Index no = mesh_.num_owned;
  const bool viscous = (cfg_.mode == PhysicsMode::kLaminar) && transport_.active();
  const Real gamma = gas_.gamma();
  const Real pr = gas_.prandtl();

  std::vector<Real> sum_c(static_cast<std::size_t>(no), 0.0);
  std::vector<Real> sum_v(static_cast<std::size_t>(no), 0.0);
  std::vector<Real> sum_lambda(static_cast<std::size_t>(no), 0.0);

  auto kappa = [&](Real rho, Real T, Real dlen) {
    if (!viscous) return 0.0;
    const Real mu = transport_.viscosity(T);
    return std::max(4.0 / 3.0, gamma / pr) * mu / (rho * dlen);
  };

  for (Index f = 0; f < mesh_.numFaces(); ++f) {
    const Index l = mesh_.face_l[f], r = mesh_.face_r[f];
    const Vec2& n = mesh_.face_normal[f];
    const PrimVec wl{w_[l * kNVar], w_[l * kNVar + 1], w_[l * kNVar + 2], w_[l * kNVar + 3]};
    const PrimVec wr{w_[r * kNVar], w_[r * kNVar + 1], w_[r * kNVar + 2], w_[r * kNVar + 3]};
    const Real lc = 0.5 * (convectiveSpectralRadius(wl, n, gas_) +
                           convectiveSpectralRadius(wr, n, gas_));
    const Vec2 d = mesh_.cell_center[r] - mesh_.cell_center[l];
    const Real dlen = std::max(norm(d), 1e-30);
    const Real rho_f = 0.5 * (wl[0] + wr[0]);
    const Real p_f = 0.5 * (wl[3] + wr[3]);
    const Real kv = kappa(rho_f, p_f / (rho_f * gas_.R()), dlen);
    face_lambda_[f] = lc + 2.0 * kv;
    const Real area = mesh_.face_area[f];
    if (l < no) { sum_c[l] += lc * area; sum_v[l] += kv * area; sum_lambda[l] += face_lambda_[f] * area; }
    if (r < no) { sum_c[r] += lc * area; sum_v[r] += kv * area; sum_lambda[r] += face_lambda_[f] * area; }
  }
  for (Index b = 0; b < mesh_.numBoundaryFaces(); ++b) {
    const Index c = mesh_.bface_cell[b];
    const Vec2& n = mesh_.bface_normal[b];
    const PrimVec wb{bstate_[b * kNVar], bstate_[b * kNVar + 1], bstate_[b * kNVar + 2],
                     bstate_[b * kNVar + 3]};
    const Real lc = convectiveSpectralRadius(wb, n, gas_);
    const Vec2 d = mesh_.bface_center[b] - mesh_.cell_center[c];
    const Real dlen = std::max(norm(d), 1e-30);
    const Real kv = kappa(wb[0], wb[3] / (wb[0] * gas_.R()), dlen);
    bface_lambda_[b] = lc + 2.0 * kv;
    const Real area = mesh_.bface_area[b];
    sum_c[c] += lc * area;
    sum_v[c] += kv * area;
    sum_lambda[c] += bface_lambda_[b] * area;
  }

  for (Index c = 0; c < no; ++c) {
    const Real vol = mesh_.cell_volume[c];
    const Real denom = sum_c[c] + opt_.viscous_dt_factor * sum_v[c];
    dtau_[c] = cfl * vol / std::max(denom, 1e-300);
    diag_[c] = vol / dtau_[c] + physical_diag * vol + 0.5 * spatial_scale * sum_lambda[c];
  }
}

std::vector<Real> SpatialOperator::residualMagnitude() const {
  std::vector<Real> m(static_cast<std::size_t>(mesh_.num_owned), 0.0);
  for (Index c = 0; c < mesh_.num_owned; ++c) {
    Real s = 0.0;
    const Real inv_v = 1.0 / mesh_.cell_volume[c];
    for (int k = 0; k < kNVar; ++k) {
      const Real v = res_[c * kNVar + k] * inv_v / res_scale_[k];
      s += v * v;
    }
    m[c] = std::sqrt(s);
  }
  return m;
}

long long SpatialOperator::applyUpdate(const std::vector<Real>& du, Real alpha) {
  long long backtracks = 0;
  const Real gm1 = gas_.gamma() - 1.0;
  for (Index c = 0; c < mesh_.num_owned; ++c) {
    Real a = alpha;
    ConsVec un{}, uc{};
    for (int k = 0; k < kNVar; ++k) uc[k] = u_[c * kNVar + k];
    bool ok = false;
    for (int trial = 0; trial <= opt_.max_update_backtracks; ++trial) {
      for (int k = 0; k < kNVar; ++k) un[k] = uc[k] + a * du[c * kNVar + k];
      const Real rho = un[0];
      if (rho > rho_floor_) {
        const Real p = gm1 * (un[3] - 0.5 * (un[1] * un[1] + un[2] * un[2]) / rho);
        if (p > p_floor_ && std::isfinite(p)) { ok = true; break; }
      }
      a *= 0.5;
      if (trial == 0) ++backtracks;
    }
    if (!ok) continue;   // keep the old state for this cell
    for (int k = 0; k < kNVar; ++k) u_[c * kNVar + k] = un[k];
  }
  stats_.update_backtracks += backtracks;
  return backtracks;
}

}  // namespace cfd
