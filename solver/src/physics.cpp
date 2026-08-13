#include "physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aerofv {
namespace {

constexpr double kTiny = 1.0e-14;

Vec2 normalized_or_zero(Vec2 normal) {
  const double length = norm(normal);
  if (!std::isfinite(length) || length <= kTiny) {
    return {};
  }
  return normal / length;
}

bool finite_vec(const Vec2 &value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

double safe_gamma_minus_one(const GasModel &gas) {
  return valid_gas_model(gas) ? gas.gamma - 1.0 : 0.4;
}

Primitive safe_state(Primitive state, const GasModel &gas) {
  const double rho_floor =
      (std::isfinite(gas.density_floor) && gas.density_floor > 0.0)
          ? gas.density_floor
          : 1.0e-10;
  const double p_floor =
      (std::isfinite(gas.pressure_floor) && gas.pressure_floor > 0.0)
          ? gas.pressure_floor
          : 1.0e-10;
  state.rho = std::isfinite(state.rho) ? std::max(state.rho, rho_floor)
                                        : rho_floor;
  state.p = std::isfinite(state.p) ? std::max(state.p, p_floor) : p_floor;
  state.u = std::isfinite(state.u) ? state.u : 0.0;
  state.v = std::isfinite(state.v) ? state.v : 0.0;
  return state;
}

Conservative zero_flux() { return {0.0, 0.0, 0.0, 0.0}; }

} // namespace

bool valid_gas_model(const GasModel &gas) {
  return std::isfinite(gas.gamma) && gas.gamma > 1.0 &&
         std::isfinite(gas.gas_constant) && gas.gas_constant > 0.0 &&
         std::isfinite(gas.prandtl) && gas.prandtl > 0.0;
}

bool finite(const Primitive &state) {
  return std::isfinite(state.rho) && std::isfinite(state.u) &&
         std::isfinite(state.v) && std::isfinite(state.p);
}

bool physically_valid(const Primitive &state, const GasModel &gas) {
  return valid_gas_model(gas) && finite(state) &&
         state.rho >= gas.density_floor && state.p >= gas.pressure_floor;
}

bool physically_valid(const Conservative &state, const GasModel &gas) {
  if (!valid_gas_model(gas) || !finite(state) ||
      state[0] < gas.density_floor) {
    return false;
  }
  const double rho = state[0];
  const double kinetic = 0.5 * (state[1] * state[1] + state[2] * state[2]) / rho;
  const double pressure = safe_gamma_minus_one(gas) * (state[3] - kinetic);
  return std::isfinite(pressure) && pressure >= gas.pressure_floor;
}

Primitive sanitize_primitive(Primitive state, const GasModel &gas) {
  return safe_state(state, gas);
}

Conservative primitive_to_conservative(const Primitive &state,
                                       const GasModel &gas) {
  const Primitive q = safe_state(state, gas);
  const double gm1 = safe_gamma_minus_one(gas);
  const double energy = q.p / gm1 + 0.5 * q.rho * (q.u * q.u + q.v * q.v);
  return {q.rho, q.rho * q.u, q.rho * q.v, energy};
}

Primitive conservative_to_primitive(const Conservative &state,
                                     const GasModel &gas) {
  if (!valid_gas_model(gas) || !finite(state)) {
    return safe_state({}, gas);
  }
  const double rho_floor = std::max(gas.density_floor, kTiny);
  const double rho = std::max(state[0], rho_floor);
  const double u = std::isfinite(state[1] / rho) ? state[1] / rho : 0.0;
  const double v = std::isfinite(state[2] / rho) ? state[2] / rho : 0.0;
  const double kinetic = 0.5 * rho * (u * u + v * v);
  const double p = safe_gamma_minus_one(gas) * (state[3] - kinetic);
  return safe_state({rho, u, v, p}, gas);
}

Conservative sanitize_conservative(const Conservative &state,
                                   const GasModel &gas) {
  return primitive_to_conservative(conservative_to_primitive(state, gas), gas);
}

double temperature(const Primitive &state, const GasModel &gas) {
  const Primitive q = safe_state(state, gas);
  const double r = valid_gas_model(gas) ? gas.gas_constant : 1.0;
  return q.p / (q.rho * r);
}

double sound_speed(const Primitive &state, const GasModel &gas) {
  const Primitive q = safe_state(state, gas);
  const double gamma = valid_gas_model(gas) ? gas.gamma : 1.4;
  return std::sqrt(std::max(0.0, gamma * q.p / q.rho));
}

double normal_velocity(const Primitive &state, const Vec2 &unit_normal) {
  const Vec2 normal = normalized_or_zero(unit_normal);
  return state.u * normal.x + state.v * normal.y;
}

Conservative euler_normal_flux(const Conservative &state,
                               const Primitive &primitive,
                               const Vec2 &unit_normal) {
  const Vec2 normal = normalized_or_zero(unit_normal);
  if (norm(normal) == 0.0) {
    return zero_flux();
  }
  const Primitive q = primitive;
  const double un = q.u * normal.x + q.v * normal.y;
  return {state[0] * un, state[1] * un + q.p * normal.x,
          state[2] * un + q.p * normal.y, (state[3] + q.p) * un};
}

Conservative rusanov_flux(const Conservative &left, const Conservative &right,
                          const Vec2 &unit_normal, const GasModel &gas) {
  const Vec2 normal = normalized_or_zero(unit_normal);
  if (norm(normal) == 0.0) {
    return zero_flux();
  }
  const Conservative ul = sanitize_conservative(left, gas);
  const Conservative ur = sanitize_conservative(right, gas);
  const Primitive ql = conservative_to_primitive(ul, gas);
  const Primitive qr = conservative_to_primitive(ur, gas);
  const Conservative fl = euler_normal_flux(ul, ql, normal);
  const Conservative fr = euler_normal_flux(ur, qr, normal);
  const double lambda = std::max(std::abs(normal_velocity(ql, normal)) +
                                     sound_speed(ql, gas),
                                 std::abs(normal_velocity(qr, normal)) +
                                     sound_speed(qr, gas));
  Conservative flux = 0.5 * (fl + fr) - 0.5 * lambda * (ur - ul);
  return finite(flux) ? flux : zero_flux();
}

Conservative hllc_flux(const Conservative &left, const Conservative &right,
                       const Vec2 &unit_normal, const GasModel &gas) {
  const Vec2 normal = normalized_or_zero(unit_normal);
  if (norm(normal) == 0.0) {
    return zero_flux();
  }
  const Conservative ul = sanitize_conservative(left, gas);
  const Conservative ur = sanitize_conservative(right, gas);
  const Primitive ql = conservative_to_primitive(ul, gas);
  const Primitive qr = conservative_to_primitive(ur, gas);
  const double un_l = normal_velocity(ql, normal);
  const double un_r = normal_velocity(qr, normal);
  const double a_l = sound_speed(ql, gas);
  const double a_r = sound_speed(qr, gas);
  const double s_l = std::min(un_l - a_l, un_r - a_r);
  const double s_r = std::max(un_l + a_l, un_r + a_r);
  const double denominator = ql.rho * (s_l - un_l) -
                             qr.rho * (s_r - un_r);
  if (!std::isfinite(denominator) || std::abs(denominator) < kTiny ||
      !(s_l < s_r)) {
    return rusanov_flux(ul, ur, normal, gas);
  }
  const double s_m = (qr.p - ql.p + ql.rho * un_l * (s_l - un_l) -
                      qr.rho * un_r * (s_r - un_r)) /
                     denominator;
  const double p_star_l = ql.p + ql.rho * (s_l - un_l) * (s_m - un_l);
  const double p_star_r = qr.p + qr.rho * (s_r - un_r) * (s_m - un_r);
  const double p_star = 0.5 * (p_star_l + p_star_r);
  if (!std::isfinite(s_m) || !std::isfinite(p_star) ||
      p_star < gas.pressure_floor || !(s_l <= s_m && s_m <= s_r)) {
    return rusanov_flux(ul, ur, normal, gas);
  }

  const Vec2 tangent{-normal.y, normal.x};
  const auto star_state = [&](const Conservative &u, const Primitive &q,
                              double un, double s) {
    const double denom = s - s_m;
    if (std::abs(denom) < kTiny) {
      return Conservative{};
    }
    const double rho_star = q.rho * (s - un) / denom;
    const double ut = q.u * tangent.x + q.v * tangent.y;
    const double e_star = ((s - un) * u[3] - q.p * un + p_star * s_m) / denom;
    const Vec2 velocity_star = s_m * normal + ut * tangent;
    return Conservative{rho_star, rho_star * velocity_star.x,
                        rho_star * velocity_star.y, e_star};
  };

  const Conservative fl = euler_normal_flux(ul, ql, normal);
  const Conservative fr = euler_normal_flux(ur, qr, normal);
  Conservative flux{};
  if (0.0 <= s_l) {
    flux = fl;
  } else if (s_l <= 0.0 && 0.0 <= s_m) {
    const Conservative us = star_state(ul, ql, un_l, s_l);
    flux = fl + s_l * (us - ul);
  } else if (s_m <= 0.0 && 0.0 <= s_r) {
    const Conservative us = star_state(ur, qr, un_r, s_r);
    flux = fr + s_r * (us - ur);
  } else {
    flux = fr;
  }
  return finite(flux) ? flux : rusanov_flux(ul, ur, normal, gas);
}

Conservative impermeable_wall_flux(const Primitive &interior,
                                   const Vec2 &unit_normal,
                                   const GasModel &gas) {
  const Primitive q = safe_state(interior, gas);
  const Vec2 normal = normalized_or_zero(unit_normal);
  if (norm(normal) == 0.0) {
    return zero_flux();
  }
  return {0.0, q.p * normal.x, q.p * normal.y, 0.0};
}

Primitive boundary_ghost_primitive(const Primitive &interior,
                                   const Primitive &freestream,
                                   const Vec2 &outward_unit_normal,
                                   BoundaryType type, const GasModel &gas) {
  const Primitive q = safe_state(interior, gas);
  const Vec2 normal = normalized_or_zero(outward_unit_normal);
  if (type == BoundaryType::farfield) {
    const Primitive infinity = safe_state(freestream, gas);
    if (norm(normal) == 0.0) {
      return infinity;
    }
    const double un = normal_velocity(q, normal);
    const double un_infinity = normal_velocity(infinity, normal);
    const double a = sound_speed(q, gas);
    const double a_infinity = sound_speed(infinity, gas);
    if (un >= a) {
      return q; // supersonic outflow: all characteristics leave the domain
    }
    if (un <= -a) {
      return infinity; // supersonic inflow: all data are prescribed
    }
    const double gm1 = gas.gamma - 1.0;
    const double outgoing = un + 2.0 * a / gm1;
    const double incoming = un_infinity - 2.0 * a_infinity / gm1;
    const double boundary_un = 0.5 * (outgoing + incoming);
    const double boundary_a =
        std::max(0.25 * gm1 * (outgoing - incoming), 1.0e-12);
    const Primitive &entropy_source = boundary_un >= 0.0 ? q : infinity;
    const double entropy = entropy_source.p /
                           std::pow(entropy_source.rho, gas.gamma);
    const double rho = std::pow(boundary_a * boundary_a /
                                    (gas.gamma * entropy),
                                1.0 / gm1);
    const double pressure = entropy * std::pow(rho, gas.gamma);
    const Vec2 tangent{-normal.y, normal.x};
    const double tangential =
        (boundary_un >= 0.0 ? q.u : infinity.u) * tangent.x +
        (boundary_un >= 0.0 ? q.v : infinity.v) * tangent.y;
    const Vec2 velocity = boundary_un * normal + tangential * tangent;
    return safe_state({rho, velocity.x, velocity.y, pressure}, gas);
  }
  if (type == BoundaryType::unknown) {
    return safe_state(freestream, gas);
  }
  if (norm(normal) == 0.0) {
    return q;
  }
  const double un = q.u * normal.x + q.v * normal.y;
  if (type == BoundaryType::slip_wall) {
    return {q.rho, q.u - 2.0 * un * normal.x, q.v - 2.0 * un * normal.y,
            q.p};
  }
  if (type == BoundaryType::no_slip_adiabatic_wall) {
    return {q.rho, -q.u, -q.v, q.p};
  }
  return q;
}

Primitive boundary_surface_primitive(const Primitive &interior,
                                     const Primitive &freestream,
                                     const Vec2 &outward_unit_normal,
                                     BoundaryType type, const GasModel &gas) {
  const Primitive q = safe_state(interior, gas);
  const Vec2 normal = normalized_or_zero(outward_unit_normal);
  if (type == BoundaryType::farfield || type == BoundaryType::unknown) {
    return safe_state(freestream, gas);
  }
  if (type == BoundaryType::no_slip_adiabatic_wall) {
    return {q.rho, 0.0, 0.0, q.p};
  }
  if (type == BoundaryType::slip_wall && norm(normal) > 0.0) {
    const double un = q.u * normal.x + q.v * normal.y;
    return {q.rho, q.u - un * normal.x, q.v - un * normal.y, q.p};
  }
  return q;
}

Conservative boundary_ghost_state(const Conservative &interior,
                                  const Primitive &freestream,
                                  const Vec2 &outward_unit_normal,
                                  BoundaryType type, const GasModel &gas) {
  return primitive_to_conservative(
      boundary_ghost_primitive(conservative_to_primitive(interior, gas),
                               freestream, outward_unit_normal, type, gas),
      gas);
}

Vec2 temperature_gradient(const Primitive &state,
                          const PrimitiveGradient &gradient,
                          const GasModel &gas) {
  const Primitive q = safe_state(state, gas);
  if (!finite_vec(gradient[0]) || !finite_vec(gradient[3])) {
    return {};
  }
  const double r = valid_gas_model(gas) ? gas.gas_constant : 1.0;
  const double rho2 = q.rho * q.rho;
  return (gradient[3] / q.rho - (q.p / rho2) * gradient[0]) / r;
}

Conservative viscous_normal_flux(const Primitive &state,
                                 const PrimitiveGradient &gradient,
                                 const Vec2 &unit_normal,
                                 double dynamic_viscosity,
                                 const GasModel &gas) {
  const Vec2 normal = normalized_or_zero(unit_normal);
  if (norm(normal) == 0.0 || !std::isfinite(dynamic_viscosity) ||
      dynamic_viscosity <= 0.0 || !valid_gas_model(gas)) {
    return zero_flux();
  }
  const Primitive q = safe_state(state, gas);
  if (!finite_vec(gradient[1]) || !finite_vec(gradient[2])) {
    return zero_flux();
  }
  const double ux = gradient[1].x;
  const double uy = gradient[1].y;
  const double vx = gradient[2].x;
  const double vy = gradient[2].y;
  const double div = ux + vy;
  const double tau_xx = dynamic_viscosity * (2.0 * ux - (2.0 / 3.0) * div);
  const double tau_yy = dynamic_viscosity * (2.0 * vy - (2.0 / 3.0) * div);
  const double tau_xy = dynamic_viscosity * (uy + vx);
  const Vec2 traction{tau_xx * normal.x + tau_xy * normal.y,
                      tau_xy * normal.x + tau_yy * normal.y};
  const double cp = gas.gamma * gas.gas_constant / (gas.gamma - 1.0);
  const double conductivity = dynamic_viscosity * cp / gas.prandtl;
  const Vec2 grad_t = temperature_gradient(q, gradient, gas);
  const double energy = q.u * traction.x + q.v * traction.y +
                        conductivity * dot(grad_t, normal);
  const Conservative flux{0.0, traction.x, traction.y, energy};
  return finite(flux) ? flux : zero_flux();
}

double convective_face_spectral_radius(const Primitive &state,
                                       const Vec2 &unit_normal,
                                       double face_length,
                                       const GasModel &gas) {
  if (!std::isfinite(face_length) || face_length <= 0.0) {
    return 0.0;
  }
  return face_length *
         (std::abs(normal_velocity(safe_state(state, gas), unit_normal)) +
          sound_speed(state, gas));
}

double viscous_face_spectral_radius(const Primitive &state,
                                   double face_length,
                                   double center_distance,
                                   double dynamic_viscosity,
                                   const GasModel &gas) {
  if (!valid_gas_model(gas) || !std::isfinite(face_length) ||
      !std::isfinite(center_distance) || !std::isfinite(dynamic_viscosity) ||
      face_length <= 0.0 || center_distance <= kTiny ||
      dynamic_viscosity <= 0.0) {
    return 0.0;
  }
  const Primitive q = safe_state(state, gas);
  const double coefficient = 4.0 / 3.0 + gas.gamma / gas.prandtl;
  return coefficient * (dynamic_viscosity / q.rho) * face_length /
         center_distance;
}

double local_pseudo_timestep(double cell_volume, double convective_sum,
                             double viscous_sum, double cfl,
                             double physical_timestep) {
  if (!std::isfinite(cell_volume) || !std::isfinite(convective_sum) ||
      !std::isfinite(viscous_sum) || !std::isfinite(cfl) || cell_volume <= 0.0 ||
      cfl <= 0.0) {
    return 0.0;
  }
  const double spectral_sum = std::max(0.0, convective_sum) +
                              std::max(0.0, viscous_sum);
  double dt = spectral_sum > kTiny ? cfl * cell_volume / spectral_sum
                                   : std::numeric_limits<double>::infinity();
  if (std::isfinite(physical_timestep) && physical_timestep > 0.0) {
    dt = std::min(dt, physical_timestep);
  }
  return dt;
}

} // namespace aerofv
