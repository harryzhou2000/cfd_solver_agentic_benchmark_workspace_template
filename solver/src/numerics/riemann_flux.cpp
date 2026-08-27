#include "numerics/riemann_flux.h"

#include <algorithm>
#include <cmath>

#include "core/exceptions.h"

namespace cns2d {

std::string riemannFluxName(RiemannFluxType t) {
  switch (t) {
    case RiemannFluxType::kRoeEntropyFix:
      return "roe";
    case RiemannFluxType::kHllc:
      return "hllc";
    case RiemannFluxType::kRusanov:
      return "rusanov_llf";
  }
  return "unknown";
}

std::string entropyFixName(RiemannFluxType t) {
  switch (t) {
    case RiemannFluxType::kRoeEntropyFix:
      return "harten_hyman_entropy_fix";
    case RiemannFluxType::kHllc:
      return "hllc_wave_speed_bounds";
    case RiemannFluxType::kRusanov:
      return "none_scalar_dissipation";
  }
  return "none";
}

RiemannFluxType parseRiemannFluxType(const std::string &name) {
  if (name == "roe" || name == "roe_entropy_fix") return RiemannFluxType::kRoeEntropyFix;
  if (name == "hllc") return RiemannFluxType::kHllc;
  if (name == "rusanov" || name == "rusanov_llf" || name == "llf") return RiemannFluxType::kRusanov;
  throw CnsError("unknown inviscid flux '" + name + "' (supported: roe, hllc, rusanov)");
}

ConsVec eulerNormalFlux(const PerfectGas &gas, const PrimVec &W, Vec2 n) {
  const Real rho = W[kPrimRho];
  const Real u = W[kPrimU];
  const Real v = W[kPrimV];
  const Real p = W[kPrimP];
  const Real un = u * n.x + v * n.y;
  const Real rho_e_total = rho * gas.totalEnergy(rho, u, v, p);
  ConsVec f{};
  f[kRho] = rho * un;
  f[kRhoU] = rho * un * u + p * n.x;
  f[kRhoV] = rho * un * v + p * n.y;
  f[kRhoE] = un * (rho_e_total + p);
  return f;
}

namespace {

// Rusanov / local Lax-Friedrichs: central flux minus a scalar jump term scaled
// by the largest local wave speed.
ConsVec rusanovFlux(const PerfectGas &gas, const PrimVec &WL, const PrimVec &WR, Vec2 n,
                    Real dissipation_scale, Real &max_wave_speed) {
  const Real aL = gas.soundSpeed(WL[kPrimRho], WL[kPrimP]);
  const Real aR = gas.soundSpeed(WR[kPrimRho], WR[kPrimP]);
  const Real unL = WL[kPrimU] * n.x + WL[kPrimV] * n.y;
  const Real unR = WR[kPrimU] * n.x + WR[kPrimV] * n.y;
  const Real smax = std::max(std::abs(unL) + aL, std::abs(unR) + aR);
  max_wave_speed = smax;

  const ConsVec fL = eulerNormalFlux(gas, WL, n);
  const ConsVec fR = eulerNormalFlux(gas, WR, n);
  const ConsVec UL = gas.consFromPrim(WL);
  const ConsVec UR = gas.consFromPrim(WR);

  ConsVec f{};
  const Real lambda = 0.5 * dissipation_scale * smax;
  for (int k = 0; k < kNumVars; ++k) {
    f[k] = 0.5 * (fL[k] + fR[k]) - lambda * (UR[k] - UL[k]);
  }
  return f;
}

// HLLC with Einfeldt-Batten-style wave-speed bounds.
ConsVec hllcFlux(const PerfectGas &gas, const PrimVec &WL, const PrimVec &WR, Vec2 n,
                 Real &max_wave_speed) {
  const Real gamma = gas.gamma();
  const Real rhoL = WL[kPrimRho];
  const Real rhoR = WR[kPrimRho];
  const Real pL = WL[kPrimP];
  const Real pR = WR[kPrimP];
  const Real aL = gas.soundSpeed(rhoL, pL);
  const Real aR = gas.soundSpeed(rhoR, pR);

  // Rotate into face-normal coordinates.
  const Real tx = -n.y;
  const Real ty = n.x;
  const Real unL = WL[kPrimU] * n.x + WL[kPrimV] * n.y;
  const Real unR = WR[kPrimU] * n.x + WR[kPrimV] * n.y;
  const Real utL = WL[kPrimU] * tx + WL[kPrimV] * ty;
  const Real utR = WR[kPrimU] * tx + WR[kPrimV] * ty;

  // Roe-averaged normal velocity and sound speed for the wave-speed estimate.
  const Real sqrtL = std::sqrt(rhoL);
  const Real sqrtR = std::sqrt(rhoR);
  const Real inv_sum = 1.0 / (sqrtL + sqrtR);
  const Real un_tilde = (sqrtL * unL + sqrtR * unR) * inv_sum;
  const Real hL = gas.totalEnthalpy(rhoL, WL[kPrimU], WL[kPrimV], pL);
  const Real hR = gas.totalEnthalpy(rhoR, WR[kPrimU], WR[kPrimV], pR);
  const Real h_tilde = (sqrtL * hL + sqrtR * hR) * inv_sum;
  const Real ut_tilde = (sqrtL * utL + sqrtR * utR) * inv_sum;
  const Real q2_tilde = un_tilde * un_tilde + ut_tilde * ut_tilde;
  const Real a2_tilde = (gamma - 1.0) * (h_tilde - 0.5 * q2_tilde);
  const Real a_tilde = std::sqrt(std::max(a2_tilde, kTiny));

  const Real sL = std::min(unL - aL, un_tilde - a_tilde);
  const Real sR = std::max(unR + aR, un_tilde + a_tilde);
  max_wave_speed = std::max(std::abs(sL), std::abs(sR));

  const ConsVec UL = gas.consFromPrim(WL);
  const ConsVec UR = gas.consFromPrim(WR);
  if (sL >= 0.0) return eulerNormalFlux(gas, WL, n);
  if (sR <= 0.0) return eulerNormalFlux(gas, WR, n);

  // Contact/shear wave speed from the normal-momentum balance.
  const Real num = pR - pL + rhoL * unL * (sL - unL) - rhoR * unR * (sR - unR);
  const Real den = rhoL * (sL - unL) - rhoR * (sR - unR);
  const Real sStar = (std::abs(den) > kTiny) ? num / den : 0.5 * (unL + unR);

  // Star-region conserved states.  The normal velocity becomes the contact
  // speed sStar, the tangential velocity is carried through unchanged, and the
  // energy follows from the integral jump relation across the acoustic wave.
  auto starState = [&](const PrimVec &W, Real un, Real ut, Real rho, Real p, Real s) {
    const Real coef = rho * (s - un) / (s - sStar);
    const Real e_total = gas.totalEnergy(rho, W[kPrimU], W[kPrimV], p);
    ConsVec Us{};
    Us[kRho] = coef;
    // Rotate the star velocity back to Cartesian components.
    Us[kRhoU] = coef * (sStar * n.x + ut * tx);
    Us[kRhoV] = coef * (sStar * n.y + ut * ty);
    Us[kRhoE] = coef * (e_total + (sStar - un) * (sStar - p / (rho * (s - un))));
    return Us;
  };

  if (sStar >= 0.0) {
    const ConsVec UsL = starState(WL, unL, utL, rhoL, pL, sL);
    const ConsVec fL = eulerNormalFlux(gas, WL, n);
    ConsVec f{};
    for (int k = 0; k < kNumVars; ++k) f[k] = fL[k] + sL * (UsL[k] - UL[k]);
    return f;
  }
  const ConsVec UsR = starState(WR, unR, utR, rhoR, pR, sR);
  const ConsVec fR = eulerNormalFlux(gas, WR, n);
  ConsVec f{};
  for (int k = 0; k < kNumVars; ++k) f[k] = fR[k] + sR * (UsR[k] - UR[k]);
  return f;
}

// Roe flux with a Harten-Hyman entropy fix on the acoustic waves.
ConsVec roeFlux(const PerfectGas &gas, const PrimVec &WL, const PrimVec &WR, Vec2 n,
                Real &max_wave_speed, bool &fallback) {
  fallback = false;
  const Real gamma = gas.gamma();
  const Real gm1 = gamma - 1.0;

  const Real rhoL = WL[kPrimRho];
  const Real rhoR = WR[kPrimRho];
  const Real pL = WL[kPrimP];
  const Real pR = WR[kPrimP];
  const Real uL = WL[kPrimU];
  const Real vL = WL[kPrimV];
  const Real uR = WR[kPrimU];
  const Real vR = WR[kPrimV];

  const Real hL = gas.totalEnthalpy(rhoL, uL, vL, pL);
  const Real hR = gas.totalEnthalpy(rhoR, uR, vR, pR);

  // Roe averages.
  const Real sqrtL = std::sqrt(rhoL);
  const Real sqrtR = std::sqrt(rhoR);
  const Real inv_sum = 1.0 / (sqrtL + sqrtR);
  const Real rho_hat = sqrtL * sqrtR;
  const Real u_hat = (sqrtL * uL + sqrtR * uR) * inv_sum;
  const Real v_hat = (sqrtL * vL + sqrtR * vR) * inv_sum;
  const Real h_hat = (sqrtL * hL + sqrtR * hR) * inv_sum;
  const Real q2_hat = u_hat * u_hat + v_hat * v_hat;
  const Real a2_hat = gm1 * (h_hat - 0.5 * q2_hat);
  if (!(a2_hat > 0.0)) {
    // The Roe average is not physically admissible; the caller should use a
    // positivity-preserving flux instead.
    fallback = true;
    max_wave_speed = std::max(std::abs(uL * n.x + vL * n.y) + gas.soundSpeed(rhoL, pL),
                              std::abs(uR * n.x + vR * n.y) + gas.soundSpeed(rhoR, pR));
    return ConsVec{};
  }
  const Real a_hat = std::sqrt(a2_hat);
  const Real un_hat = u_hat * n.x + v_hat * n.y;

  // Wave speeds.
  Real lambda[3] = {un_hat - a_hat, un_hat, un_hat + a_hat};
  max_wave_speed = std::abs(un_hat) + a_hat;

  // --- entropy fix on every wave -------------------------------------------
  //
  // Two distinct problems are corrected here, and both matter in these cases.
  //
  // (a) Acoustic waves at a sonic point.  A vanishing u-a or u+a eigenvalue lets
  //     plain Roe admit a non-physical expansion shock.  The Harten-Hyman fix
  //     widens the eigenvalue by the local left/right wave-speed spread.
  //
  // (b) Entropy and shear waves at a stagnation point or wherever the flow is
  //     parallel to a face.  Their eigenvalue is u.n, which then vanishes and
  //     leaves density and tangential-velocity jumps COMPLETELY undamped.  That
  //     is the classic Roe checkerboard/carbuncle mode: it does not blow up, it
  //     quietly stalls the steady residual and breaks the symmetry of a
  //     symmetric case.  A floor proportional to the local maximum wave speed
  //     restores a small amount of dissipation on the linear waves.
  //
  // The floor is applied through the same smooth Harten function, so the flux
  // stays consistent (identical states still give the exact physical flux,
  // because the jumps themselves vanish there).
  const Real unL = uL * n.x + vL * n.y;
  const Real unR = uR * n.x + vR * n.y;
  const Real aL = gas.soundSpeed(rhoL, pL);
  const Real aR = gas.soundSpeed(rhoR, pR);
  {
    // Harten smoothing: |lambda| -> (lambda^2 + delta^2) / (2 delta) below delta.
    auto harten = [](Real lambda_value, Real delta) {
      const Real abs_lambda = std::abs(lambda_value);
      if (delta <= 0.0) return abs_lambda;
      if (abs_lambda >= delta) return abs_lambda;
      return 0.5 * (lambda_value * lambda_value / delta + delta);
    };

    // Reference speed for the linear-wave floor.
    const Real max_speed = std::max(std::abs(unL) + aL, std::abs(unR) + aR);
    // 1/20 of the local maximum wave speed: enough to kill the checkerboard mode,
    // small enough to leave boundary layers and contact discontinuities sharp.
    const Real linear_floor = 0.05 * max_speed;

    // Acoustic wave u - a: Harten-Hyman spread, with the same floor as a lower
    // bound so a sonic point is never left undamped either.
    const Real spread_minus =
        std::max(0.0, std::max(lambda[0] - (unL - aL), (unR - aR) - lambda[0]));
    lambda[0] = harten(lambda[0], std::max(spread_minus, linear_floor));

    // Entropy and shear waves share the eigenvalue u.n.
    lambda[1] = harten(lambda[1], linear_floor);

    // Acoustic wave u + a.
    const Real spread_plus =
        std::max(0.0, std::max(lambda[2] - (unL + aL), (unR + aR) - lambda[2]));
    lambda[2] = harten(lambda[2], std::max(spread_plus, linear_floor));
  }

  // Jumps.
  const Real drho = rhoR - rhoL;
  const Real dp = pR - pL;
  const Real du = uR - uL;
  const Real dv = vR - vL;
  const Real dun = du * n.x + dv * n.y;

  // Wave amplitudes in the characteristic basis.
  const Real alpha_1 = 0.5 * (dp - rho_hat * a_hat * dun) / a2_hat;  // u - a
  const Real alpha_3 = 0.5 * (dp + rho_hat * a_hat * dun) / a2_hat;  // u + a
  const Real alpha_2 = drho - dp / a2_hat;                            // entropy
  // Shear wave amplitude (tangential velocity jump).
  const Real tx = -n.y;
  const Real ty = n.x;
  const Real dut = du * tx + dv * ty;

  // Right eigenvectors, assembled as the dissipation sum.
  ConsVec diss{};
  // Acoustic u - a.
  {
    const Real k0 = 1.0;
    const Real k1 = u_hat - a_hat * n.x;
    const Real k2 = v_hat - a_hat * n.y;
    const Real k3 = h_hat - a_hat * un_hat;
    const Real w = lambda[0] * alpha_1;
    diss[kRho] += w * k0;
    diss[kRhoU] += w * k1;
    diss[kRhoV] += w * k2;
    diss[kRhoE] += w * k3;
  }
  // Entropy wave.
  {
    const Real k0 = 1.0;
    const Real k1 = u_hat;
    const Real k2 = v_hat;
    const Real k3 = 0.5 * q2_hat;
    const Real w = lambda[1] * alpha_2;
    diss[kRho] += w * k0;
    diss[kRhoU] += w * k1;
    diss[kRhoV] += w * k2;
    diss[kRhoE] += w * k3;
  }
  // Shear wave.
  {
    const Real w = lambda[1] * rho_hat * dut;
    diss[kRhoU] += w * tx;
    diss[kRhoV] += w * ty;
    diss[kRhoE] += w * (u_hat * tx + v_hat * ty);
  }
  // Acoustic u + a.
  {
    const Real k0 = 1.0;
    const Real k1 = u_hat + a_hat * n.x;
    const Real k2 = v_hat + a_hat * n.y;
    const Real k3 = h_hat + a_hat * un_hat;
    const Real w = lambda[2] * alpha_3;
    diss[kRho] += w * k0;
    diss[kRhoU] += w * k1;
    diss[kRhoV] += w * k2;
    diss[kRhoE] += w * k3;
  }

  const ConsVec fL = eulerNormalFlux(gas, WL, n);
  const ConsVec fR = eulerNormalFlux(gas, WR, n);
  ConsVec f{};
  for (int k = 0; k < kNumVars; ++k) {
    f[k] = 0.5 * (fL[k] + fR[k]) - 0.5 * diss[k];
  }
  return f;
}

}  // namespace

ConsVec riemannFlux(RiemannFluxType type, const PerfectGas &gas, const PrimVec &WL,
                    const PrimVec &WR, Vec2 n, Real dissipation_scale, Real &max_wave_speed) {
  switch (type) {
    case RiemannFluxType::kRusanov:
      return rusanovFlux(gas, WL, WR, n, dissipation_scale, max_wave_speed);
    case RiemannFluxType::kHllc:
      return hllcFlux(gas, WL, WR, n, max_wave_speed);
    case RiemannFluxType::kRoeEntropyFix: {
      bool fallback = false;
      const ConsVec f = roeFlux(gas, WL, WR, n, max_wave_speed, fallback);
      if (!fallback) return f;
      // Roe average inadmissible: use HLLC, which stays positive.
      return hllcFlux(gas, WL, WR, n, max_wave_speed);
    }
  }
  throw CnsError("internal error: unhandled Riemann flux type");
}

}  // namespace cns2d
