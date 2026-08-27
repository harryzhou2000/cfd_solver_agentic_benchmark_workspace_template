// Approximate Riemann solvers for the 2-D compressible Euler fluxes.
//
// Three schemes are provided.  HLLC is the production default, for positivity
// rather than accuracy: its wave-speed estimate keeps the intermediate states
// physical without help.  Roe with a Harten-Yee entropy fix and Rusanov (local
// Lax-Friedrichs) are selectable from the command line and are used to
// cross-check the flux implementation; with the shock fix below all three agree
// on the Mach 2 drag to under a percent (see the report).
//
// Both contact-resolving schemes are combined with a multidimensional shock fix
// (shockFixWeight below), because resolving the contact wave is exactly what
// makes them vulnerable to the carbuncle instability at a strong bow shock.
#pragma once

#include <algorithm>
#include <cmath>

#include "core/Types.hpp"
#include "physics/PerfectGas.hpp"

namespace cfd {

enum class RiemannScheme { kRoe, kHllc, kRusanov };

struct FluxOptions {
  RiemannScheme scheme = RiemannScheme::kRoe;
  Real entropy_fix = 0.10;   // Harten-Yee delta as a fraction of (|un|+a)
  Real dissipation_scale = 1.0;    // multiplies the Rusanov jump term
  Real shock_fix = 1.0;      // strength of the multidimensional shock fix, 0 disables
};

// Weight of the Rusanov blend used by the multidimensional shock fix.
//
// A contact-resolving flux (Roe, HLLC) carries almost no dissipation in the
// direction *along* a captured shock, so a transverse perturbation of the shock
// front is not damped and grows into the carbuncle: on the Mach 2 aerofoil the
// leading-edge surface pressure reaches 1.29 times the Rayleigh-Pitot limit and
// the solution of a symmetric problem loses its symmetry.  The cure is to add
// Rusanov dissipation on the faces that lie *inside* a strong shock layer and
// are nearly parallel to it, and only there.  Two dimensionless measures select
// those faces:
//
//   sigma = |grad p| h / p    strength of the pressure gradient across a cell,
//                             O(1) inside a captured strong shock and O(h)
//                             wherever the flow is smooth;
//   alpha = |n . grad p / |grad p||   alignment of the face with the shock:
//                             1 for a face the shock crosses head-on, 0 for a
//                             face lying in the shock front.
//
// A third factor switches the fix off wherever the flow is subsonic, since a
// shock cannot exist there.
//
// The blend is proportional to the strength and to (1 - alpha^2), so faces that
// resolve the shock keep the full contact-resolving flux and stay sharp, while
// the faces along the front - the ones that feed the instability - are damped.
// The sensor is built from face-averaged quantities that are identical on both
// sides of a partition cut, so the fix does not break partition independence.
inline Real shockFixWeight(const Vec2& n, const Vec2& gradp, Real p, Real h, Real mach,
                           Real k) {
  if (k <= 0.0 || p <= 0.0 || h <= 0.0) return 0.0;
  // A shock needs supersonic flow upstream of it, so the fix is switched off
  // entirely in subsonic flow.  Without this guard the pressure-gradient sensor
  // also fires on the stagnation region of a stretched mesh, where the gradient
  // is large but there is no shock and no instability to cure.
  const Real sonic = std::min(Real(1), std::max(Real(0), (mach - Real(0.95)) / Real(0.10)));
  if (sonic <= 0.0) return 0.0;
  const Real gn = std::sqrt(gradp[0] * gradp[0] + gradp[1] * gradp[1]);
  if (gn <= 0.0) return 0.0;
  const Real sigma = gn * h / p;
  // Dead band, so that smooth compressions and the acoustic field are untouched.
  constexpr Real kOn = 0.25, kFull = 0.75;
  const Real strength = std::min(Real(1), std::max(Real(0), (sigma - kOn) / (kFull - kOn)));
  if (strength <= 0.0) return 0.0;
  const Real alpha = std::abs(n[0] * gradp[0] + n[1] * gradp[1]) / gn;
  const Real transverse = Real(1) - alpha * alpha;
  return std::min(Real(1), k * sonic * strength * transverse);
}

// Maximum signal speed used for local time stepping and the implicit diagonal.
inline Real convectiveSpectralRadius(const PrimVec& w, const Vec2& n, const PerfectGas& gas) {
  const Real un = w[1] * n[0] + w[2] * n[1];
  return std::abs(un) + gas.soundSpeed(w[0], w[3]);
}

// Roe flux with a Harten-Yee entropy fix applied to the two genuinely
// nonlinear (acoustic) fields only.  Leaving the linearly degenerate fields
// untouched keeps the wall boundary flux free of spurious shear dissipation,
// because a mirrored wall state has exactly zero Roe-averaged normal velocity.
inline ConsVec roeFlux(const PrimVec& wl, const PrimVec& wr, const Vec2& n,
                       const PerfectGas& gas, const FluxOptions& opt) {
  const Real g = gas.gamma();
  const Real rl = wl[0], pl = wl[3];
  const Real rr = wr[0], pr = wr[3];
  const Real ql2 = wl[1] * wl[1] + wl[2] * wl[2];
  const Real qr2 = wr[1] * wr[1] + wr[2] * wr[2];
  const Real hl = gas.totalEnthalpy(rl, pl, ql2);
  const Real hr = gas.totalEnthalpy(rr, pr, qr2);

  const Real srl = std::sqrt(rl), srr = std::sqrt(rr);
  const Real den = srl + srr;
  const Real u = (srl * wl[1] + srr * wr[1]) / den;
  const Real v = (srl * wl[2] + srr * wr[2]) / den;
  const Real h = (srl * hl + srr * hr) / den;
  const Real q2 = u * u + v * v;
  Real a2 = (g - 1.0) * (h - 0.5 * q2);
  a2 = std::max(a2, 1.0e-14);
  const Real a = std::sqrt(a2);
  const Real rho = srl * srr;

  const Vec2 t{-n[1], n[0]};
  const Real un = u * n[0] + v * n[1];
  const Real ut = u * t[0] + v * t[1];

  const Real drho = rr - rl;
  const Real dp = pr - pl;
  const Real dun = (wr[1] - wl[1]) * n[0] + (wr[2] - wl[2]) * n[1];
  const Real dut = (wr[1] - wl[1]) * t[0] + (wr[2] - wl[2]) * t[1];

  // Wave strengths.
  const Real a1 = (dp - rho * a * dun) / (2.0 * a2);
  const Real a2s = drho - dp / a2;
  const Real a3 = rho * dut;
  const Real a4 = (dp + rho * a * dun) / (2.0 * a2);

  // Eigenvalues with Harten-Yee entropy fix on the acoustic fields.
  const Real delta = opt.entropy_fix * (std::abs(un) + a);
  auto fix = [delta](Real lam) {
    const Real al = std::abs(lam);
    return (al > delta || delta <= 0.0) ? al : 0.5 * (lam * lam / delta + delta);
  };
  const Real l1 = fix(un - a);
  const Real l2 = std::abs(un);
  const Real l3 = l2;
  const Real l4 = fix(un + a);

  // Right eigenvectors.
  const ConsVec r1{1.0, u - a * n[0], v - a * n[1], h - a * un};
  const ConsVec r2{1.0, u, v, 0.5 * q2};
  const ConsVec r3{0.0, t[0], t[1], ut};
  const ConsVec r4{1.0, u + a * n[0], v + a * n[1], h + a * un};

  const ConsVec fl = gas.normalFlux(wl, n);
  const ConsVec fr = gas.normalFlux(wr, n);
  ConsVec f{};
  for (int k = 0; k < kNVar; ++k) {
    const Real diss = l1 * a1 * r1[k] + l2 * a2s * r2[k] + l3 * a3 * r3[k] + l4 * a4 * r4[k];
    f[k] = 0.5 * (fl[k] + fr[k]) - 0.5 * diss;
  }
  return f;
}

// HLLC with Einfeldt-Batten wave-speed estimates.
inline ConsVec hllcFlux(const PrimVec& wl, const PrimVec& wr, const Vec2& n,
                        const PerfectGas& gas, const FluxOptions&) {
  const Real g = gas.gamma();
  const Real rl = wl[0], pl = wl[3], rr = wr[0], pr = wr[3];
  const Real unl = wl[1] * n[0] + wl[2] * n[1];
  const Real unr = wr[1] * n[0] + wr[2] * n[1];
  const Real al = gas.soundSpeed(rl, pl), ar = gas.soundSpeed(rr, pr);

  const Real srl = std::sqrt(rl), srr = std::sqrt(rr), den = srl + srr;
  const Real hl = gas.totalEnthalpy(rl, pl, wl[1] * wl[1] + wl[2] * wl[2]);
  const Real hr = gas.totalEnthalpy(rr, pr, wr[1] * wr[1] + wr[2] * wr[2]);
  const Real u = (srl * wl[1] + srr * wr[1]) / den;
  const Real v = (srl * wl[2] + srr * wr[2]) / den;
  const Real h = (srl * hl + srr * hr) / den;
  const Real aroe = std::sqrt(std::max((g - 1.0) * (h - 0.5 * (u * u + v * v)), 1.0e-14));
  const Real unroe = u * n[0] + v * n[1];

  const Real sl = std::min(unl - al, unroe - aroe);
  const Real sr = std::max(unr + ar, unroe + aroe);

  const ConsVec ul = gas.toConservative(wl);
  const ConsVec ur = gas.toConservative(wr);
  if (sl >= 0.0) return gas.normalFlux(wl, n);
  if (sr <= 0.0) return gas.normalFlux(wr, n);

  const Real sstar = (pr - pl + rl * unl * (sl - unl) - rr * unr * (sr - unr)) /
                     (rl * (sl - unl) - rr * (sr - unr));
  auto starState = [&](const PrimVec& w, const ConsVec& uc, Real s, Real un) {
    const Real f = w[0] * (s - un) / (s - sstar);
    ConsVec us{};
    us[0] = f;
    us[1] = f * (w[1] + (sstar - un) * n[0]);
    us[2] = f * (w[2] + (sstar - un) * n[1]);
    us[3] = f * (uc[3] / w[0] + (sstar - un) * (sstar + w[3] / (w[0] * (s - un))));
    return us;
  };
  ConsVec f{};
  if (sstar >= 0.0) {
    const ConsVec us = starState(wl, ul, sl, unl);
    const ConsVec fl = gas.normalFlux(wl, n);
    for (int k = 0; k < kNVar; ++k) f[k] = fl[k] + sl * (us[k] - ul[k]);
  } else {
    const ConsVec us = starState(wr, ur, sr, unr);
    const ConsVec fr = gas.normalFlux(wr, n);
    for (int k = 0; k < kNVar; ++k) f[k] = fr[k] + sr * (us[k] - ur[k]);
  }
  return f;
}

// Rusanov / local Lax-Friedrichs.
inline ConsVec rusanovFlux(const PrimVec& wl, const PrimVec& wr, const Vec2& n,
                           const PerfectGas& gas, const FluxOptions& opt) {
  const ConsVec ul = gas.toConservative(wl);
  const ConsVec ur = gas.toConservative(wr);
  const ConsVec fl = gas.normalFlux(wl, n);
  const ConsVec fr = gas.normalFlux(wr, n);
  const Real smax = std::max(convectiveSpectralRadius(wl, n, gas),
                             convectiveSpectralRadius(wr, n, gas));
  ConsVec f{};
  for (int k = 0; k < kNVar; ++k) {
    f[k] = 0.5 * (fl[k] + fr[k]) - 0.5 * opt.dissipation_scale * smax * (ur[k] - ul[k]);
  }
  return f;
}

inline ConsVec inviscidFlux(const PrimVec& wl, const PrimVec& wr, const Vec2& n,
                            const PerfectGas& gas, const FluxOptions& opt,
                            Real rusanov_blend = 0.0) {
  ConsVec f{};
  switch (opt.scheme) {
    case RiemannScheme::kRoe: f = roeFlux(wl, wr, n, gas, opt); break;
    case RiemannScheme::kHllc: f = hllcFlux(wl, wr, n, gas, opt); break;
    case RiemannScheme::kRusanov: return rusanovFlux(wl, wr, n, gas, opt);
  }
  if (rusanov_blend > 0.0) {
    const ConsVec g = rusanovFlux(wl, wr, n, gas, opt);
    for (int v = 0; v < kNVar; ++v) f[v] += rusanov_blend * (g[v] - f[v]);
  }
  return f;
}

inline const char* toString(RiemannScheme s) {
  switch (s) {
    case RiemannScheme::kRoe: return "roe";
    case RiemannScheme::kHllc: return "hllc";
    case RiemannScheme::kRusanov: return "rusanov_llf";
  }
  return "unknown";
}

}  // namespace cfd
