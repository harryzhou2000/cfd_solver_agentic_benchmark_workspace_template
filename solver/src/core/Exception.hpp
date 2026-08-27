// Solver-level error type.  All recoverable input/setup problems are reported
// through CfdError so main() can print one clear message and exit nonzero.
#pragma once

#include <stdexcept>
#include <string>
#include <sstream>

namespace cfd {

class CfdError : public std::runtime_error {
 public:
  explicit CfdError(const std::string& what) : std::runtime_error(what) {}
};

// Small helper to build formatted messages without pulling in <format>.
class ErrorBuilder {
 public:
  template <typename T>
  ErrorBuilder& operator<<(const T& v) {
    oss_ << v;
    return *this;
  }
  [[noreturn]] void raise() const { throw CfdError(oss_.str()); }
  std::string str() const { return oss_.str(); }

 private:
  std::ostringstream oss_;
};

#define CFD_THROW(msg_stream)                        \
  do {                                               \
    ::cfd::ErrorBuilder _eb;                         \
    _eb << msg_stream;                               \
    _eb.raise();                                     \
  } while (false)

#define CFD_CHECK(cond, msg_stream)                  \
  do {                                               \
    if (!(cond)) CFD_THROW(msg_stream);              \
  } while (false)

}  // namespace cfd
