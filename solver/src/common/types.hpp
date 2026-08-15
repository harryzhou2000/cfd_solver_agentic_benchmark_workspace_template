#pragma once

// Simple POD geometry/vector types used for mesh and solution data.
// Eigen is deliberately NOT used here: these structs are small, frequently
// copied, and must stay trivially serializable for partitioning later.

#include <cmath>
#include <stdexcept>
#include <string>

namespace cfd {

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------
struct Vector3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  Vector3() = default;
  Vector3(double x_, double y_, double z_ = 0.0) : x(x_), y(y_), z(z_) {}

  Vector3& operator+=(const Vector3& o) {
    x += o.x; y += o.y; z += o.z;
    return *this;
  }
  Vector3& operator-=(const Vector3& o) {
    x -= o.x; y -= o.y; z -= o.z;
    return *this;
  }
  Vector3& operator*=(double s) {
    x *= s; y *= s; z *= s;
    return *this;
  }
  Vector3& operator/=(double s) {
    x /= s; y /= s; z /= s;
    return *this;
  }

  double dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }

  Vector3 cross(const Vector3& o) const {
    return Vector3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
  }

  double norm() const { return std::sqrt(x * x + y * y + z * z); }
};

inline Vector3 operator+(Vector3 a, const Vector3& b) { a += b; return a; }
inline Vector3 operator-(Vector3 a, const Vector3& b) { a -= b; return a; }
inline Vector3 operator*(Vector3 a, double s) { a *= s; return a; }
inline Vector3 operator*(double s, Vector3 a) { a *= s; return a; }
inline Vector3 operator/(Vector3 a, double s) { a /= s; return a; }
inline Vector3 operator-(Vector3 a) { return Vector3(-a.x, -a.y, -a.z); }

// ---------------------------------------------------------------------------
// Vector2
// ---------------------------------------------------------------------------
struct Vector2 {
  double x = 0.0;
  double y = 0.0;

  Vector2() = default;
  Vector2(double x_, double y_) : x(x_), y(y_) {}

  Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
  Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }
  Vector2& operator*=(double s) { x *= s; y *= s; return *this; }
  Vector2& operator/=(double s) { x /= s; y /= s; return *this; }

  double dot(const Vector2& o) const { return x * o.x + y * o.y; }
  double norm() const { return std::sqrt(x * x + y * y); }
};

inline Vector2 operator+(Vector2 a, const Vector2& b) { a += b; return a; }
inline Vector2 operator-(Vector2 a, const Vector2& b) { a -= b; return a; }
inline Vector2 operator*(Vector2 a, double s) { a *= s; return a; }
inline Vector2 operator*(double s, Vector2 a) { a *= s; return a; }
inline Vector2 operator/(Vector2 a, double s) { a /= s; return a; }
inline Vector2 operator-(Vector2 a) { return Vector2(-a.x, -a.y); }

}  // namespace cfd
