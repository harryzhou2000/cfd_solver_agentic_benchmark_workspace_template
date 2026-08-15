#pragma once

// Calorically perfect gas model for the 2D compressible Euler/Navier-Stokes
// solver. The conservative state per cell is stored in a flat double array:
//   U[cell*4 + 0] = rho   (density)
//   U[cell*4 + 1] = rho*u (x-momentum)
//   U[cell*4 + 2] = rho*v (y-momentum)
//   U[cell*4 + 3] = rho*E (total energy)

#include <cmath>
#include <stdexcept>
#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"

namespace cfd {

inline constexpr int NVARS = 4;  // rho, rhou, rhov, rhoE

struct PrimitiveState {
  double rho = 0.0;
  double u = 0.0;
  double v = 0.0;
  double p = 0.0;
  double T = 0.0;
  double a = 0.0;  // speed of sound
};

// Conservative -> primitive. Throws std::runtime_error for non-physical
// states (non-positive density or pressure).
PrimitiveState cons_to_prim(const double* U, double gamma, double R);

// Primitive -> conservative (U[3] = p/(gamma-1) + 0.5*rho*(u^2+v^2)).
void prim_to_cons(const PrimitiveState& prim, double gamma, double* U);

// Freestream primitive state derived from the case configuration
// (velocity_magnitude and aoa_degrees define the direction).
PrimitiveState freestream_primitive(const CaseConfig& cfg);

// Fills the full local state array (owned + ghost cells) with the case
// freestream state.
std::vector<double> init_freestream(const DistributedMesh& dmesh,
                                    const CaseConfig& cfg);

// Convenience accessors.
inline double speed_of_sound(double p, double rho, double gamma) {
  return std::sqrt(gamma * p / rho);
}

inline double temperature(double p, double rho, double R) {
  return p / (rho * R);
}

}  // namespace cfd
