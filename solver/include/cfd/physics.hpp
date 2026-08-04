#pragma once

#include "cfd/case_config.hpp"
#include "cfd/types.hpp"

#include <cstdint>

namespace cfd {

struct FluxResult {
  Conserved flux{};  // per unit face length, oriented with the supplied normal
  double spectral_radius = 0.0;
  bool used_rusanov_fallback = false;
};

class PerfectGasPhysics {
 public:
  explicit PerfectGasPhysics(const CaseConfig& config);

  [[nodiscard]] Conserved to_conserved(const Primitive& primitive) const;
  [[nodiscard]] Primitive to_primitive(const Conserved& conserved) const;
  [[nodiscard]] bool admissible(const Conserved& conserved) const;
  [[nodiscard]] double sound_speed(const Primitive& primitive) const;
  [[nodiscard]] double temperature(const Primitive& primitive) const;
  [[nodiscard]] double total_energy_per_mass(const Primitive& primitive) const;
  [[nodiscard]] Conserved inviscid_physical_flux(const Primitive& primitive, Vec2 normal) const;
  [[nodiscard]] FluxResult hllc_flux(const Primitive& left, const Primitive& right, Vec2 normal) const;
  [[nodiscard]] FluxResult rusanov_flux(const Primitive& left, const Primitive& right, Vec2 normal) const;
  [[nodiscard]] Primitive boundary_state(const Primitive& interior, BoundaryType boundary, Vec2 outward_normal) const;
  [[nodiscard]] Conserved viscous_flux(const Primitive& face_state, const PrimitiveGradient& face_gradient,
                                       Vec2 normal) const;

  [[nodiscard]] const Primitive& freestream() const { return freestream_; }
  [[nodiscard]] double gamma() const { return gamma_; }
  [[nodiscard]] double gas_constant() const { return gas_constant_; }
  [[nodiscard]] double viscosity() const { return viscosity_; }
  [[nodiscard]] double conductivity() const { return conductivity_; }
  [[nodiscard]] double density_floor() const { return density_floor_; }
  [[nodiscard]] double pressure_floor() const { return pressure_floor_; }

 private:
  Primitive freestream_{};
  double gamma_ = 1.4;
  double gas_constant_ = 1.0;
  double viscosity_ = 0.0;
  double conductivity_ = 0.0;
  double rusanov_scale_ = 1.0;
  double density_floor_ = 1.0e-12;
  double pressure_floor_ = 1.0e-12;
};

}  // namespace cfd
