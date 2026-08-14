#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cfds {

using Vec2 = std::array<double, 2>;

// Conservative state ordering: [rho, rhou, rhov, rhoE].
using ConsVec = std::array<double, 4>;

inline double dot(const Vec2& a, const Vec2& b) { return a[0]*b[0] + a[1]*b[1]; }

struct Primitive {
  double rho = 0.0;
  double u = 0.0;
  double v = 0.0;
  double p = 0.0;
};

// Exit helper: print message to stderr and terminate.
[[noreturn]] inline void fatal(const std::string& msg) {
  std::fprintf(stderr, "[cfds] FATAL: %s\n", msg.c_str());
  std::abort();
}

}  // namespace cfds
