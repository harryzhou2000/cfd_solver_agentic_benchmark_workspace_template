#pragma once
// Common small types and helpers for the fv2d solver.
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>

namespace fv {

using std::array;
using std::string;
using std::vector;

// Conservative state: [rho, rho*u, rho*v, rho*E]
using State = array<double, 4>;

struct Vec2 {
  double x = 0.0, y = 0.0;
  Vec2() = default;
  Vec2(double x_, double y_) : x(x_), y(y_) {}
  Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(double s) const { return {x * s, y * s}; }
  double dot(const Vec2& o) const { return x * o.x + y * o.y; }
  double norm() const { return std::sqrt(x * x + y * y); }
};

[[noreturn]] inline void die(const string& msg) {
  throw std::runtime_error(msg);
}

inline void check(bool cond, const string& msg) {
  if (!cond) die(msg);
}

}  // namespace fv
