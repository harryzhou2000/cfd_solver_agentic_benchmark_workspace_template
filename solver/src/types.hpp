// Core numerical types for the 2-D compressible Navier-Stokes solver.
// Conservative state U = [rho, rho*u, rho*v, rho*E], calorically perfect gas.
// These structs are deliberately plain (POD) so they can be packed into MPI
// buffers and stored contiguously for cache-friendly residual assembly.
#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <string>

namespace cfd {

constexpr int NEQ = 4;   // conservative variables per cell

struct Vec2 {
  double x = 0.0, y = 0.0;
  Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(double s) const { return {x * s, y * s}; }
};
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double cross_z(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double len(const Vec2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// Conservative state. Index 0..3 = rho, rhou, rhov, rhoE.
struct Cons {
  double v[NEQ] = {0, 0, 0, 0};
  double& rho()  { return v[0]; }
  double& rhou() { return v[1]; }
  double& rhov() { return v[2]; }
  double& rhoE() { return v[3]; }
  double rho()  const { return v[0]; }
  double rhou() const { return v[1]; }
  double rhov() const { return v[2]; }
  double rhoE() const { return v[3]; }
  Cons operator+(const Cons& o) const { Cons r; for (int i = 0; i < NEQ; ++i) r.v[i] = v[i] + o.v[i]; return r; }
  Cons operator-(const Cons& o) const { Cons r; for (int i = 0; i < NEQ; ++i) r.v[i] = v[i] - o.v[i]; return r; }
  Cons operator*(double s) const { Cons r; for (int i = 0; i < NEQ; ++i) r.v[i] = v[i] * s; return r; }
  Cons& operator+=(const Cons& o) { for (int i = 0; i < NEQ; ++i) v[i] += o.v[i]; return *this; }
  Cons& operator-=(const Cons& o) { for (int i = 0; i < NEQ; ++i) v[i] -= o.v[i]; return *this; }
};

// Primitive state, convenient for reconstruction and fluxes.
struct Prim {
  double rho = 0, u = 0, v = 0, p = 0;
};

// Calorically perfect gas. Configured from the case file (gamma, R, Pr).
struct GasModel {
  double gamma = 1.4;
  double R = 1.0;        // specific gas constant (nondimensional here)
  double Pr = 0.72;
  double gm1() const { return gamma - 1.0; }
  // cp = gamma * R / (gamma-1); cv = R/(gamma-1)
  double cp() const { return gamma * R / gm1(); }
  double cv() const { return R / gm1(); }
  double soundSpeed(const Prim& w) const { return std::sqrt(gamma * std::abs(w.p) / std::max(w.rho, 1e-30)); }
  double temperature(const Prim& w) const { return w.p / (R * std::max(w.rho, 1e-30)); }
  // pressure from conservative state: p = (gamma-1)*(rhoE - 0.5*rho*(u^2+v^2))
  double pressureFromCons(const Cons& U) const {
    double rho = U.rho();
    double rhoEu = U.rhou(), rhoEv = U.rhov();
    double ke = 0.5 * (rhoEu * rhoEu + rhoEv * rhoEv) / std::max(rho, 1e-30);
    return gm1() * (U.rhoE() - ke);
  }
  Prim primFromCons(const Cons& U) const {
    Prim w;
    w.rho = U.rho();
    double inv = 1.0 / std::max(w.rho, 1e-30);
    w.u = U.rhou() * inv;
    w.v = U.rhov() * inv;
    double ke = 0.5 * (w.u * w.u + w.v * w.v);
    w.p = gm1() * (U.rhoE() * inv - ke);
    return w;
  }
  Cons consFromPrim(const Prim& w) const {
    Cons U;
    U.rho() = w.rho;
    U.rhou() = w.rho * w.u;
    U.rhov() = w.rho * w.v;
    double ke = 0.5 * (w.u * w.u + w.v * w.v);
    U.rhoE() = w.p / gm1() + w.rho * ke;
    return U;
  }
  // total energy per mass
  double E(const Prim& w) const { double ke = 0.5 * (w.u * w.u + w.v * w.v); return w.p / (w.rho * gm1()) + ke; }
};

// Boundary-condition kinds required by the cases.
enum class BCType : int {
  Internal = 0,
  Farfield = 1,
  SlipWall = 2,
  NoSlipAdiabaticWall = 3,
};
inline const char* bcTypeName(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
    default: return "internal";
  }
}

}  // namespace cfd
