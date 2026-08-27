// Elementary polygon / edge geometry used by both the global preprocessing
// mesh and the rank-local mesh.  Everything here is dimension-aware through
// kDim so that a 3-D extension replaces only these primitives.
#pragma once

#include <vector>

#include "core/Types.hpp"

namespace cfd {
namespace geom {

// Signed area of a simple polygon given in order (positive if counter-clockwise).
inline Real signedArea(const Real* x, const Real* y, const Index* nodes, int n) {
  Real a = 0.0;
  for (int i = 0; i < n; ++i) {
    const Index i0 = nodes[i];
    const Index i1 = nodes[(i + 1) % n];
    a += x[i0] * y[i1] - x[i1] * y[i0];
  }
  return 0.5 * a;
}

// Area-weighted centroid of a simple polygon.
inline Vec2 centroid(const Real* x, const Real* y, const Index* nodes, int n) {
  Real cx = 0.0, cy = 0.0, a = 0.0;
  for (int i = 0; i < n; ++i) {
    const Index i0 = nodes[i];
    const Index i1 = nodes[(i + 1) % n];
    const Real cr = x[i0] * y[i1] - x[i1] * y[i0];
    a += cr;
    cx += (x[i0] + x[i1]) * cr;
    cy += (y[i0] + y[i1]) * cr;
  }
  a *= 0.5;
  if (std::abs(a) < 1e-300) {  // degenerate: fall back to vertex average
    cx = cy = 0.0;
    for (int i = 0; i < n; ++i) { cx += x[nodes[i]]; cy += y[nodes[i]]; }
    return {cx / n, cy / n};
  }
  return {cx / (6.0 * a), cy / (6.0 * a)};
}

// Unit normal of the edge (n0 -> n1) rotated -90 degrees.  For a
// counter-clockwise polygon traversal this points out of the polygon.
inline Vec2 edgeNormal(Real x0, Real y0, Real x1, Real y1) {
  const Real dx = x1 - x0, dy = y1 - y0;
  const Real len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.0) return {0.0, 0.0};
  return {dy / len, -dx / len};
}

inline Real edgeLength(Real x0, Real y0, Real x1, Real y1) {
  const Real dx = x1 - x0, dy = y1 - y0;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace geom
}  // namespace cfd
