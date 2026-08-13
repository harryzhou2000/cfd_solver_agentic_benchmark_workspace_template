#pragma once

#include <algorithm>

namespace aerofv {

// Keep the recovery cap conservative without ever reporting a value above the
// valid maximum prescribed by an individual steady case.
inline constexpr double kSteadyRecoveryCflLimit = 5.0;

[[nodiscard]] inline double bounded_steady_recovery_cfl_cap(
    double configured_cfl_maximum) noexcept {
  return std::min(kSteadyRecoveryCflLimit, configured_cfl_maximum);
}

} // namespace aerofv
