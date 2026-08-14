#pragma once

#include <cmath>

namespace cfd {

// Neumaier compensation preserves low-order terms when adding values with
// widely different magnitudes. It is suitable for cancellation-dominated
// finite-volume face assembly while retaining a double-valued result.
class CompensatedSum {
 public:
  void add(double value) noexcept {
    const double next = sum_ + value;
    if (std::abs(sum_) >= std::abs(value)) {
      correction_ += (sum_ - next) + value;
    } else {
      correction_ += (value - next) + sum_;
    }
    sum_ = next;
  }

  double value() const noexcept { return sum_ + correction_; }

 private:
  double sum_{};
  double correction_{};
};

}  // namespace cfd
