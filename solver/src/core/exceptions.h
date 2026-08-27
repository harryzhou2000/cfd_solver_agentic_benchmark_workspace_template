// cns2d -- error type used for all checked input / runtime failures.
// main() converts these into a clear message plus a nonzero exit status,
// which is what the benchmark CLI contract requires for malformed inputs.
#pragma once

#include <stdexcept>
#include <string>

namespace cns2d {

class CnsError : public std::runtime_error {
 public:
  explicit CnsError(const std::string &what) : std::runtime_error(what) {}
};

}  // namespace cns2d
