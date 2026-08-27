// cns2d -- calorically perfect gas model and the nondimensional flow context.
//
// The gas model is deliberately a small value type with inline conversions
// rather than a set of free functions scattered through the flux kernels.
// Swapping in a different equation of state later means providing another class
// with the same conversion interface (pressure/temperature/soundSpeed/energy)
// and templating or injecting it into the residual, without touching mesh,
// partitioning, driver or I/O code.
#pragma once

#include <cmath>

#include "core/case_input.h"
#include "core/types.h"

namespace cns2d {

class PerfectGas {
 public:
  PerfectGas() = default;
  explicit PerfectGas(const GasProperties &props)
      : gamma_(props.gamma), R_(props.R), prandtl_(props.prandtl) {
    gamma_minus_one_ = gamma_ - 1.0;
    cp_ = gamma_ * R_ / gamma_minus_one_;
    cv_ = R_ / gamma_minus_one_;
  }

  Real gamma() const { return gamma_; }
  Real R() const { return R_; }
  Real prandtl() const { return prandtl_; }
  Real cp() const { return cp_; }
  Real cv() const { return cv_; }

  // p = (gamma - 1) * rho * e   with   e = E - 0.5 * |u|^2
  Real pressureFromCons(const ConsVec &U) const {
    const Real inv_rho = 1.0 / U[kRho];
    const Real kinetic = 0.5 * (U[kRhoU] * U[kRhoU] + U[kRhoV] * U[kRhoV]) * inv_rho;
    return gamma_minus_one_ * (U[kRhoE] - kinetic);
  }

  Real temperatureFromRhoP(Real rho, Real p) const { return p / (rho * R_); }

  Real soundSpeed(Real rho, Real p) const { return std::sqrt(gamma_ * p / rho); }

  // Total energy per unit mass from primitive variables.
  Real totalEnergy(Real rho, Real u, Real v, Real p) const {
    return p / (gamma_minus_one_ * rho) + 0.5 * (u * u + v * v);
  }

  Real totalEnthalpy(Real rho, Real u, Real v, Real p) const {
    return (gamma_ / gamma_minus_one_) * p / rho + 0.5 * (u * u + v * v);
  }

  ConsVec consFromPrim(const PrimVec &W) const {
    const Real rho = W[kPrimRho];
    const Real u = W[kPrimU];
    const Real v = W[kPrimV];
    const Real p = W[kPrimP];
    return {rho, rho * u, rho * v, rho * totalEnergy(rho, u, v, p)};
  }

  PrimVec primFromCons(const ConsVec &U) const {
    const Real inv_rho = 1.0 / U[kRho];
    PrimVec W{};
    W[kPrimRho] = U[kRho];
    W[kPrimU] = U[kRhoU] * inv_rho;
    W[kPrimV] = U[kRhoV] * inv_rho;
    W[kPrimP] = pressureFromCons(U);
    return W;
  }

  // Thermal conductivity from viscosity: k = mu * cp / Pr (Fourier heat flux).
  Real conductivity(Real mu) const { return mu * cp_ / prandtl_; }

 private:
  Real gamma_{1.4};
  Real gamma_minus_one_{0.4};
  Real R_{1.0};
  Real prandtl_{0.72};
  Real cp_{3.5};
  Real cv_{2.5};
};

// Transport model.  Constant viscosity is derived from the case Reynolds
// number so it always matches the requested Re; Sutherland's law is available
// for cases that request it.
class TransportModel {
 public:
  TransportModel() = default;

  static TransportModel makeInviscid() {
    TransportModel t;
    t.enabled_ = false;
    return t;
  }

  // mu = rho_inf * U_inf * L_ref / Re
  static TransportModel makeConstant(Real rho_inf, Real u_inf, Real l_ref, Real reynolds) {
    TransportModel t;
    t.enabled_ = true;
    t.sutherland_ = false;
    t.mu_ref_ = rho_inf * u_inf * l_ref / reynolds;
    return t;
  }

  // Sutherland's law nondimensionalised about the freestream temperature.
  static TransportModel makeSutherland(Real mu_ref, Real t_ref, Real s_over_tref) {
    TransportModel t;
    t.enabled_ = true;
    t.sutherland_ = true;
    t.mu_ref_ = mu_ref;
    t.t_ref_ = t_ref;
    t.sutherland_s_ = s_over_tref * t_ref;
    return t;
  }

  bool enabled() const { return enabled_; }
  Real referenceViscosity() const { return mu_ref_; }

  Real viscosity(Real temperature) const {
    if (!enabled_) return 0.0;
    if (!sutherland_) return mu_ref_;
    const Real ratio = temperature / t_ref_;
    return mu_ref_ * ratio * std::sqrt(ratio) * (t_ref_ + sutherland_s_) /
           (temperature + sutherland_s_);
  }

 private:
  bool enabled_{false};
  bool sutherland_{false};
  Real mu_ref_{0.0};
  Real t_ref_{1.0};
  Real sutherland_s_{0.0};
};

// Everything the residual needs about the physical problem, assembled once.
struct FlowContext {
  PerfectGas gas;
  TransportModel transport;
  ConsVec freestream_cons{};
  PrimVec freestream_prim{};
  Real freestream_mach{0.0};
  Real freestream_speed{0.0};
  Real freestream_sound_speed{0.0};
  Real dynamic_pressure{0.0};  // 0.5 * rho_inf * U_inf^2
  Vec2 flow_direction{1.0, 0.0};
  bool viscous{false};

  // Reference quantities for force coefficients.
  Real ref_area{1.0};
  Real ref_length{1.0};
  Vec2 moment_center{};
};

// Build the flow context from a parsed case.  Verifies that the supplied
// freestream pressure, density and Mach number are mutually consistent.
FlowContext makeFlowContext(const CaseInput &input);

}  // namespace cns2d
