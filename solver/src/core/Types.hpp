// Basic scalar/index types and small fixed-size algebra helpers.
//
// The solver is written so that the spatial dimension and the number of
// conserved variables are compile-time constants gathered in one place.  A
// future 3-D or multi-species extension changes these constants and the
// physics module rather than the mesh/parallel infrastructure.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cfd {

using Real = double;
using Index = std::int32_t;   // local (rank) indices
using GlobalIndex = std::int64_t;

inline constexpr int kDim = 2;         // spatial dimension
inline constexpr int kNVar = kDim + 2; // rho, rho*u_i, rho*E

using Vec2 = std::array<Real, 2>;
using ConsVec = std::array<Real, kNVar>;   // [rho, rho u, rho v, rho E]
using PrimVec = std::array<Real, kNVar>;   // [rho, u, v, p]

inline constexpr Real kEps = 1.0e-300;

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return {a[0] + b[0], a[1] + b[1]}; }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return {a[0] - b[0], a[1] - b[1]}; }
inline Vec2 operator*(Real s, const Vec2& a) { return {s * a[0], s * a[1]}; }
inline Real dot(const Vec2& a, const Vec2& b) { return a[0] * b[0] + a[1] * b[1]; }
inline Real cross(const Vec2& a, const Vec2& b) { return a[0] * b[1] - a[1] * b[0]; }
inline Real norm(const Vec2& a) { return std::sqrt(dot(a, a)); }

template <std::size_t N>
inline std::array<Real, N> operator+(const std::array<Real, N>& a, const std::array<Real, N>& b) {
  std::array<Real, N> r{};
  for (std::size_t i = 0; i < N; ++i) r[i] = a[i] + b[i];
  return r;
}
template <std::size_t N>
inline std::array<Real, N> operator-(const std::array<Real, N>& a, const std::array<Real, N>& b) {
  std::array<Real, N> r{};
  for (std::size_t i = 0; i < N; ++i) r[i] = a[i] - b[i];
  return r;
}
template <std::size_t N>
inline std::array<Real, N> operator*(Real s, const std::array<Real, N>& a) {
  std::array<Real, N> r{};
  for (std::size_t i = 0; i < N; ++i) r[i] = s * a[i];
  return r;
}

// Gradient of one scalar field in kDim directions.
using Grad = std::array<Real, kDim>;
// Gradients of all kNVar primitive variables.
using GradSet = std::array<Grad, kNVar>;

}  // namespace cfd
