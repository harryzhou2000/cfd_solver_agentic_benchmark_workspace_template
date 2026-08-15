#pragma once

// Force integration over wall boundary faces. Returns non-dimensional
// force/moment COEFFICIENTS (Cd, Cl, Cm) split into pressure and viscous
// parts, plus the total moment coefficient.

#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"

namespace cfd {

struct ForceResult {
  double pressure_drag = 0.0;  // Cd from pressure (x-direction force)
  double viscous_drag = 0.0;   // Cd from wall shear
  double pressure_lift = 0.0;  // Cl from pressure (y-direction force)
  double viscous_lift = 0.0;   // Cl from wall shear
  double moment_z = 0.0;       // Cm about the reference moment center
};

// Computes force coefficients on all wall boundary faces (SlipWall and
// NoSlipAdiabaticWall) owned by this rank.
// U: flat conservative state (local indexing), dmesh: rank-local mesh,
// cfg: case configuration (freestream dynamic pressure, reference area and
// length, moment center), mu: dynamic viscosity (0 in inviscid mode).
//
// Pressure force: p_wall * normal * area (p_wall = interior cell pressure).
// Viscous force (no-slip walls only): reaction of the wall shear
// tau = mu * V_t / d, i.e. +mu*V_t/d * tangent * area, where V_t is the
// signed tangential cell velocity and d the cell-center-to-face distance.
// Moment: r x F about the moment center (positive = counterclockwise).
//
// Coefficients: divide by q_inf * ref_area (moment additionally by
// ref_length), q_inf = 0.5 * rho_inf * V_inf^2.
//
// NOTE: returns PER-RANK coefficients; sum across ranks for the global
// values (each boundary face is owned by exactly one rank).
ForceResult compute_forces(const std::vector<double>& U,
                           const DistributedMesh& dmesh,
                           const CaseConfig& cfg, double mu);

}  // namespace cfd
