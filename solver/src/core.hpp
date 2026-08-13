#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace aerofv {

struct Vec2 {
  double x{0.0};
  double y{0.0};

  constexpr Vec2 operator+(const Vec2 &other) const {
    return {x + other.x, y + other.y};
  }
  constexpr Vec2 operator-(const Vec2 &other) const {
    return {x - other.x, y - other.y};
  }
  constexpr Vec2 operator*(double scalar) const {
    return {x * scalar, y * scalar};
  }
  constexpr Vec2 operator/(double scalar) const {
    return {x / scalar, y / scalar};
  }
  Vec2 &operator+=(const Vec2 &other) {
    x += other.x;
    y += other.y;
    return *this;
  }
  Vec2 &operator-=(const Vec2 &other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  Vec2 &operator*=(double scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
  }
};

inline constexpr Vec2 operator*(double scalar, const Vec2 &value) {
  return value * scalar;
}
inline constexpr double dot(const Vec2 &a, const Vec2 &b) {
  return a.x * b.x + a.y * b.y;
}
inline constexpr double cross(const Vec2 &a, const Vec2 &b) {
  return a.x * b.y - a.y * b.x;
}
inline double norm(const Vec2 &value) { return std::sqrt(dot(value, value)); }

using Conservative = std::array<double, 4>;

inline Conservative operator+(const Conservative &a, const Conservative &b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}
inline Conservative operator-(const Conservative &a, const Conservative &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}
inline Conservative operator*(const Conservative &a, double scalar) {
  return {a[0] * scalar, a[1] * scalar, a[2] * scalar,
          a[3] * scalar};
}
inline Conservative operator*(double scalar, const Conservative &a) {
  return a * scalar;
}
inline Conservative operator/(const Conservative &a, double scalar) {
  return a * (1.0 / scalar);
}
inline Conservative &operator+=(Conservative &a, const Conservative &b) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] += b[i];
  }
  return a;
}
inline Conservative &operator-=(Conservative &a, const Conservative &b) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] -= b[i];
  }
  return a;
}
inline Conservative &operator*=(Conservative &a, double scalar) {
  for (double &value : a) {
    value *= scalar;
  }
  return a;
}

struct Primitive {
  double rho{1.0};
  double u{0.0};
  double v{0.0};
  double p{1.0};
};

using PrimitiveGradient = std::array<Vec2, 4>; // rho, u, v, pressure

struct GasModel {
  double gamma{1.4};
  double gas_constant{1.0};
  double prandtl{0.72};
  double density_floor{1.0e-10};
  double pressure_floor{1.0e-10};
};

enum class BoundaryType : std::uint8_t {
  interior,
  farfield,
  slip_wall,
  no_slip_adiabatic_wall,
  unknown,
};

inline std::string to_string(BoundaryType type) {
  switch (type) {
  case BoundaryType::interior:
    return "interior";
  case BoundaryType::farfield:
    return "farfield";
  case BoundaryType::slip_wall:
    return "slip_wall";
  case BoundaryType::no_slip_adiabatic_wall:
    return "no_slip_adiabatic_wall";
  case BoundaryType::unknown:
    return "unknown";
  }
  throw std::logic_error("invalid boundary type");
}

inline BoundaryType boundary_type_from_string(const std::string &name) {
  if (name == "farfield") {
    return BoundaryType::farfield;
  }
  if (name == "slip_wall") {
    return BoundaryType::slip_wall;
  }
  if (name == "no_slip_adiabatic_wall") {
    return BoundaryType::no_slip_adiabatic_wall;
  }
  if (name == "interior") {
    return BoundaryType::interior;
  }
  return BoundaryType::unknown;
}

inline bool finite(const Conservative &state) {
  for (double value : state) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

} // namespace aerofv
