#pragma once

#include "cfd/CaseConfig.hpp"
#include "cfd/Mesh.hpp"

#include <array>
#include <stdexcept>

namespace cfd {

/// Conservative variables ordered as rho, rho*u, rho*v, rho*E.
using Conserved = std::array<double, 4>;

/// Thermodynamic state. temperature, sound_speed, and mach are derived from
/// rho, u, v, and p. T/a are zero-cost compatibility aliases.
struct Primitive {
  double rho = 0.0;
  double u = 0.0;
  double v = 0.0;
  double p = 0.0;
  union {
    double temperature = 0.0;
    double T;
  };
  union {
    double sound_speed = 0.0;
    double a;
  };
  double mach = 0.0;
};

/// Calorically perfect-gas material parameters.
struct GasProperties {
  double gamma = 1.4;
  double gas_constant = 1.0;
  double prandtl = 0.72;
};

/// Gradients needed by the Newtonian/Fourier viscous flux.
struct PrimitiveGradients {
  Vec2 u;
  Vec2 v;
  Vec2 T;
};

class PhysicsError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Converts gas fields from a validated case configuration.
GasProperties gas_properties(const CaseConfig& config);

/// Checks primary primitive variables and fills its derived thermodynamic fields.
Primitive complete_primitive(Primitive state, const GasProperties& gas);

/// Converts between primitive and conservative variables, rejecting nonphysical states.
Conserved conserved_from_primitive(const Primitive& state, const GasProperties& gas);
Primitive primitive_from_conserved(const Conserved& state, const GasProperties& gas);

/// Lightweight compatibility overloads for callers that have only gamma and R.
Conserved primitive_to_conservative(const Primitive& state, double gamma, double gas_constant);
Primitive conservative_to_primitive(const Conserved& state, double gamma, double gas_constant);

/// Builds the thermodynamically consistent freestream state from CaseConfig.
Primitive freestream_primitive(const CaseConfig& config);
Conserved freestream_conserved(const CaseConfig& config);

/// Euler flux dotted with normal. normal may be unit length or area-weighted.
Conserved euler_normal_flux(const Primitive& state, Vec2 normal, const GasProperties& gas);

/// Largest Euler characteristic speed in the direction of normal.
double normal_wave_speed(const Primitive& state, Vec2 normal, const GasProperties& gas);

/// Local Lax--Friedrichs/Rusanov face flux. dissipation_scale must be positive.
Conserved rusanov_flux(const Primitive& left, const Primitive& right, Vec2 normal,
                       const GasProperties& gas, double dissipation_scale = 1.0);

/// Newtonian stress and Fourier heat flux dotted with normal.
/// The returned energy component is (u*tau_xj + v*tau_yj - q_j) n_j.
Conserved viscous_normal_flux(const Primitive& state, const PrimitiveGradients& gradients,
                              Vec2 normal, const GasProperties& gas, double mu);

/// Returns true if U has finite rho and pressure no smaller than the supplied floors.
bool is_physical(const Conserved& state, const GasProperties& gas,
                 double density_floor = 1.0e-12, double pressure_floor = 1.0e-12);

/// Applies increment as far as possible while retaining the requested positivity floors.
/// A nonphysical starting state is rejected; the returned state is always physical.
Conserved positivity_safe_update(const Conserved& state, const Conserved& increment,
                                 const GasProperties& gas, double density_floor = 1.0e-12,
                                 double pressure_floor = 1.0e-12);

/// Alias that reads naturally at residual-assembly call sites.
inline Conserved apply_positivity_limited_update(
    const Conserved& state, const Conserved& increment, const GasProperties& gas,
    double density_floor = 1.0e-12, double pressure_floor = 1.0e-12) {
  return positivity_safe_update(state, increment, gas, density_floor, pressure_floor);
}

}  // namespace cfd
