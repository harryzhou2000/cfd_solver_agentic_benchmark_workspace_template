#pragma once
// Inviscid (Rusanov/local Lax-Friedrichs) and viscous numerical fluxes, plus
// boundary-condition ghost-state construction.
//
// All fluxes are original implementations of published algorithms (no solver
// code is copied or adapted from any existing CFD codebase).

#include "common.hpp"
#include "config.hpp"
#include "physics.hpp"

namespace cfd2d {

enum class BcType { Farfield, SlipWall, NoSlipAdiabaticWall };

BcType bcTypeFromString(const std::string& s);
std::string bcTypeToString(BcType t);

struct FluxContext {
  Gas gas;
  double rusanovScale = 1.0;  // multiplier on the LLF jump term
  double mu = 0.0;            // laminar viscosity (0 for inviscid)
  double kCond = 0.0;         // thermal conductivity
  bool viscous = false;
};

// Physical inviscid flux dotted with a unit normal (F nx + G ny).
Vec4 inviscidPhysicalFlux(const Gas& g, const Vec4& W, double nx, double ny);

// Rusanov / local Lax-Friedrichs flux through a face with unit normal n and
// length len. WL/WR are primitive face states; UL/UR conservative ones.
Vec4 rusanovFlux(const FluxContext& ctx, const Vec4& WL, const Vec4& WR,
                 const Vec4& UL, const Vec4& UR, double nx, double ny,
                 double len, double* lambdaOut = nullptr);

// Viscous flux through a face. gradU/gradV/gradT are face-centered gradients
// of velocity and temperature (2 components each), uf/vf/ufT are face velocity
// used in the work terms. Returns flux * len.
Vec4 viscousFlux(const FluxContext& ctx, double dudx, double dudy, double dvdx,
                 double dvdy, double dTdx, double dTdy, double uf, double vf,
                 double nx, double ny, double len);

// Convective spectral radius (|u.n| + a) * len for a primitive state.
double convectiveSpectralRadius(const Gas& g, const Vec4& W, double nx,
                                double ny, double len);

// Full 4x4 Jacobian d(F.n)/dU of the inviscid normal flux at primitive state
// W, stored row-major (A[4*row + col]).
void eulerFluxJacobian(const Gas& g, const Vec4& W, double nx, double ny,
                       double A[16]);

// Ghost primitive state for a boundary face, given the interior primitive
// state at the face and the outward unit normal.
Vec4 boundaryGhostState(BcType bc, const FluxContext& ctx,
                        const FreeStream& fs, const Vec4& Wi, double nx,
                        double ny);

}  // namespace cfd2d
