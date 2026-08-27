#pragma once
// Inviscid Riemann fluxes (Rusanov/LLF and Roe with Harten entropy fix),
// boundary ghost states, and laminar viscous fluxes. All original code.
#include "common.hpp"
#include "gas.hpp"
#include "case_config.hpp"

namespace fv {

// Primitive state helper
struct Prim {
  double rho = 1, u = 0, v = 0, p = 1;
  double T(const Gas& g) const { return p / (rho * g.R); }
  double a(const Gas& g) const { return std::sqrt(g.gamma * p / rho); }
  State toCons(const Gas& g) const { return primToCons(rho, u, v, p, g); }
  static Prim fromCons(const State& U, const Gas& g) {
    Prim w;
    w.rho = U[0];
    w.u = U[1] / U[0];
    w.v = U[2] / U[0];
    w.p = consPressure(U, g);
    return w;
  }
};

// Physical inviscid flux in the face-normal direction n (unit vector).
State physFluxN(const Prim& w, double nx, double ny, const Gas& g);

// Rusanov / local Lax-Friedrichs flux. scale multiplies the dissipative jump.
State rusanovFlux(const Prim& wl, const Prim& wr, double nx, double ny, const Gas& g, double scale = 1.0);

// Roe flux with Harten entropy fix (rotated to the face normal frame).
State roeFlux(const Prim& wl, const Prim& wr, double nx, double ny, const Gas& g);

// Characteristic-based farfield boundary state (outward unit normal n).
Prim farfieldState(const Prim& wc, double nx, double ny, const Prim& wf, const Gas& g);

// Slip-wall ghost state (mirrored normal velocity).
Prim slipWallGhost(const Prim& wc, double nx, double ny);

// No-slip adiabatic-wall ghost state (negated velocity, same T/p/rho).
Prim noSlipGhost(const Prim& wc);

// Pressure-only wall flux used at both slip and no-slip walls.
State wallPressureFlux(double pw, double nx, double ny);

// Viscous flux in the normal direction given face gradients of velocity and
// temperature, the face velocity, and transport coefficients.
State viscousFluxN(double gux, double guy, double gvx, double gvy, double gTx, double gTy,
                   double uf, double vf, double mu, double k, double nx, double ny, const Gas& g);

}  // namespace fv
