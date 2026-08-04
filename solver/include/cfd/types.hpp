#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cfd {

constexpr int nvars = 4;

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double b) { return {a.x * b, a.y * b}; }
inline Vec2 operator*(double b, Vec2 a) { return a * b; }
inline Vec2 operator/(Vec2 a, double b) { return {a.x / b, a.y / b}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) {
  a.x += b.x;
  a.y += b.y;
  return a;
}
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double norm2(Vec2 a) { return dot(a, a); }
inline double norm(Vec2 a) { return std::sqrt(norm2(a)); }

using Conserved = std::array<double, nvars>;

inline Conserved zeros() { return {0.0, 0.0, 0.0, 0.0}; }
inline Conserved operator+(const Conserved& a, const Conserved& b) {
  Conserved r{};
  for (int k = 0; k < nvars; ++k) r[static_cast<std::size_t>(k)] = a[static_cast<std::size_t>(k)] + b[static_cast<std::size_t>(k)];
  return r;
}
inline Conserved operator-(const Conserved& a, const Conserved& b) {
  Conserved r{};
  for (int k = 0; k < nvars; ++k) r[static_cast<std::size_t>(k)] = a[static_cast<std::size_t>(k)] - b[static_cast<std::size_t>(k)];
  return r;
}
inline Conserved operator*(const Conserved& a, double s) {
  Conserved r{};
  for (int k = 0; k < nvars; ++k) r[static_cast<std::size_t>(k)] = a[static_cast<std::size_t>(k)] * s;
  return r;
}
inline Conserved operator*(double s, const Conserved& a) { return a * s; }
inline Conserved operator/(const Conserved& a, double s) { return a * (1.0 / s); }
inline Conserved& operator+=(Conserved& a, const Conserved& b) {
  for (int k = 0; k < nvars; ++k) a[static_cast<std::size_t>(k)] += b[static_cast<std::size_t>(k)];
  return a;
}
inline Conserved& operator-=(Conserved& a, const Conserved& b) {
  for (int k = 0; k < nvars; ++k) a[static_cast<std::size_t>(k)] -= b[static_cast<std::size_t>(k)];
  return a;
}

struct Primitive {
  double rho = 1.0;
  double u = 0.0;
  double v = 0.0;
  double p = 1.0;

  double& operator[](std::size_t i) {
    if (i == 0) return rho;
    if (i == 1) return u;
    if (i == 2) return v;
    return p;
  }
  double operator[](std::size_t i) const {
    if (i == 0) return rho;
    if (i == 1) return u;
    if (i == 2) return v;
    return p;
  }
};

struct PrimitiveGradient {
  std::array<Vec2, nvars> q{};
};

enum class BoundaryType : std::uint8_t {
  interior = 0,
  farfield = 1,
  slip_wall = 2,
  no_slip_adiabatic_wall = 3,
};

inline std::string to_string(BoundaryType type) {
  switch (type) {
    case BoundaryType::interior: return "interior";
    case BoundaryType::farfield: return "farfield";
    case BoundaryType::slip_wall: return "slip_wall";
    case BoundaryType::no_slip_adiabatic_wall: return "no_slip_adiabatic_wall";
  }
  throw std::runtime_error("unknown boundary type");
}

inline BoundaryType boundary_type_from_string(const std::string& value) {
  if (value == "farfield") return BoundaryType::farfield;
  if (value == "slip_wall") return BoundaryType::slip_wall;
  if (value == "no_slip_adiabatic_wall") return BoundaryType::no_slip_adiabatic_wall;
  throw std::runtime_error("unsupported boundary condition: " + value);
}

}  // namespace cfd
