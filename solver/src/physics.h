#pragma once

#include "common.h"

namespace cfd {

struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double Pr = 0.72;
  double mu = 0.0;  // constant dynamic viscosity (0 => inviscid)

  double cp() const { return gamma * R / (gamma - 1.0); }
  double kappa() const { return mu * cp() / Pr; }
};

struct Prim {
  double rho = 0.0, u = 0.0, v = 0.0, p = 0.0;
};

struct Cons {
  double rho = 0.0, rhou = 0.0, rhov = 0.0, rhoE = 0.0;
};

inline Prim to_prim(const Cons& U, const GasModel& gas) {
  Prim w;
  w.rho = U.rho;
  w.u = U.rhou / U.rho;
  w.v = U.rhov / U.rho;
  double ke = 0.5 * (w.u * w.u + w.v * w.v);
  w.p = (gas.gamma - 1.0) * (U.rhoE - U.rho * ke);
  return w;
}

inline Cons to_cons(const Prim& w, const GasModel& gas) {
  Cons U;
  U.rho = w.rho;
  U.rhou = w.rho * w.u;
  U.rhov = w.rho * w.v;
  U.rhoE = w.p / (gas.gamma - 1.0) + 0.5 * w.rho * (w.u * w.u + w.v * w.v);
  return U;
}

inline double pressure(const Cons& U, const GasModel& gas) {
  double u = U.rhou / U.rho, v = U.rhov / U.rho;
  return (gas.gamma - 1.0) * (U.rhoE - 0.5 * U.rho * (u * u + v * v));
}

inline double speed_of_sound(const Prim& w, const GasModel& gas) {
  return std::sqrt(gas.gamma * w.p / w.rho);
}

inline double mach_number(const Prim& w, const GasModel& gas) {
  double a = speed_of_sound(w, gas);
  return std::sqrt(w.u * w.u + w.v * w.v) / a;
}

// Euler flux in direction n (unit normal).
inline Cons flux_euler(const Prim& w, double nx, double ny, const GasModel& gas) {
  double un = w.u * nx + w.v * ny;
  double H = (w.p / w.rho) * (gas.gamma / (gas.gamma - 1.0)) + 0.5 * (w.u * w.u + w.v * w.v);
  Cons F;
  F.rho = w.rho * un;
  F.rhou = w.rho * un * w.u + w.p * nx;
  F.rhov = w.rho * un * w.v + w.p * ny;
  F.rhoE = w.rho * un * H;
  return F;
}

// Rusanov / local Lax-Friedrichs flux.
inline Cons flux_rusanov(const Prim& wl, const Prim& wr, const Cons& Ul, const Cons& Ur,
                         double nx, double ny, const GasModel& gas, double dscale) {
  Cons Fl = flux_euler(wl, nx, ny, gas);
  Cons Fr = flux_euler(wr, nx, ny, gas);
  double al = std::abs(wl.u * nx + wl.v * ny) + speed_of_sound(wl, gas);
  double ar = std::abs(wr.u * nx + wr.v * ny) + speed_of_sound(wr, gas);
  double lambda = dscale * std::max(al, ar);
  Cons F;
  F.rho = 0.5 * (Fl.rho + Fr.rho) - 0.5 * lambda * (Ur.rho - Ul.rho);
  F.rhou = 0.5 * (Fl.rhou + Fr.rhou) - 0.5 * lambda * (Ur.rhou - Ul.rhou);
  F.rhov = 0.5 * (Fl.rhov + Fr.rhov) - 0.5 * lambda * (Ur.rhov - Ul.rhov);
  F.rhoE = 0.5 * (Fl.rhoE + Fr.rhoE) - 0.5 * lambda * (Ur.rhoE - Ul.rhoE);
  return F;
}

// Roe flux with Harten-Yee entropy fix. eps_fix ~ 0.1 is a typical value.
Cons flux_roe(const Prim& wl, const Prim& wr, const Cons& Ul, const Cons& Ur,
              double nx, double ny, const GasModel& gas, double eps_fix);

// Viscous flux dotted with the (scaled) face normal; len = face length.
// w is the face state, g* are averaged primitive gradients.
inline Cons viscous_flux(const Prim& w, double ux, double uy, double vx, double vy,
                         double Tx, double Ty, double nx, double ny, double len,
                         const GasModel& gas) {
  double mu = gas.mu;
  if (mu == 0.0) return Cons{0.0, 0.0, 0.0, 0.0};
  double div = ux + vy;
  double txx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  double tyy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  double txy = mu * (uy + vx);
  double k = gas.kappa();
  double qx = -k * Tx;
  double qy = -k * Ty;
  Cons F;
  F.rho = 0.0;
  F.rhou = (txx * nx + txy * ny) * len;
  F.rhov = (txy * nx + tyy * ny) * len;
  F.rhoE = ((w.u * txx + w.v * txy - qx) * nx +
            (w.u * txy + w.v * tyy - qy) * ny) * len;
  return F;
}

}  // namespace cfd
