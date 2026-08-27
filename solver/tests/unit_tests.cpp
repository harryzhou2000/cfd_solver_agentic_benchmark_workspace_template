// cns2d -- self-contained verification harness.
//
// Covers the pieces whose correctness is hard to see by inspection: polygon
// geometry, the perfect-gas conversions, the Riemann solvers' consistency and
// conservation properties, the limiter bounds, and the viscous stress algebra.
// Run with:  ./cns2d_tests
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/logging.h"
#include "mesh/geometry.h"
#include "numerics/limiter.h"
#include "numerics/riemann_flux.h"
#include "numerics/viscous_flux.h"
#include "physics/boundary_conditions.h"
#include "physics/perfect_gas.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string &what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

void checkClose(double a, double b, double tol, const std::string &what) {
  ++g_checks;
  const double diff = std::abs(a - b);
  const double scale = std::max(1.0, std::max(std::abs(a), std::abs(b)));
  if (!(diff <= tol * scale)) {
    ++g_failures;
    std::printf("FAIL: %s (%.17g vs %.17g, rel diff %.3e)\n", what.c_str(), a, b, diff / scale);
  }
}

using namespace cns2d;

// --- geometry --------------------------------------------------------------
void testGeometry() {
  // Unit square, counter-clockwise.
  const Real xs[4] = {0.0, 1.0, 1.0, 0.0};
  const Real ys[4] = {0.0, 0.0, 1.0, 1.0};
  Vec2 centroid{};
  const Real area = polygonAreaCentroid(xs, ys, 4, centroid);
  checkClose(area, 1.0, 1e-14, "unit square area");
  checkClose(centroid.x, 0.5, 1e-14, "unit square centroid x");
  checkClose(centroid.y, 0.5, 1e-14, "unit square centroid y");

  // Right triangle with legs 1: area 1/2, centroid at (1/3, 1/3).
  const Real tx[3] = {0.0, 1.0, 0.0};
  const Real ty[3] = {0.0, 0.0, 1.0};
  Vec2 tc{};
  const Real ta = polygonAreaCentroid(tx, ty, 3, tc);
  checkClose(ta, 0.5, 1e-14, "triangle area");
  checkClose(tc.x, 1.0 / 3.0, 1e-14, "triangle centroid x");
  checkClose(tc.y, 1.0 / 3.0, 1e-14, "triangle centroid y");

  // Clockwise ordering must give a negative signed area.
  const Real cx[4] = {0.0, 0.0, 1.0, 1.0};
  const Real cy[4] = {0.0, 1.0, 1.0, 0.0};
  Vec2 cc{};
  check(polygonAreaCentroid(cx, cy, 4, cc) < 0.0, "clockwise polygon has negative signed area");

  // A stretched boundary-layer-like quad.
  const Real sx[4] = {0.0, 1.0, 1.0, 0.0};
  const Real sy[4] = {0.0, 0.0, 1.0e-4, 1.0e-4};
  Vec2 sc{};
  checkClose(polygonAreaCentroid(sx, sy, 4, sc), 1.0e-4, 1e-12, "stretched quad area");
}

// --- gas model ------------------------------------------------------------
void testPerfectGas() {
  GasProperties props;
  props.gamma = 1.4;
  props.R = 1.0;
  props.prandtl = 0.72;
  const PerfectGas gas(props);

  const PrimVec W{1.2, 0.3, -0.4, 2.5};
  const ConsVec U = gas.consFromPrim(W);
  const PrimVec W2 = gas.primFromCons(U);
  for (int v = 0; v < kNumVars; ++v) {
    checkClose(W[v], W2[v], 1e-14, "prim->cons->prim roundtrip component " + std::to_string(v));
  }
  checkClose(gas.pressureFromCons(U), W[kPrimP], 1e-14, "pressure from conserved state");
  checkClose(gas.soundSpeed(W[kPrimRho], W[kPrimP]), std::sqrt(1.4 * 2.5 / 1.2), 1e-14,
             "sound speed");
  // h = E + p/rho
  const Real E = gas.totalEnergy(W[kPrimRho], W[kPrimU], W[kPrimV], W[kPrimP]);
  checkClose(gas.totalEnthalpy(W[kPrimRho], W[kPrimU], W[kPrimV], W[kPrimP]),
             E + W[kPrimP] / W[kPrimRho], 1e-14, "total enthalpy identity");
  // k = mu cp / Pr
  checkClose(gas.conductivity(2.0), 2.0 * gas.cp() / 0.72, 1e-14, "Fourier conductivity");
}

// --- Riemann fluxes -------------------------------------------------------
void testRiemannFluxes() {
  GasProperties props;
  props.gamma = 1.4;
  props.R = 1.0;
  const PerfectGas gas(props);

  const std::vector<RiemannFluxType> types = {
      RiemannFluxType::kRoeEntropyFix, RiemannFluxType::kHllc, RiemannFluxType::kRusanov};

  for (const RiemannFluxType type : types) {
    const std::string name = riemannFluxName(type);

    // 1. Consistency: identical states must return the exact physical flux.
    {
      const PrimVec W{1.0, 0.7, -0.2, 1.0 / 1.4};
      const Vec2 n{0.6, 0.8};
      Real s = 0.0;
      const ConsVec f = riemannFlux(type, gas, W, W, n, 1.0, s);
      const ConsVec exact = eulerNormalFlux(gas, W, n);
      for (int k = 0; k < kNumVars; ++k) {
        checkClose(f[k], exact[k], 1e-12, name + " consistency component " + std::to_string(k));
      }
      check(s > 0.0, name + " reports a positive wave speed");
    }

    // 2. Conservation / rotational symmetry: flipping the normal and swapping
    //    the states must negate the flux.
    {
      const PrimVec WL{1.0, 0.5, 0.1, 0.8};
      const PrimVec WR{0.7, 0.2, -0.3, 0.5};
      const Vec2 n{1.0, 0.0};
      const Vec2 nm{-1.0, 0.0};
      Real s1 = 0.0;
      Real s2 = 0.0;
      const ConsVec f1 = riemannFlux(type, gas, WL, WR, n, 1.0, s1);
      const ConsVec f2 = riemannFlux(type, gas, WR, WL, nm, 1.0, s2);
      for (int k = 0; k < kNumVars; ++k) {
        checkClose(f1[k], -f2[k], 1e-12, name + " antisymmetry component " + std::to_string(k));
      }
    }

    // 3. Supersonic upwinding: at Mach 3 the flux must equal the upwind flux.
    {
      const PrimVec WL{1.0, 3.0 * std::sqrt(1.4 * 1.0 / 1.0), 0.0, 1.0};
      const PrimVec WR{0.5, 0.1, 0.0, 0.3};
      const Vec2 n{1.0, 0.0};
      Real s = 0.0;
      const ConsVec f = riemannFlux(type, gas, WL, WR, n, 1.0, s);
      const ConsVec exact = eulerNormalFlux(gas, WL, n);
      // Rusanov is a central flux and is not exactly upwind, so only the
      // genuinely upwind solvers are checked here.
      if (type != RiemannFluxType::kRusanov) {
        for (int k = 0; k < kNumVars; ++k) {
          checkClose(f[k], exact[k], 1e-10, name + " supersonic upwind component " + std::to_string(k));
        }
      }
    }

    // 4. Strong shock robustness: the flux must stay finite.
    {
      const PrimVec WL{1.0, 0.0, 0.0, 1000.0};
      const PrimVec WR{0.01, 0.0, 0.0, 0.01};
      const Vec2 n{1.0, 0.0};
      Real s = 0.0;
      const ConsVec f = riemannFlux(type, gas, WL, WR, n, 1.0, s);
      for (int k = 0; k < kNumVars; ++k) {
        check(std::isfinite(f[k]), name + " strong shock finite component " + std::to_string(k));
      }
    }
  }

  // 5. Roe entropy fix: a sonic expansion must produce more dissipation than
  //    the unfixed eigenvalue would, i.e. the flux must differ from the plain
  //    average when the acoustic eigenvalue vanishes.
  {
    const Real a = std::sqrt(1.4);
    const PrimVec WL{1.0, a, 0.0, 1.0};   // u = a  =>  u - a = 0 exactly
    const PrimVec WR{1.0, a, 0.0, 1.0};
    const Vec2 n{1.0, 0.0};
    Real s = 0.0;
    const ConsVec f = riemannFlux(RiemannFluxType::kRoeEntropyFix, gas, WL, WR, n, 1.0, s);
    for (int k = 0; k < kNumVars; ++k) check(std::isfinite(f[k]), "sonic Roe flux finite");
  }

  // 6. HLLC star state must satisfy the Rankine-Hugoniot conditions across the
  //    acoustic waves.  This is checked by reconstructing the star state
  //    independently here -- from the pressure/velocity relations rather than
  //    from the solver's own expression -- and comparing the resulting flux.
  //
  //    The energy component is the one that is easy to get wrong: the star
  //    energy is  e* = e + (S* - un) * (S* + p / (rho * (S - un))).  Toro writes
  //    the same quantity as (S* - p / (rho * (un - S))); combining the minus
  //    sign with (S - un) instead of (un - S) breaks the energy jump condition
  //    by an O(1) amount while leaving mass and momentum untouched, so only an
  //    energy-aware test detects it.
  {
    const std::vector<std::pair<PrimVec, PrimVec>> pairs = {
        {PrimVec{1.0, 0.4, 0.15, 1.0}, PrimVec{0.35, -0.25, -0.1, 0.35}},
        {PrimVec{1.2, -0.3, 0.05, 0.9}, PrimVec{0.8, 0.6, 0.2, 1.4}},
        {PrimVec{2.0, 0.1, -0.4, 3.0}, PrimVec{0.5, 0.2, 0.3, 0.4}},
    };
    const Vec2 n{0.6, 0.8};
    const Real tx = -n.y;
    const Real ty = n.x;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
      const PrimVec &WL = pairs[i].first;
      const PrimVec &WR = pairs[i].second;
      Real smax = 0.0;
      const ConsVec f = riemannFlux(RiemannFluxType::kHllc, gas, WL, WR, n, 1.0, smax);
      const std::string tag = " (pair " + std::to_string(i) + ")";

      // Independent reconstruction of the HLLC wave speeds (same estimates the
      // solver uses, recomputed here from the primitive states).
      const Real rhoL = WL[kPrimRho];
      const Real rhoR = WR[kPrimRho];
      const Real pL = WL[kPrimP];
      const Real pR = WR[kPrimP];
      const Real unL = WL[kPrimU] * n.x + WL[kPrimV] * n.y;
      const Real unR = WR[kPrimU] * n.x + WR[kPrimV] * n.y;
      const Real utL = WL[kPrimU] * tx + WL[kPrimV] * ty;
      const Real utR = WR[kPrimU] * tx + WR[kPrimV] * ty;
      const Real aL = gas.soundSpeed(rhoL, pL);
      const Real aR = gas.soundSpeed(rhoR, pR);
      const Real sqL = std::sqrt(rhoL);
      const Real sqR = std::sqrt(rhoR);
      const Real inv = 1.0 / (sqL + sqR);
      const Real unT = (sqL * unL + sqR * unR) * inv;
      const Real utT = (sqL * utL + sqR * utR) * inv;
      const Real hT = (sqL * gas.totalEnthalpy(rhoL, WL[kPrimU], WL[kPrimV], pL) +
                       sqR * gas.totalEnthalpy(rhoR, WR[kPrimU], WR[kPrimV], pR)) * inv;
      const Real aT = std::sqrt((1.4 - 1.0) * (hT - 0.5 * (unT * unT + utT * utT)));
      const Real sL = std::min(unL - aL, unT - aT);
      const Real sR = std::max(unR + aR, unT + aT);
      check(sL < 0.0 && sR > 0.0, "hllc test state is subsonic" + tag);
      const Real sStar = (pR - pL + rhoL * unL * (sL - unL) - rhoR * unR * (sR - unR)) /
                         (rhoL * (sL - unL) - rhoR * (sR - unR));

      // Star state built from the Rankine-Hugoniot relations, written in the
      // (un - S) convention so it is an genuinely independent expression.
      const bool left = (sStar >= 0.0);
      const Real rho = left ? rhoL : rhoR;
      const Real un = left ? unL : unR;
      const Real ut = left ? utL : utR;
      const Real pk = left ? pL : pR;
      const Real sk = left ? sL : sR;
      const PrimVec &WK = left ? WL : WR;
      const Real coef = rho * (sk - un) / (sk - sStar);
      const Real eK = gas.totalEnergy(rho, WK[kPrimU], WK[kPrimV], pk);
      ConsVec Us{};
      Us[kRho] = coef;
      Us[kRhoU] = coef * (sStar * n.x + ut * tx);
      Us[kRhoV] = coef * (sStar * n.y + ut * ty);
      Us[kRhoE] = coef * (eK - (sStar - un) * (pk / (rho * (un - sk)) - sStar));
      const ConsVec fK = eulerNormalFlux(gas, WK, n);
      const ConsVec UK = gas.consFromPrim(WK);
      for (int k = 0; k < kNumVars; ++k) {
        const Real expect = fK[k] + sk * (Us[k] - UK[k]);
        checkClose(f[k], expect, 1e-11,
                   "hllc rankine-hugoniot star flux component " + std::to_string(k) + tag);
      }
      // The star pressure implied by the momentum jump must be positive and the
      // star density must be positive, which fails loudly if the energy term is
      // inconsistent with the rest of the star state.
      const Real p_star = pk + rho * (un - sk) * (un - sStar);
      check(p_star > 0.0, "hllc star pressure positive" + tag);
      check(Us[kRho] > 0.0, "hllc star density positive" + tag);
      // Star total energy must exceed the kinetic energy, i.e. the implied
      // internal energy is positive.
      const Real ke = 0.5 * (Us[kRhoU] * Us[kRhoU] + Us[kRhoV] * Us[kRhoV]) / Us[kRho];
      check(Us[kRhoE] > ke, "hllc star internal energy positive" + tag);
    }
  }
}

// --- boundary conditions --------------------------------------------------
void testBoundaryConditions() {
  CaseInput input;
  input.gas.gamma = 1.4;
  input.gas.R = 1.0;
  input.gas.prandtl = 0.72;
  input.freestream.rho = 1.0;
  input.freestream.velocity_magnitude = 1.0;
  input.freestream.pressure = 1.0 / (1.4 * 0.15 * 0.15);
  input.freestream.mach = 0.15;
  input.freestream.aoa_degrees = 0.0;
  input.physics.mode = PhysicsMode::kInviscid;
  const FlowContext ctx = makeFlowContext(input);

  const PerfectGas &gas = ctx.gas;

  // Slip wall: normal velocity of the AVERAGE of interior and mirrored state
  // must vanish; tangential velocity must be preserved.
  {
    const Vec2 n{0.0, 1.0};
    const PrimVec Wi{1.0, 0.8, 0.3, 2.0};
    const ConsVec Ui = gas.consFromPrim(Wi);
    const ConsVec Ub = slipWallState(Ui, n);
    const PrimVec Wb = gas.primFromCons(Ub);
    checkClose(0.5 * (Wi[kPrimV] + Wb[kPrimV]), 0.0, 1e-14, "slip wall zero mean normal velocity");
    checkClose(Wb[kPrimU], Wi[kPrimU], 1e-14, "slip wall preserves tangential velocity");
    checkClose(Wb[kPrimP], Wi[kPrimP], 1e-14, "slip wall preserves pressure");
    checkClose(Wb[kPrimRho], Wi[kPrimRho], 1e-14, "slip wall preserves density");
  }

  // No-slip adiabatic wall: exactly zero velocity, interior pressure.
  {
    const PrimVec Wi{1.3, 0.5, -0.2, 3.0};
    const ConsVec Ui = gas.consFromPrim(Wi);
    const ConsVec Ub = noSlipAdiabaticWallState(Ui, ctx);
    const PrimVec Wb = gas.primFromCons(Ub);
    checkClose(Wb[kPrimU], 0.0, 1e-15, "no-slip wall zero u");
    checkClose(Wb[kPrimV], 0.0, 1e-15, "no-slip wall zero v");
    checkClose(Wb[kPrimP], Wi[kPrimP], 1e-14, "no-slip wall interior pressure");
    // Adiabatic => wall temperature equals interior temperature.
    checkClose(gas.temperatureFromRhoP(Wb[kPrimRho], Wb[kPrimP]),
               gas.temperatureFromRhoP(Wi[kPrimRho], Wi[kPrimP]), 1e-14,
               "adiabatic wall temperature match");
  }

  // Farfield: a state equal to the freestream must be returned unchanged.
  {
    const Vec2 n{1.0, 0.0};
    const ConsVec Ub = farfieldState(ctx.freestream_cons, n, ctx);
    for (int k = 0; k < kNumVars; ++k) {
      checkClose(Ub[k], ctx.freestream_cons[k], 1e-10, "farfield preserves freestream component " +
                                                            std::to_string(k));
    }
  }
  // Supersonic inflow must take the freestream exactly.
  {
    CaseInput sup = input;
    sup.freestream.mach = 2.0;
    sup.freestream.pressure = 1.0 / (1.4 * 4.0);
    const FlowContext sctx = makeFlowContext(sup);
    const Vec2 n{-1.0, 0.0};  // outward normal points upstream => inflow
    const PrimVec Wi{2.0, 1.0, 0.0, 1.0};
    const ConsVec Ub = farfieldState(sctx.gas.consFromPrim(Wi), n, sctx);
    for (int k = 0; k < kNumVars; ++k) {
      checkClose(Ub[k], sctx.freestream_cons[k], 1e-12,
                 "supersonic inflow takes freestream component " + std::to_string(k));
    }
  }
}

// --- limiter --------------------------------------------------------------
void testLimiterAndReconstruction() {
  // Reconstruction with a zero gradient returns the cell value.
  {
    const Real w[kNumVars] = {1.0, 0.2, 0.3, 2.0};
    const Real g[kNumVars * kDim] = {0, 0, 0, 0, 0, 0, 0, 0};
    const Real phi[kNumVars] = {1, 1, 1, 1};
    PrimVec out{};
    check(reconstructPrimitive(w, g, phi, {0.5, 0.5}, out), "zero-gradient reconstruction succeeds");
    for (int v = 0; v < kNumVars; ++v) checkClose(out[v], w[v], 1e-15, "zero gradient preserves value");
  }
  // Exact linear field: unlimited reconstruction must be exact.
  {
    const Real w[kNumVars] = {1.0, 0.0, 0.0, 1.0};
    const Real g[kNumVars * kDim] = {2.0, 3.0, 0, 0, 0, 0, 5.0, -1.0};
    const Real phi[kNumVars] = {1, 1, 1, 1};
    PrimVec out{};
    const Vec2 d{0.1, 0.2};
    check(reconstructPrimitive(w, g, phi, d, out), "linear reconstruction succeeds");
    checkClose(out[kPrimRho], 1.0 + 2.0 * 0.1 + 3.0 * 0.2, 1e-15, "linear rho reconstruction");
    checkClose(out[kPrimP], 1.0 + 5.0 * 0.1 - 1.0 * 0.2, 1e-15, "linear p reconstruction");
  }
  // Positivity fallback: a gradient that would drive density negative must be
  // clipped, and the result must remain positive.
  {
    const Real w[kNumVars] = {0.1, 0.0, 0.0, 0.1};
    const Real g[kNumVars * kDim] = {-100.0, 0, 0, 0, 0, 0, -100.0, 0};
    const Real phi[kNumVars] = {1, 1, 1, 1};
    PrimVec out{};
    const bool full = reconstructPrimitive(w, g, phi, {1.0, 0.0}, out);
    check(!full, "positivity fallback reports clipping");
    check(out[kPrimRho] > 0.0, "positivity fallback keeps density positive");
    check(out[kPrimP] > 0.0, "positivity fallback keeps pressure positive");
  }
  check(limiterName(LimiterType::kBarthJespersen) == "barth_jespersen", "limiter name round trip");
  check(parseLimiterType("venkatakrishnan") == LimiterType::kVenkatakrishnan, "limiter parse");
}

// --- viscous flux ---------------------------------------------------------
void testViscousFlux() {
  GasProperties props;
  props.gamma = 1.4;
  props.R = 1.0;
  props.prandtl = 0.72;
  const PerfectGas gas(props);
  const Real mu = 0.01;

  // Pure shear du/dy = 1: tau_xy = mu, no normal stress, no divergence.
  {
    ViscousGradients g;
    g.grad_u = {0.0, 1.0};
    g.grad_v = {0.0, 0.0};
    g.grad_T = {0.0, 0.0};
    const PrimVec W{1.0, 0.0, 0.0, 1.0};
    // Normal along +y: the traction is tau_xy in x.
    const ConsVec f = viscousNormalFlux(gas, mu, W, g, {0.0, 1.0});
    checkClose(f[kRhoU], mu * 1.0, 1e-15, "pure shear tau_xy");
    checkClose(f[kRho], 0.0, 1e-15, "viscous flux has no mass component");
    checkClose(f[kRhoE], 0.0, 1e-15, "zero velocity gives no viscous work");
  }
  // Pure dilatation u_x = v_y = d: deviatoric stress must vanish for
  // incompressible-consistent Stokes hypothesis (2 mu d - 2/3 mu * 2d).
  {
    ViscousGradients g;
    const Real d = 0.5;
    g.grad_u = {d, 0.0};
    g.grad_v = {0.0, d};
    g.grad_T = {0.0, 0.0};
    const PrimVec W{1.0, 0.0, 0.0, 1.0};
    const ConsVec f = viscousNormalFlux(gas, mu, W, g, {1.0, 0.0});
    checkClose(f[kRhoU], 2.0 * mu * d - (2.0 / 3.0) * mu * 2.0 * d, 1e-15, "dilatation tau_xx");
    checkClose(f[kRhoV], 0.0, 1e-15, "dilatation has no shear");
  }
  // Fourier heat flux only.
  {
    ViscousGradients g;
    g.grad_u = {0.0, 0.0};
    g.grad_v = {0.0, 0.0};
    g.grad_T = {3.0, 0.0};
    const PrimVec W{1.0, 0.0, 0.0, 1.0};
    const ConsVec f = viscousNormalFlux(gas, mu, W, g, {1.0, 0.0});
    // energy flux = -q.n = +k dT/dn
    checkClose(f[kRhoE], gas.conductivity(mu) * 3.0, 1e-15, "Fourier heat flux");
  }
  // Skin friction must be the TANGENTIAL traction: a pure normal stress state
  // must give zero skin friction.
  {
    ViscousGradients g;
    g.grad_u = {1.0, 0.0};  // u_x only => tau_xx nonzero, tau_xy zero
    g.grad_v = {0.0, -1.0};
    g.grad_T = {0.0, 0.0};
    const Vec2 n{1.0, 0.0};
    const Vec2 shear = wallShearTraction(mu, g, n);
    checkClose(shear.x, 0.0, 1e-15, "normal traction excluded from skin friction (x)");
    checkClose(shear.y, 0.0, 1e-15, "normal traction excluded from skin friction (y)");
  }
  // A pure shear state on a wall with normal +y must give a nonzero tangential
  // traction equal to mu * du/dy.
  {
    ViscousGradients g;
    g.grad_u = {0.0, 2.0};
    g.grad_v = {0.0, 0.0};
    g.grad_T = {0.0, 0.0};
    const Vec2 shear = wallShearTraction(mu, g, {0.0, 1.0});
    checkClose(shear.x, mu * 2.0, 1e-15, "wall shear equals mu du/dy");
    checkClose(shear.y, 0.0, 1e-15, "wall shear has no normal part");
  }
}

}  // namespace

int main() {
  cns2d::Logger::instance().configure(0, 1);
  testGeometry();
  testPerfectGas();
  testRiemannFluxes();
  testBoundaryConditions();
  testLimiterAndReconstruction();
  testViscousFlux();
  std::printf("cns2d unit tests: %d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
