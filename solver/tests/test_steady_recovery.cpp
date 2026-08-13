#include "steady_recovery.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool near(double left, double right) {
  return std::abs(left - right) < 1.0e-14;
}

} // namespace

int main() {
  using aerofv::bounded_steady_recovery_cfl_cap;

  assert(near(bounded_steady_recovery_cfl_cap(50.0), 5.0));
  assert(near(bounded_steady_recovery_cfl_cap(5.0), 5.0));
  assert(near(bounded_steady_recovery_cfl_cap(3.0), 3.0));

  std::cout << "steady recovery cap tests passed\n";
}
