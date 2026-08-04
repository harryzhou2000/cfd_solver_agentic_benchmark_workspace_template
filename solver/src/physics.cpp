#include "cfd/physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cfd {
namespace {

bool finite_state(const Conserved& value) {
  return std::all_of(value.begin(), value.end(), [](double x) { return std::isfinite(x); });
}

}  // namespace

PerfectGasPhysics::PerfectGasPhysics(const CaseConfig& config)
    : freestream_(config.freestream_primitive()),
      gamma_(config.gas.gamma),
      gas_constant_(config.gas.gas_constant),
      viscosity_(config.viscosity()),
      rusanov_scale_(config.run.rusanov_dissipation_scale) {
  const double cp = gamma_ * gas_constant_ / (gamma_ - 1.0);
  conductivity_ = viscosity_ * cp / config.gas.prandtl;
  density_floor_ = std::max(1.0e-12, 1.0e-10 * freestream_.rho);
  pressure_floor_ = std::max(1.0e-12, 1.0e-10 * freestream_.p);
}

Conserved PerfectGasPhysics::to_conserved(const Primitive& primitive) const {
  if (!std::isfinite(primitive.rho) || !std::isfinite(primitive.u) || !std::isfinite(primitive.v) ||
      !std::isfinite(primitive.p) || primitive.rho <= 0.0 || primitive.p <= 0.0) {
    throw std::runtime_error("cannot convert a nonphysical primitive state");
  }
  const double kinetic = 0.5 * (primitive.u * primitive.u + primitive.v * primitive.v);
  return {primitive.rho, primitive.rho * primitive.u, primitive.rho * primitive.v,
          primitive.p / (gamma_ - 1.0) + primitive.rho * kinetic};
}

Primitive PerfectGasPhysics::to_primitive(const Conserved& conserved) const {
  if (!finite_state(conserved) || conserved[0] <= density_floor_) throw std::runtime_error("nonpositive or nonfinite density");
  const double rho = conserved[0];
  const double u = conserved[1] / rho;
  const double v = conserved[2] / rho;
  const double kinetic = 0.5 * rho * (u * u + v * v);
  const double pressure = (gamma_ - 1.0) * (conserved[3] - kinetic);
  if (!std::isfinite(pressure) || pressure <= pressure_floor_) throw std::runtime_error("nonpositive or nonfinite pressure");
  return {rho, u, v, pressure};
}

bool PerfectGasPhysics::admissible(const Conserved& conserved) const {
  if (!finite_state(conserved) || conserved[0] <= density_floor_) return false;
  const double kinetic = 0.5 * (conserved[1] * conserved[1] + conserved[2] * conserved[2]) / conserved[0];
  const double pressure = (gamma_ - 1.0) * (conserved[3] - kinetic);
  return std::isfinite(pressure) && pressure > pressure_floor_;
}

double PerfectGasPhysics::sound_speed(const Primitive& primitive) const {
  return std::sqrt(gamma_ * primitive.p / primitive.rho);
}

double PerfectGasPhysics::temperature(const Primitive& primitive) const {
  return primitive.p / (primitive.rho * gas_constant_);
}

double PerfectGasPhysics::total_energy_per_mass(const Primitive& primitive) const {
  return primitive.p / ((gamma_ - 1.0) * primitive.rho) +
         0.5 * (primitive.u * primitive.u + primitive.v * primitive.v);
}

Conserved PerfectGasPhysics::inviscid_physical_flux(const Primitive& primitive, Vec2 normal) const {
  const double un = primitive.u * normal.x + primitive.v * normal.y;
  const double total_energy_density = primitive.p / (gamma_ - 1.0) +
      0.5 * primitive.rho * (primitive.u * primitive.u + primitive.v * primitive.v);
  return {primitive.rho * un,
          primitive.rho * primitive.u * un + primitive.p * normal.x,
          primitive.rho * primitive.v * un + primitive.p * normal.y,
          (total_energy_density + primitive.p) * un};
}

FluxResult PerfectGasPhysics::rusanov_flux(const Primitive& left, const Primitive& right, Vec2 normal) const {
  const Conserved ul = to_conserved(left);
  const Conserved ur = to_conserved(right);
  const Conserved fl = inviscid_physical_flux(left, normal);
  const Conserved fr = inviscid_physical_flux(right, normal);
  const double un_left = left.u * normal.x + left.v * normal.y;
  const double un_right = right.u * normal.x + right.v * normal.y;
  const double raw_radius = std::max(std::abs(un_left) + sound_speed(left), std::abs(un_right) + sound_speed(right));
  const double dissipation = rusanov_scale_ * raw_radius;
  FluxResult result;
  result.spectral_radius = raw_radius;
  result.used_rusanov_fallback = true;
  for (int k = 0; k < nvars; ++k) {
    const auto i = static_cast<std::size_t>(k);
    result.flux[i] = 0.5 * (fl[i] + fr[i]) - 0.5 * dissipation * (ur[i] - ul[i]);
  }
  return result;
}

FluxResult PerfectGasPhysics::hllc_flux(const Primitive& left, const Primitive& right, Vec2 normal) const {
  const Conserved ul = to_conserved(left);
  const Conserved ur = to_conserved(right);
  const Conserved fl = inviscid_physical_flux(left, normal);
  const Conserved fr = inviscid_physical_flux(right, normal);
  const double a_left = sound_speed(left);
  const double a_right = sound_speed(right);
  const double un_left = left.u * normal.x + left.v * normal.y;
  const double un_right = right.u * normal.x + right.v * normal.y;
  const double s_left = std::min(un_left - a_left, un_right - a_right);
  const double s_right = std::max(un_left + a_left, un_right + a_right);
  const double denominator = left.rho * (s_left - un_left) - right.rho * (s_right - un_right);
  if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-14) return rusanov_flux(left, right, normal);
  const double s_middle = (right.p - left.p + left.rho * un_left * (s_left - un_left) -
                           right.rho * un_right * (s_right - un_right)) / denominator;
  const double p_star_left = left.p + left.rho * (s_left - un_left) * (s_middle - un_left);
  const double p_star_right = right.p + right.rho * (s_right - un_right) * (s_middle - un_right);
  const double p_star = 0.5 * (p_star_left + p_star_right);
  if (!std::isfinite(s_middle) || !std::isfinite(p_star) || p_star <= pressure_floor_) {
    return rusanov_flux(left, right, normal);
  }

  auto star_state = [&](const Primitive& p, const Conserved& u, double wave, double un) {
    const double star_denominator = wave - s_middle;
    const double rho_star = p.rho * (wave - un) / star_denominator;
    const double tangential = -p.u * normal.y + p.v * normal.x;
    const double u_star = s_middle * normal.x - tangential * normal.y;
    const double v_star = s_middle * normal.y + tangential * normal.x;
    const double energy_specific = u[3] / p.rho +
        (s_middle - un) * (s_middle + p.p / (p.rho * (wave - un)));
    return Conserved{rho_star, rho_star * u_star, rho_star * v_star, rho_star * energy_specific};
  };

  FluxResult result;
  result.spectral_radius = std::max(std::abs(un_left) + a_left, std::abs(un_right) + a_right);
  result.used_rusanov_fallback = false;
  if (s_left >= 0.0) {
    result.flux = fl;
  } else if (s_middle >= 0.0) {
    const Conserved star = star_state(left, ul, s_left, un_left);
    if (!admissible(star)) return rusanov_flux(left, right, normal);
    result.flux = fl + s_left * (star - ul);
  } else if (s_right > 0.0) {
    const Conserved star = star_state(right, ur, s_right, un_right);
    if (!admissible(star)) return rusanov_flux(left, right, normal);
    result.flux = fr + s_right * (star - ur);
  } else {
    result.flux = fr;
  }
  if (!finite_state(result.flux)) return rusanov_flux(left, right, normal);
  return result;
}

Primitive PerfectGasPhysics::boundary_state(const Primitive& interior, BoundaryType boundary, Vec2 outward_normal) const {
  if (boundary == BoundaryType::farfield) {
    const double normal_interior = interior.u * outward_normal.x + interior.v * outward_normal.y;
    const double normal_freestream = freestream_.u * outward_normal.x + freestream_.v * outward_normal.y;
    const double sound_interior = sound_speed(interior);
    const double sound_freestream = sound_speed(freestream_);
    if (normal_interior >= sound_interior) return interior;
    if (normal_freestream <= -sound_freestream) return freestream_;

    const double outgoing_invariant = normal_interior + 2.0 * sound_interior / (gamma_ - 1.0);
    const double incoming_invariant = normal_freestream - 2.0 * sound_freestream / (gamma_ - 1.0);
    const double normal_velocity = 0.5 * (outgoing_invariant + incoming_invariant);
    const double sound = std::max(1.0e-6 * std::sqrt(gamma_ * freestream_.p / freestream_.rho),
                                  0.25 * (gamma_ - 1.0) * (outgoing_invariant - incoming_invariant));
    const Primitive& entropy_source = normal_velocity >= 0.0 ? interior : freestream_;
    const Primitive& tangent_source = normal_velocity >= 0.0 ? interior : freestream_;
    const double entropy_constant = entropy_source.p / std::pow(entropy_source.rho, gamma_);
    const double rho = std::pow(sound * sound / (gamma_ * entropy_constant), 1.0 / (gamma_ - 1.0));
    const double pressure = entropy_constant * std::pow(rho, gamma_);
    const Vec2 tangent{-outward_normal.y, outward_normal.x};
    const double tangential_velocity = tangent_source.u * tangent.x + tangent_source.v * tangent.y;
    return {rho,
            normal_velocity * outward_normal.x + tangential_velocity * tangent.x,
            normal_velocity * outward_normal.y + tangential_velocity * tangent.y,
            pressure};
  }
  Primitive exterior = interior;
  const double un = interior.u * outward_normal.x + interior.v * outward_normal.y;
  if (boundary == BoundaryType::slip_wall) {
    exterior.u = interior.u - 2.0 * un * outward_normal.x;
    exterior.v = interior.v - 2.0 * un * outward_normal.y;
    return exterior;
  }
  if (boundary == BoundaryType::no_slip_adiabatic_wall) {
    exterior.u = -interior.u;
    exterior.v = -interior.v;
    return exterior;
  }
  throw std::runtime_error("boundary_state called for an interior face");
}

Conserved PerfectGasPhysics::viscous_flux(const Primitive& face_state,
                                           const PrimitiveGradient& gradient,
                                           Vec2 normal) const {
  if (viscosity_ == 0.0) return zeros();
  const double ux = gradient.q[1].x;
  const double uy = gradient.q[1].y;
  const double vx = gradient.q[2].x;
  const double vy = gradient.q[2].y;
  const double divergence = ux + vy;
  const double tau_xx = 2.0 * viscosity_ * ux - (2.0 / 3.0) * viscosity_ * divergence;
  const double tau_yy = 2.0 * viscosity_ * vy - (2.0 / 3.0) * viscosity_ * divergence;
  const double tau_xy = viscosity_ * (uy + vx);

  const double rho = face_state.rho;
  const double p = face_state.p;
  const Vec2 grad_temperature = (gradient.q[3] / rho - gradient.q[0] * (p / (rho * rho))) / gas_constant_;
  const double traction_x = tau_xx * normal.x + tau_xy * normal.y;
  const double traction_y = tau_xy * normal.x + tau_yy * normal.y;
  const double heat_conduction = conductivity_ * dot(grad_temperature, normal);
  return {0.0, traction_x, traction_y,
          face_state.u * traction_x + face_state.v * traction_y + heat_conduction};
}

}  // namespace cfd
