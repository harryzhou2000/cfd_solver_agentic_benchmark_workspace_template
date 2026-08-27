// Calorically perfect gas closure.
//
// All thermodynamics used by the residual go through this small interface, so
// a different equation of state (Cantera-backed, real-gas, multi-species) can
// be introduced by adding a sibling class with the same member functions and
// templating / injecting it into the residual assembler.
#pragma once

#include "core/Types.hpp"

namespace cfd {

class PerfectGas {
 public:
  PerfectGas() = default;
  PerfectGas(Real gamma, Real R, Real prandtl) : gamma_(gamma), R_(R), pr_(prandtl) {}

  Real gamma() const { return gamma_; }
  Real R() const { return R_; }
  Real prandtl() const { return pr_; }
  Real cp() const { return gamma_ * R_ / (gamma_ - 1.0); }
  Real cv() const { return R_ / (gamma_ - 1.0); }

  Real pressure(const ConsVec& u) const {
    const Real ke = 0.5 * (u[1] * u[1] + u[2] * u[2]) / u[0];
    return (gamma_ - 1.0) * (u[3] - ke);
  }
  Real soundSpeed(Real rho, Real p) const { return std::sqrt(gamma_ * p / rho); }
  Real temperature(Real rho, Real p) const { return p / (rho * R_); }
  // Total enthalpy H = E + p/rho.
  Real totalEnthalpy(Real rho, Real p, Real q2) const {
    return gamma_ / (gamma_ - 1.0) * p / rho + 0.5 * q2;
  }

  PrimVec toPrimitive(const ConsVec& u) const {
    PrimVec w{};
    w[0] = u[0];
    w[1] = u[1] / u[0];
    w[2] = u[2] / u[0];
    w[3] = (gamma_ - 1.0) * (u[3] - 0.5 * (u[1] * u[1] + u[2] * u[2]) / u[0]);
    return w;
  }
  ConsVec toConservative(const PrimVec& w) const {
    ConsVec u{};
    u[0] = w[0];
    u[1] = w[0] * w[1];
    u[2] = w[0] * w[2];
    u[3] = w[3] / (gamma_ - 1.0) + 0.5 * w[0] * (w[1] * w[1] + w[2] * w[2]);
    return u;
  }

  // Physical inviscid flux projected on a unit normal.
  ConsVec normalFlux(const PrimVec& w, const Vec2& n) const {
    const Real un = w[1] * n[0] + w[2] * n[1];
    const Real E = w[3] / ((gamma_ - 1.0) * w[0]) + 0.5 * (w[1] * w[1] + w[2] * w[2]);
    ConsVec f{};
    f[0] = w[0] * un;
    f[1] = w[0] * w[1] * un + w[3] * n[0];
    f[2] = w[0] * w[2] * un + w[3] * n[1];
    f[3] = un * (w[0] * E + w[3]);
    return f;
  }

 private:
  Real gamma_ = 1.4;
  Real R_ = 1.0;
  Real pr_ = 0.72;
};

// Molecular transport.  `constant` matches the case Reynolds number exactly;
// `sutherland` is available for dimensional or high-temperature extensions.
class TransportModel {
 public:
  enum class Law { kConstant, kSutherland };

  TransportModel() = default;
  TransportModel(Law law, Real mu_ref, Real t_ref, Real sutherland_s)
      : law_(law), mu_ref_(mu_ref), t_ref_(t_ref), s_(sutherland_s) {}

  Real viscosity(Real T) const {
    if (law_ == Law::kConstant) return mu_ref_;
    const Real r = T / t_ref_;
    return mu_ref_ * r * std::sqrt(r) * (t_ref_ + s_) / (T + s_);
  }
  Real referenceViscosity() const { return mu_ref_; }
  bool active() const { return mu_ref_ > 0.0; }

 private:
  Law law_ = Law::kConstant;
  Real mu_ref_ = 0.0;
  Real t_ref_ = 1.0;
  Real s_ = 110.4;
};

}  // namespace cfd
