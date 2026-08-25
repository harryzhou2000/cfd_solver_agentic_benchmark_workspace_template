#pragma once
// Common small types and helpers for the 2-D compressible FVM solver.
// All solver logic in this project is original code written for this
// benchmark; only infrastructure libraries (CGNS/HDF5/METIS/JSON) are used.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

using Vec4 = std::array<double, 4>;  // one conserved/primitive state (2-D, 4 eqns)

struct Vec2 {
  double x = 0.0, y = 0.0;
};

inline Vec4 make4(double a, double b, double c, double d) { return Vec4{a, b, c, d}; }
inline Vec4 add4(const Vec4& a, const Vec4& b) {
  return Vec4{a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}
inline Vec4 sub4(const Vec4& a, const Vec4& b) {
  return Vec4{a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
}
inline Vec4 scale4(const Vec4& a, double s) {
  return Vec4{a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}
inline void iadd4(Vec4& a, const Vec4& b) {
  a[0] += b[0]; a[1] += b[1]; a[2] += b[2]; a[3] += b[3];
}
inline void isub4(Vec4& a, const Vec4& b) {
  a[0] -= b[0]; a[1] -= b[1]; a[2] -= b[2]; a[3] -= b[3];
}

// Boundary-condition kinds supported by the solver (extensible enum).
enum class BCType { Farfield, SlipWall, NoSlipAdiabaticWall };

BCType parse_bc_type(const std::string& s);
std::string bc_type_name(BCType t);

// Gas constants for a calorically perfect gas.
struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double cp() const { return gamma * R / (gamma - 1.0); }
  double cv() const { return R / (gamma - 1.0); }
};

// Free-stream reference state for a case.
struct FreeStream {
  double mach = 0.0;
  double aoa_deg = 0.0;
  double rho = 1.0;
  double vmag = 1.0;
  double p = 1.0;
  double u = 1.0, v = 0.0;   // Cartesian components from AoA
  double T = 1.0;
  double a = 1.0;            // sound speed
  double mu = 0.0;           // dynamic viscosity (0 for inviscid)
  double k_cond = 0.0;       // heat conductivity
  double reynolds = 0.0;
  bool viscous = false;
  double q_dyn = 0.5;        // dynamic pressure 0.5*rho*|V|^2
};

}  // namespace cfd
