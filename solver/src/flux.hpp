#pragma once

#include "common.hpp"

namespace fv {

enum class InviscidFluxType { HLLC, RUSANOV };

// Numerical inviscid flux through a face with unit normal (nx,ny), multiplied
// by nothing (caller scales by area). smax returns the wave-speed estimate
// used (for spectral-radius bookkeeping).
Vec4 hllcFlux(const Gas& g, const Vec4& UL, const Vec4& UR, double nx, double ny,
              double& smax);
Vec4 rusanovFlux(const Gas& g, const Vec4& UL, const Vec4& UR, double nx, double ny,
                 double dissipationScale, double& smax);

// Convective spectral radius (|u_n| + a) for a state.
double convectiveSpectralRadius(const Gas& g, const Vec4& U, double nx, double ny);

// Viscous flux (to be SUBTRACTED from the inviscid flux):
//   Fv . n = [0, tau.n, u.tau.n + k gradT.n]
// Computed from face-averaged velocities and face gradients.
//   (uF,vF): face velocity; (duFdx..): face velocity gradients;
//   (dTFdx,dTFdy): face temperature gradient; mu, k: transport coefficients.
Vec4 viscousFlux(double uF, double vF, double duFdx, double duFdy, double dvFdx,
                 double dvFdy, double dTFdx, double dTFdy, double mu, double kCond,
                 double nx, double ny);

}  // namespace fv
