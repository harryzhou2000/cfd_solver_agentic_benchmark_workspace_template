#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfd {

using Real = double;

struct Vec2 {
  Real x{0.0};
  Real y{0.0};

  Vec2() = default;
  Vec2(Real x_, Real y_) : x(x_), y(y_) {}

  Vec2& operator+=(const Vec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  Vec2& operator-=(const Vec2& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  Vec2& operator*=(Real a) {
    x *= a;
    y *= a;
    return *this;
  }
};

inline Vec2 operator+(Vec2 a, const Vec2& b) { return a += b; }
inline Vec2 operator-(Vec2 a, const Vec2& b) { return a -= b; }
inline Vec2 operator*(Real a, Vec2 b) { return b *= a; }
inline Vec2 operator*(Vec2 b, Real a) { return b *= a; }
inline Real dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline Real norm(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline Real cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

inline std::string trim_copy(std::string s) {
  const char* ws = " \t\r\n";
  const auto b = s.find_first_not_of(ws);
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(ws);
  return s.substr(b, e - b + 1);
}

class CfdError : public std::runtime_error {
 public:
  explicit CfdError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace cfd
