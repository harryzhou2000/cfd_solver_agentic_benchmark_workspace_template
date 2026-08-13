#pragma once
// Phase 3: physics helpers — primitive variables, EOS (calorically perfect
// gas), freestream-derived quantities, and viscosity.
//
// All quantities are nondimensionalized (the case JSON supplies freestream
// density 1.0, velocity magnitude 1.0, and the pressure consistent with the
// Mach number through the calorically-perfect EOS), so the helpers operate
// purely on the nondimensional state.

#include "types.h"

namespace cfd {

// Primitive (non-conservative) flow state.
struct PrimState {
  double rho = 0.0;
  double u = 0.0;
  double v = 0.0;
  double p = 0.0;
  double T = 0.0;     // temperature T = p / (rho * R)
  double mach = 0.0;  // local Mach number |V| / a
  double a = 0.0;     // speed of sound
};

// Pressure from the conserved state:
//   p = (gamma-1) * (rhoE - 0.5*(rhou^2 + rhov^2)/rho)
double pressure_from_cons(const ConsState& U, const GasConfig& gas);

// Speed of sound: a = sqrt(gamma * p / rho)
double speed_of_sound(const ConsState& U, const GasConfig& gas);

// Temperature: T = p / (rho * R)
double temperature(const ConsState& U, const GasConfig& gas);

// Full primitive state (all derived quantities populated).
PrimState cons_to_prim(const ConsState& U, const GasConfig& gas);

// Conserved state from primitives:
//   rhoE = p/(gamma-1) + 0.5*rho*(u^2 + v^2)
// P.T / P.mach / P.a are derived quantities and are ignored on input.
ConsState prim_to_cons(const PrimState& P, const GasConfig& gas);

// Local Mach number of a cell: |V| / a.
double mach_number(const ConsState& U, const GasConfig& gas);

// Constant viscosity from the freestream Reynolds number:
//   mu = rho_inf * u_inf * L_ref / Re
// Returns 0 for Re <= 0 (inviscid).
double viscosity_from_reynolds(double rho_inf, double u_inf, double L_ref,
                               double Re);

// Freestream primitive / conserved states (from the case freestream block).
PrimState freestream_to_prim(const Freestream& fs, const GasConfig& gas);
ConsState freestream_to_cons(const Freestream& fs, const GasConfig& gas);

// Reference dynamic pressure q_inf = 0.5 * rho_inf * u_inf^2.
double dynamic_pressure(const Freestream& fs);

}  // namespace cfd
