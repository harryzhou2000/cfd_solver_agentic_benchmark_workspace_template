#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace cfd {

using GlobalId = std::int64_t;
using LocalIndex = std::int32_t;

constexpr GlobalId invalid_global_id = GlobalId{-1};
constexpr LocalIndex invalid_local_index = LocalIndex{-1};

struct Vec2 {
  double x{0.0};
  double y{0.0};

  constexpr Vec2& operator+=(const Vec2& other) noexcept {
    x += other.x;
    y += other.y;
    return *this;
  }
  constexpr Vec2& operator-=(const Vec2& other) noexcept {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  constexpr Vec2& operator*=(double scale) noexcept {
    x *= scale;
    y *= scale;
    return *this;
  }
};

constexpr Vec2 operator+(Vec2 lhs, const Vec2& rhs) noexcept { return lhs += rhs; }
constexpr Vec2 operator-(Vec2 lhs, const Vec2& rhs) noexcept { return lhs -= rhs; }
constexpr Vec2 operator*(Vec2 value, double scale) noexcept { return value *= scale; }
constexpr Vec2 operator*(double scale, Vec2 value) noexcept { return value *= scale; }
constexpr Vec2 operator/(Vec2 value, double scale) noexcept {
  value.x /= scale;
  value.y /= scale;
  return value;
}
constexpr double dot(const Vec2& lhs, const Vec2& rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y;
}
constexpr double cross(const Vec2& lhs, const Vec2& rhs) noexcept {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}
inline double norm(const Vec2& value) noexcept { return std::sqrt(dot(value, value)); }
inline bool finite(const Vec2& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

}  // namespace cfd
