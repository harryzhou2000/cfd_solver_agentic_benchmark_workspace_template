// cns2d -- core scalar/vector types and compile-time problem dimensions.
//
// Extensibility note: the spatial dimension and the number of conserved
// variables are compile-time constants declared here, and every module below
// refers to them symbolically (kDim, kNumVars) instead of to literal 2/4.
// Extending to 3-D or to additional transported scalars (RANS, species) is
// therefore a matter of changing these constants plus the flux/gradient
// kernels, not of rewriting mesh, partitioning, I/O, or driver code.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cns2d {

using Real = double;
using Index = std::int32_t;      // rank-local indices
using GlobalIndex = std::int64_t; // global mesh indices

inline constexpr int kDim = 2;
inline constexpr int kNumVars = 4;  // rho, rho*u, rho*v, rho*E

// Conserved-variable component ordering.
enum : int { kRho = 0, kRhoU = 1, kRhoV = 2, kRhoE = 3 };

// Fixed-size algebraic containers.  Deliberately small and trivially
// copyable so they can be packed straight into MPI buffers.
struct Vec2 {
  Real x{0.0};
  Real y{0.0};
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Real s, Vec2 a) { return {s * a.x, s * a.y}; }
inline Vec2 operator*(Vec2 a, Real s) { return {s * a.x, s * a.y}; }
inline Real dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline Real norm(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// State vector of conserved variables in one cell / at one face state.
using ConsVec = std::array<Real, kNumVars>;

inline ConsVec operator+(const ConsVec &a, const ConsVec &b) {
  ConsVec r{};
  for (int k = 0; k < kNumVars; ++k) r[k] = a[k] + b[k];
  return r;
}
inline ConsVec operator-(const ConsVec &a, const ConsVec &b) {
  ConsVec r{};
  for (int k = 0; k < kNumVars; ++k) r[k] = a[k] - b[k];
  return r;
}
inline ConsVec operator*(Real s, const ConsVec &a) {
  ConsVec r{};
  for (int k = 0; k < kNumVars; ++k) r[k] = s * a[k];
  return r;
}
inline void addScaled(ConsVec &acc, Real s, const ConsVec &a) {
  for (int k = 0; k < kNumVars; ++k) acc[k] += s * a[k];
}

// Primitive variables used by reconstruction, limiting and viscous fluxes.
// Reconstructing primitives (rho, u, v, p) rather than conserved variables
// keeps the limiter directly tied to the positivity constraints.
enum : int { kPrimRho = 0, kPrimU = 1, kPrimV = 2, kPrimP = 3 };
using PrimVec = std::array<Real, kNumVars>;

// Gradient of a variable set: kNumVars rows, kDim columns.
using PrimGrad = std::array<Vec2, kNumVars>;

inline constexpr Real kTiny = 1.0e-300;
inline constexpr Real kEps = std::numeric_limits<Real>::epsilon();

}  // namespace cns2d
