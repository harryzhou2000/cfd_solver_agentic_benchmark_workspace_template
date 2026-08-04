#pragma once

#include <array>
#include <cmath>

#include "cfd/config.hpp"
#include "cfd/mesh.hpp"

namespace cfd {

constexpr double kDensityFloor = 1.0e-10;
constexpr double kPressureFloor = 1.0e-10;

inline Vec2 operator+(const Vec2 a, const Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(const Vec2 a, const Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(const double scale, const Vec2 a) { return {scale * a.x, scale * a.y}; }
inline double dot(const Vec2 a, const Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double norm(const Vec2 a) { return std::sqrt(dot(a, a)); }

using Conserved = std::array<double, 4>;

struct Primitive {
  double rho{1.0};
  double u{0.0};
  double v{0.0};
  double pressure{1.0};
  double temperature{1.0};
  double sound_speed{1.0};
};

class PerfectGas {
 public:
  explicit PerfectGas(GasModel gas) : gas_(gas) {}

  [[nodiscard]] const GasModel& parameters() const { return gas_; }
  [[nodiscard]] double cp() const { return gas_.gamma * gas_.gas_constant / (gas_.gamma - 1.0); }
  [[nodiscard]] Primitive primitive(const Conserved& state) const;
  [[nodiscard]] Conserved conserved(const Primitive& state) const;
  [[nodiscard]] Conserved freestream_state(const Freestream& freestream) const;
  [[nodiscard]] bool physical(const Conserved& state) const;
  [[nodiscard]] Conserved enforce_physical(const Conserved& candidate, const Conserved& fallback) const;

 private:
  GasModel gas_;
};

Conserved rusanov_flux(const PerfectGas& gas, const Primitive& left, const Primitive& right,
                       Vec2 unit_normal, double dissipation_scale = 1.0);
Conserved euler_flux(const PerfectGas& gas, const Primitive& state, Vec2 unit_normal);
Conserved hllc_flux(const PerfectGas& gas, const Primitive& left, const Primitive& right,
                    Vec2 unit_normal, double dissipation_scale = 1.0);
Primitive reflected_slip_state(const Primitive& inside, Vec2 unit_normal);
Primitive reflected_no_slip_adiabatic_state(const Primitive& inside);

}  // namespace cfd
