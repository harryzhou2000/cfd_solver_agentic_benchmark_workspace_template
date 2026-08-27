// Newtonian viscous stress and Fourier heat flux on unstructured faces.
#pragma once

#include "core/Types.hpp"
#include "physics/PerfectGas.hpp"

namespace cfd {

// Gradients required by the viscous flux: du, dv, dT (each a kDim vector).
struct ViscousGradients {
  Grad du{{0.0, 0.0}};
  Grad dv{{0.0, 0.0}};
  Grad dT{{0.0, 0.0}};
};

struct StressTensor {
  Real xx = 0.0, yy = 0.0, xy = 0.0;
};

inline StressTensor newtonianStress(const ViscousGradients& g, Real mu) {
  const Real div = g.du[0] + g.dv[1];
  StressTensor t;
  t.xx = mu * (2.0 * g.du[0] - (2.0 / 3.0) * div);
  t.yy = mu * (2.0 * g.dv[1] - (2.0 / 3.0) * div);
  t.xy = mu * (g.du[1] + g.dv[0]);
  return t;
}

// Viscous flux vector projected on the unit normal `n`.  `uf`,`vf` are the
// face velocity components used for the work term; `adiabatic` suppresses the
// heat-flux contribution (zero normal temperature gradient at the wall).
inline ConsVec viscousNormalFlux(const ViscousGradients& g, Real mu, Real k, Real uf, Real vf,
                                 const Vec2& n, bool adiabatic = false) {
  const StressTensor t = newtonianStress(g, mu);
  const Real tx = t.xx * n[0] + t.xy * n[1];
  const Real ty = t.xy * n[0] + t.yy * n[1];
  Real qn = 0.0;
  if (!adiabatic) qn = -k * (g.dT[0] * n[0] + g.dT[1] * n[1]);
  ConsVec f{};
  f[0] = 0.0;
  f[1] = tx;
  f[2] = ty;
  f[3] = tx * uf + ty * vf - qn;
  return f;
}

}  // namespace cfd
