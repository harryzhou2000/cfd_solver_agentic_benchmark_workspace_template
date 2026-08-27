// Slope limiters.
//
// Both limiters map an unlimited reconstruction increment and the admissible
// excursion of the neighbourhood onto a factor in [0,1].  They are collected
// here so that they can be unit tested independently of the mesh and reused by
// SpatialOperator.
#pragma once

#include <algorithm>

#include "core/Types.hpp"

namespace cfd {

// Barth-Jespersen: the largest factor that keeps the reconstructed value
// inside [W_min, W_max].
inline Real barthJespersenPhi(Real dmax, Real dminus) {
  if (std::abs(dminus) < 1e-300) return 1.0;
  return std::min(1.0, std::max(0.0, dmax / dminus));
}

// Venkatakrishnan's smooth limiter.  `dmax` is (W_max - W_c) when `dminus` > 0
// and (W_min - W_c) otherwise; `eps2` is the smoothing parameter, which makes
// the function differentiable and lets it return 1 in smooth regions.
inline Real venkatakrishnanPhi(Real dmax, Real dminus, Real eps2) {
  const Real d2 = dminus;
  if (std::abs(d2) < 1e-300) return 1.0;
  const Real num = (dmax * dmax + eps2) * d2 + 2.0 * d2 * d2 * dmax;
  const Real den = dmax * dmax + 2.0 * d2 * d2 + d2 * dmax + eps2;
  if (std::abs(den) < 1e-300) return 1.0;
  return std::min(1.0, std::max(0.0, num / (den * d2)));
}

}  // namespace cfd
