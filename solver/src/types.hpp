#pragma once
// Common types and includes for the CFDNS2D solver.
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <limits>

namespace cfd {

using Int = int32_t;
using Int64 = int64_t;
using Real = double;

inline constexpr int NEQ = 4; // rho, rhou, rhov, rhoE
inline constexpr Real PI = 3.14159265358979323846;

struct Vec2 {
    Real x = 0.0, y = 0.0;
    Vec2() = default;
    Vec2(Real x_, Real y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(Real s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};

inline Real dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline Real norm(const Vec2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }

using Cons = std::array<Real, NEQ>; // [rho, rhou, rhov, rhoE]
using Prim = std::array<Real, NEQ>; // [rho, u, v, p]

enum class BCType : Int { Interior = 0, Farfield, SlipWall, NoSlipWall };

} // namespace cfd
