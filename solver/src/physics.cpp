// Phase 3: physics helpers implementation (see physics.h).

#include "physics.h"

#include <cmath>

namespace cfd {

double pressure_from_cons(const ConsState& U, const GasConfig& gas) {
  const double kin =
      0.5 * (U.rhou * U.rhou + U.rhov * U.rhov) / U.rho;
  return (gas.gamma - 1.0) * (U.rhoE - kin);
}

double speed_of_sound(const ConsState& U, const GasConfig& gas) {
  const double p = pressure_from_cons(U, gas);
  return std::sqrt(gas.gamma * p / U.rho);
}

double temperature(const ConsState& U, const GasConfig& gas) {
  const double p = pressure_from_cons(U, gas);
  return p / (U.rho * gas.R);
}

PrimState cons_to_prim(const ConsState& U, const GasConfig& gas) {
  PrimState P;
  P.rho = U.rho;
  P.u = U.rhou / U.rho;
  P.v = U.rhov / U.rho;
  P.p = pressure_from_cons(U, gas);
  P.T = P.p / (P.rho * gas.R);
  P.a = std::sqrt(gas.gamma * P.p / P.rho);
  P.mach = std::sqrt(P.u * P.u + P.v * P.v) / P.a;
  return P;
}

ConsState prim_to_cons(const PrimState& P, const GasConfig& gas) {
  ConsState U;
  U.rho = P.rho;
  U.rhou = P.rho * P.u;
  U.rhov = P.rho * P.v;
  U.rhoE = P.p / (gas.gamma - 1.0) +
           0.5 * P.rho * (P.u * P.u + P.v * P.v);
  return U;
}

double mach_number(const ConsState& U, const GasConfig& gas) {
  const double a = speed_of_sound(U, gas);
  const double v = std::sqrt(U.rhou * U.rhou + U.rhov * U.rhov) / U.rho;
  return v / a;
}

double viscosity_from_reynolds(double rho_inf, double u_inf, double L_ref,
                               double Re) {
  if (Re <= 0.0) {
    return 0.0;
  }
  return rho_inf * u_inf * L_ref / Re;
}

PrimState freestream_to_prim(const Freestream& fs, const GasConfig& gas) {
  const Vec2 v = fs.velocity();
  PrimState P;
  P.rho = fs.rho;
  P.u = v.x;
  P.v = v.y;
  P.p = fs.pressure;
  P.T = P.p / (P.rho * gas.R);
  P.a = std::sqrt(gas.gamma * P.p / P.rho);
  P.mach = std::sqrt(P.u * P.u + P.v * P.v) / P.a;
  return P;
}

ConsState freestream_to_cons(const Freestream& fs, const GasConfig& gas) {
  return prim_to_cons(freestream_to_prim(fs, gas), gas);
}

double dynamic_pressure(const Freestream& fs) {
  return 0.5 * fs.rho * fs.u_mag * fs.u_mag;
}

}  // namespace cfd
