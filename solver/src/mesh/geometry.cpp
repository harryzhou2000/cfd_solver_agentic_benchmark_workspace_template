#include "mesh/geometry.h"

#include <cmath>

namespace cns2d {

Real polygonAreaCentroid(const Real *xs, const Real *ys, int n, Vec2 &centroid) {
  // Standard polygon area/centroid from the cross-product (shoelace) sum.
  Real a2 = 0.0;  // twice the signed area
  Real cx = 0.0;
  Real cy = 0.0;
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    const Real cross = xs[i] * ys[j] - xs[j] * ys[i];
    a2 += cross;
    cx += (xs[i] + xs[j]) * cross;
    cy += (ys[i] + ys[j]) * cross;
  }
  const Real area = 0.5 * a2;
  if (std::abs(a2) > 0.0) {
    centroid.x = cx / (3.0 * a2);
    centroid.y = cy / (3.0 * a2);
  } else {
    // Degenerate polygon: fall back to the vertex average so callers still get
    // a finite point to report in the error path.
    Real sx = 0.0;
    Real sy = 0.0;
    for (int i = 0; i < n; ++i) {
      sx += xs[i];
      sy += ys[i];
    }
    centroid.x = sx / static_cast<Real>(n);
    centroid.y = sy / static_cast<Real>(n);
  }
  return area;
}

}  // namespace cns2d
