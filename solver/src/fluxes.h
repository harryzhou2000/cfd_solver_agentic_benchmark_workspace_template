#pragma once

#include "types.h"
#include "case_io.h"
#include <array>

namespace cfd2d {

// Gas physics helpers
struct GasPhysics {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double mu = 0.0; // dynamic viscosity (nondimensional)
  bool viscous = false;
  double rusanovScale = 1.0;

  // Freestream
  double rhoInf = 1.0, uInf = 0.0, vInf = 0.0, pInf = 1.0, machInf = 0.1;
  double velMag = 1.0;

  void initFromCase(const CaseInput& ci);
  // Conservative <-> primitive
  PrimState consToPrim(const ConsState& U) const;
  ConsState primToCons(const PrimState& P) const;
  double soundSpeed(const PrimState& P) const;
  double temperature(const PrimState& P) const;
  // Inviscid flux in x and y
  void inviscidFlux(const ConsState& U, ConsState& F, ConsState& G) const;
  // Rusanov flux given left/right states and normal
  ConsState rusanovFlux(const ConsState& UL, const ConsState& UR,
                        double nx, double ny) const;
  // Viscous flux given primitive states and gradients
  // dUdx, dUdy are gradients of primitive [rho,u,v,p] (or T)
  void viscousFlux(const PrimState& PL, const PrimState& PR,
                   const std::array<double,4>& dWdx,
                   const std::array<double,4>& dWdy,
                   double nx, double ny,
                   ConsState& Fvisc) const;
};

} // namespace cfd2d
