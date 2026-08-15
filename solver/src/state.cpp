#include "cfd/state.hpp"

#include <algorithm>

namespace cfd {

Primitive freestream_primitive(const CaseConfig& cfg) {
  constexpr Real pi = 3.141592653589793238462643383279502884;
  const Real alpha = cfg.freestream.aoa_degrees * pi / 180.0;
  Primitive q;
  q.rho = cfg.freestream.rho;
  q.u = cfg.freestream.velocity_magnitude * std::cos(alpha);
  q.v = cfg.freestream.velocity_magnitude * std::sin(alpha);
  q.p = cfg.freestream.pressure;
  return q;
}

Conserved primitive_to_conserved(const Primitive& q_in, const GasModel& gas) {
  Primitive q = q_in;
  q.rho = std::max(q.rho, 1.0e-10);
  q.p = std::max(q.p, 1.0e-10);
  const Real kinetic = 0.5 * (q.u * q.u + q.v * q.v);
  const Real e = q.p / ((gas.gamma - 1.0) * q.rho);
  return {q.rho, q.rho * q.u, q.rho * q.v, q.rho * (e + kinetic)};
}

Primitive conserved_to_primitive(const Conserved& u, const GasModel& gas) {
  Primitive q;
  q.rho = std::max(u[0], 1.0e-10);
  q.u = u[1] / q.rho;
  q.v = u[2] / q.rho;
  const Real E = u[3] / q.rho;
  const Real kinetic = 0.5 * (q.u * q.u + q.v * q.v);
  q.p = std::max((gas.gamma - 1.0) * q.rho * (E - kinetic), 1.0e-10);
  return q;
}

Thermo thermo_from_primitive(const Primitive& q, const GasModel& gas) {
  Thermo t;
  t.T = q.p / (q.rho * gas.R);
  t.E = q.p / ((gas.gamma - 1.0) * q.rho) + 0.5 * (q.u * q.u + q.v * q.v);
  t.a = std::sqrt(std::max(gas.gamma * q.p / q.rho, 1.0e-12));
  t.mach = std::sqrt(q.u * q.u + q.v * q.v) / t.a;
  return t;
}

Real viscosity(const CaseConfig& cfg) {
  if (cfg.mode == PhysicsMode::Inviscid) return 0.0;
  return cfg.freestream.rho * cfg.freestream.velocity_magnitude * cfg.reference.reynolds_length /
         cfg.reynolds;
}

Real dynamic_pressure(const CaseConfig& cfg) {
  return 0.5 * cfg.freestream.rho * cfg.freestream.velocity_magnitude *
         cfg.freestream.velocity_magnitude;
}

Conserved operator+(const Conserved& a, const Conserved& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}

Conserved operator-(const Conserved& a, const Conserved& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}

Conserved operator*(Real s, const Conserved& a) {
  return {s * a[0], s * a[1], s * a[2], s * a[3]};
}

Conserved& operator+=(Conserved& a, const Conserved& b) {
  for (int i = 0; i < 4; ++i) a[i] += b[i];
  return a;
}

Conserved& operator-=(Conserved& a, const Conserved& b) {
  for (int i = 0; i < 4; ++i) a[i] -= b[i];
  return a;
}

}  // namespace cfd
