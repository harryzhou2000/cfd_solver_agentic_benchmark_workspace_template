#include "numerics/residual.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "physics/boundary_conditions.h"

namespace cns2d {

ResidualAssembler::ResidualAssembler(const DistributedMesh &mesh, const FlowContext &flow,
                                     const SchemeOptions &scheme)
    : mesh_(mesh), flow_(flow), scheme_(scheme) {
  const Index n = mesh_.numLocal();
  W_.resize(n);
  grad_.resize(n);
  phi_.resize(n);
  conv_radius_.assign(static_cast<std::size_t>(mesh_.numOwned()), 0.0);
  visc_radius_.assign(static_cast<std::size_t>(mesh_.numOwned()), 0.0);
}

void ResidualAssembler::computeFaceStates(Index face, const StateField &U, PrimVec &WL, PrimVec &WR,
                                          ResidualDiagnostics &diag) const {
  const LocalFace &f = mesh_.faces()[static_cast<std::size_t>(face)];
  const auto &cells = mesh_.cells();

  const Index cl = f.left;
  const Real *wl = W_.cell(cl);

  if (!scheme_.second_order) {
    for (int v = 0; v < kNumVars; ++v) WL[static_cast<std::size_t>(v)] = wl[v];
  } else {
    const Vec2 delta = f.geom.centroid - cells[static_cast<std::size_t>(cl)].centroid;
    if (!reconstructPrimitive(wl, grad_.cell(cl), phi_.cell(cl), delta, WL)) {
      ++diag.positivity_fallbacks;
    }
  }

  if (f.kind == FaceKind::kBoundary) {
    // The Riemann solver needs the GHOST state (mirrored at walls) so that the
    // resulting flux enforces the wall condition.  It is built from the
    // RECONSTRUCTED interior face state, so the boundary condition is applied at
    // second order too.
    const ConsVec u_face = flow_.gas.consFromPrim(WL);
    const ConsVec ub =
        boundaryGhostState(mesh_.boundaryTypeOfTag(f.boundary_tag), u_face, f.geom.normal, flow_);
    WR = flow_.gas.primFromCons(ub);
    return;
  }

  const Index cr = f.right;
  const Real *wr = W_.cell(cr);
  if (!scheme_.second_order) {
    for (int v = 0; v < kNumVars; ++v) WR[static_cast<std::size_t>(v)] = wr[v];
  } else {
    const Vec2 delta = f.geom.centroid - cells[static_cast<std::size_t>(cr)].centroid;
    // Ghost cells carry gradients and limiter factors received by halo
    // exchange, so a partition-boundary face reconstructs exactly as it would
    // in serial.
    if (!reconstructPrimitive(wr, grad_.cell(cr), phi_.cell(cr), delta, WR)) {
      ++diag.positivity_fallbacks;
    }
  }
}

void ResidualAssembler::evaluate(const StateField &U, StateField &residual, HaloExchange &halo,
                                 ResidualDiagnostics &diag) {
  const Index num_owned = mesh_.numOwned();
  const auto &faces = mesh_.faces();
  const auto &cells = mesh_.cells();

  residual.setZero();
  std::fill(conv_radius_.begin(), conv_radius_.end(), 0.0);
  std::fill(visc_radius_.begin(), visc_radius_.end(), 0.0);

  // ---- primitives (owned + ghost) --------------------------------------
  // U ghosts are synchronised by the caller; primitives are derived locally for
  // every local cell, so no extra communication is needed here.
  computePrimitives(mesh_, flow_, U, W_);

  // ---- gradients and limiter -------------------------------------------
  if (scheme_.second_order || scheme_.viscous) {
    computeGradients(mesh_, flow_, W_, U, grad_);
    // Ghost gradients are required for face reconstruction from the far side
    // and for the viscous face gradient average.
    halo.exchange(grad_.data(), GradientField::kComponents, GradientField::kComponents);
  }
  if (scheme_.second_order) {
    computeLimiter(mesh_, flow_, scheme_.limiter, W_, grad_, scheme_.venkat_k, phi_);
    halo.exchange(phi_.data(), kNumVars, kNumVars);
  }

  diag.min_density = std::numeric_limits<Real>::max();
  diag.min_pressure = std::numeric_limits<Real>::max();
  diag.max_mach = 0.0;

  // ---- face loop --------------------------------------------------------
  const Index num_faces = mesh_.numFaces();
  for (Index i = 0; i < num_faces; ++i) {
    const LocalFace &f = faces[static_cast<std::size_t>(i)];
    const Index cl = f.left;
    const Index cr = f.right;

    // Skip faces that touch no owned cell (cannot happen by construction, but
    // the guard keeps the loop safe if the partition layout changes).
    const bool left_owned = cl < num_owned;
    const bool right_owned = cr >= 0 && cr < num_owned;
    if (!left_owned && !right_owned) continue;

    PrimVec WL{};
    PrimVec WR{};
    computeFaceStates(i, U, WL, WR, diag);

    Real max_wave_speed = 0.0;
    ConsVec flux = riemannFlux(scheme_.inviscid_flux, flow_.gas, WL, WR, f.geom.normal,
                               scheme_.rusanov_dissipation_scale, max_wave_speed);

    // ---- viscous contribution ------------------------------------------
    Real visc_coefficient = 0.0;
    if (scheme_.viscous) {
      ViscousGradients vg;
      PrimVec W_face{};
      Real mu = 0.0;

      if (f.kind == FaceKind::kBoundary) {
        const BCType bc = mesh_.boundaryTypeOfTag(f.boundary_tag);
        // The viscous flux must be evaluated with the PHYSICAL state on the wall
        // (zero velocity for no-slip), not with the mirrored ghost state used by
        // the Riemann solver.  The gradient comes from the adjacent cell,
        // corrected so the wall-normal difference between the cell centre and the
        // wall is represented exactly, which is what produces a physical skin
        // friction on a stretched boundary-layer mesh.
        const ConsVec u_wall =
            boundaryFaceState(bc, flow_.gas.consFromPrim(WL), f.geom.normal, flow_);
        W_face = flow_.gas.primFromCons(u_wall);
        const Real T_face = flow_.gas.temperatureFromRhoP(W_face[kPrimRho], W_face[kPrimP]);
        mu = flow_.transport.viscosity(T_face);

        const PrimGrad gc = grad_.getAll(cl);
        vg.grad_u = gc[kPrimU];
        vg.grad_v = gc[kPrimV];

        // Temperature gradient from rho and p gradients: T = p/(rho R).
        const Real inv_R = 1.0 / flow_.gas.R();
        const Real rho_c = W_.cell(cl)[kPrimRho];
        const Real p_c = W_.cell(cl)[kPrimP];
        const Vec2 grad_rho = gc[kPrimRho];
        const Vec2 grad_p = gc[kPrimP];
        vg.grad_T.x = inv_R * (grad_p.x / rho_c - p_c * grad_rho.x / (rho_c * rho_c));
        vg.grad_T.y = inv_R * (grad_p.y / rho_c - p_c * grad_rho.y / (rho_c * rho_c));

        // Wall-normal correction using the imposed wall state.
        const Vec2 dvec = f.geom.centroid - cells[static_cast<std::size_t>(cl)].centroid;
        const Real dn = dot(dvec, f.geom.normal);
        if (std::abs(dn) > 0.0) {
          const Real *wc = W_.cell(cl);
          auto correct = [&](Vec2 g, Real value_face, Real value_cell) {
            const Real expected = (value_face - value_cell) / dn;
            const Real actual = dot(g, f.geom.normal);
            return g + (expected - actual) * f.geom.normal;
          };
          vg.grad_u = correct(vg.grad_u, W_face[kPrimU], wc[kPrimU]);
          vg.grad_v = correct(vg.grad_v, W_face[kPrimV], wc[kPrimV]);
          if (bc == BCType::kNoSlipAdiabaticWall) {
            // Adiabatic wall: enforce zero normal temperature gradient exactly.
            const Real normal_dT = dot(vg.grad_T, f.geom.normal);
            vg.grad_T = vg.grad_T - normal_dT * f.geom.normal;
          }
        }
      } else {
        // Interior face: average the two cell gradients, then correct the
        // component along the cell-to-cell line so the face gradient reproduces
        // the actual cell-value difference.
        const PrimGrad gl = grad_.getAll(cl);
        const PrimGrad gr = grad_.getAll(cr);
        const Real *wl = W_.cell(cl);
        const Real *wr = W_.cell(cr);
        for (int v = 0; v < kNumVars; ++v) {
          W_face[static_cast<std::size_t>(v)] = 0.5 * (wl[v] + wr[v]);
        }
        const Vec2 dvec =
            cells[static_cast<std::size_t>(cr)].centroid - cells[static_cast<std::size_t>(cl)].centroid;
        const Real d2 = dot(dvec, dvec);

        auto faceGradient = [&](Vec2 g_l, Vec2 g_r, Real phi_l, Real phi_r) {
          Vec2 avg = 0.5 * (g_l + g_r);
          if (d2 > 0.0) {
            const Real expected = (phi_r - phi_l) / std::sqrt(d2);
            const Vec2 e = (1.0 / std::sqrt(d2)) * dvec;
            const Real actual = dot(avg, e);
            avg = avg + (expected - actual) * e;
          }
          return avg;
        };

        vg.grad_u = faceGradient(gl[kPrimU], gr[kPrimU], wl[kPrimU], wr[kPrimU]);
        vg.grad_v = faceGradient(gl[kPrimV], gr[kPrimV], wl[kPrimV], wr[kPrimV]);

        const Real inv_R = 1.0 / flow_.gas.R();
        const Real T_l = inv_R * wl[kPrimP] / wl[kPrimRho];
        const Real T_r = inv_R * wr[kPrimP] / wr[kPrimRho];
        auto tempGradient = [&](Index c, const PrimGrad &g) {
          const Real *w = W_.cell(c);
          const Vec2 grad_rho = g[kPrimRho];
          const Vec2 grad_p = g[kPrimP];
          const Real rho = w[kPrimRho];
          const Real p = w[kPrimP];
          return Vec2{inv_R * (grad_p.x / rho - p * grad_rho.x / (rho * rho)),
                      inv_R * (grad_p.y / rho - p * grad_rho.y / (rho * rho))};
        };
        vg.grad_T = faceGradient(tempGradient(cl, gl), tempGradient(cr, gr), T_l, T_r);

        const Real T_face = flow_.gas.temperatureFromRhoP(W_face[kPrimRho], W_face[kPrimP]);
        mu = flow_.transport.viscosity(T_face);
      }

      const ConsVec fv = viscousNormalFlux(flow_.gas, mu, W_face, vg, f.geom.normal);
      for (int k = 0; k < kNumVars; ++k) flux[k] -= fv[k];

      // Viscous spectral-radius contribution: mu / (rho * distance).
      visc_coefficient = mu / std::max(W_face[kPrimRho], kTiny);
    }

    // ---- scatter ---------------------------------------------------------
    const Real area = f.geom.area;
    if (left_owned) {
      Real *r = residual.cell(cl);
      for (int k = 0; k < kNumVars; ++k) r[k] -= flux[k] * area;
      conv_radius_[static_cast<std::size_t>(cl)] += max_wave_speed * area;
      if (scheme_.viscous) {
        visc_radius_[static_cast<std::size_t>(cl)] += visc_coefficient * area * area *
                                                     cells[static_cast<std::size_t>(cl)].inv_volume;
      }
    }
    if (right_owned) {
      Real *r = residual.cell(cr);
      for (int k = 0; k < kNumVars; ++k) r[k] += flux[k] * area;
      conv_radius_[static_cast<std::size_t>(cr)] += max_wave_speed * area;
      if (scheme_.viscous) {
        visc_radius_[static_cast<std::size_t>(cr)] += visc_coefficient * area * area *
                                                     cells[static_cast<std::size_t>(cr)].inv_volume;
      }
    }
    ++diag.face_evaluations;
  }

  // ---- field diagnostics ------------------------------------------------
  // Identify the cell driving the global volume-weighted residual norm, and
  // whether it sits on a boundary.  This turns "the residual stalled" into a
  // concrete location that can be inspected.
  const auto &ranges = mesh_.cellFaceRanges();
  const auto &cell_faces_map = mesh_.cellFaces();
  diag.worst_weighted = 0.0;
  for (Index c = 0; c < num_owned; ++c) {
    const Real *w = W_.cell(c);
    diag.min_density = std::min(diag.min_density, w[kPrimRho]);
    diag.min_pressure = std::min(diag.min_pressure, w[kPrimP]);
    if (w[kPrimRho] > 0.0 && w[kPrimP] > 0.0) {
      const Real a = flow_.gas.soundSpeed(w[kPrimRho], w[kPrimP]);
      const Real speed = std::sqrt(w[kPrimU] * w[kPrimU] + w[kPrimV] * w[kPrimV]);
      diag.max_mach = std::max(diag.max_mach, speed / a);
    }

    const Real volume = cells[static_cast<std::size_t>(c)].volume;
    const Real inv_v = cells[static_cast<std::size_t>(c)].inv_volume;
    const Real *r = residual.cell(c);
    for (int k = 0; k < kNumVars; ++k) {
      const Real rate = r[k] * inv_v;
      const Real weighted = rate * rate * volume;  // this cell's share of the norm
      if (weighted > diag.worst_weighted) {
        diag.worst_weighted = weighted;
        diag.worst_location = cells[static_cast<std::size_t>(c)].centroid;
        diag.worst_volume = volume;
        diag.worst_component = k;
        diag.worst_touches_boundary = false;
        diag.worst_boundary_tag = -1;
        const DistributedMesh::CellFaceRange range = ranges[static_cast<std::size_t>(c)];
        for (Index j = 0; j < range.count; ++j) {
          const Index fid = cell_faces_map[static_cast<std::size_t>(range.begin + j)];
          const LocalFace &bf = faces[static_cast<std::size_t>(fid)];
          if (bf.kind == FaceKind::kBoundary) {
            diag.worst_touches_boundary = true;
            diag.worst_boundary_tag = bf.boundary_tag;
            break;
          }
        }
      }
    }
  }
}

}  // namespace cns2d
