#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mpi.h>

namespace cfd {

// ---------------------------------------------------------------------------
// Small fixed-size vector helpers for the 2-D 4-component conservative state.
// ---------------------------------------------------------------------------
using Vec4 = std::array<double, 4>;

inline Vec4 operator+(const Vec4& a, const Vec4& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}
inline Vec4 operator-(const Vec4& a, const Vec4& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}
inline Vec4 operator*(const Vec4& a, double s) {
  return {a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}
inline Vec4 operator*(double s, const Vec4& a) { return a * s; }
inline Vec4 operator/(const Vec4& a, double s) {
  return {a[0] / s, a[1] / s, a[2] / s, a[3] / s};
}
inline Vec4& operator+=(Vec4& a, const Vec4& b) {
  a[0] += b[0]; a[1] += b[1]; a[2] += b[2]; a[3] += b[3];
  return a;
}
inline Vec4& operator-=(Vec4& a, const Vec4& b) {
  a[0] -= b[0]; a[1] -= b[1]; a[2] -= b[2]; a[3] -= b[3];
  return a;
}
inline double dot4(const Vec4& a, const Vec4& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

// ---------------------------------------------------------------------------
// Diagnostics / logging
// ---------------------------------------------------------------------------
extern int g_rank;   // MPI rank
extern int g_nranks; // MPI world size

[[noreturn]] inline void fatal(const std::string& msg) {
  if (g_rank == 0) std::cerr << "[cfd_solver] ERROR: " << msg << "\n";
  MPI_Abort(MPI_COMM_WORLD, 1);
  std::abort();
}

inline void log0(const std::string& msg) {
  if (g_rank == 0) std::cout << "[cfd_solver] " << msg << std::endl;
}

inline void log_always(const std::string& msg) {
  std::cout << "[rank " << g_rank << "] " << msg << std::endl;
}

inline std::string fmt1(const char* fmt, double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), fmt, v);
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Simple stopwatch
// ---------------------------------------------------------------------------
class Timer {
 public:
  Timer() { reset(); }
  void reset() { start_ = MPI_Wtime(); }
  double elapsed() const { return MPI_Wtime() - start_; }
 private:
  double start_ = 0.0;
};

}  // namespace cfd
