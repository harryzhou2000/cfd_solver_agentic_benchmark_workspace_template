#pragma once
// Core numerical types for the 2-D compressible Navier-Stokes solver.
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

// Conservative state: [rho, rho*u, rho*v, rho*E]
struct Cons {
  double q[4];
  double& r() { return q[0]; }
  double& ru() { return q[1]; }
  double& rv() { return q[2]; }
  double& rE() { return q[3]; }
  double r() const { return q[0]; }
  double ru() const { return q[1]; }
  double rv() const { return q[2]; }
  double rE() const { return q[3]; }
  Cons() { for (int i = 0; i < 4; ++i) q[i] = 0.0; }
};

// Primitive state: [rho, u, v, p]
struct Prim {
  double q[4];
  double& r() { return q[0]; }
  double& u() { return q[1]; }
  double& v() { return q[2]; }
  double& p() { return q[3]; }
  double r() const { return q[0]; }
  double u() const { return q[1]; }
  double v() const { return q[2]; }
  double p() const { return q[3]; }
  Prim() { for (int i = 0; i < 4; ++i) q[i] = 0.0; }
};

// Boundary condition types supported by the cases.
enum class BCType : int {
  Farfield = 0,
  SlipWall = 1,
  NoSlipAdiabaticWall = 2,
  InteriorInterface = 3,   // matched interior face (cross-zone)
  None = 4,
};

inline const char* bcTypeName(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
    case BCType::InteriorInterface: return "interior_interface";
    default: return "none";
  }
}

}  // namespace cfd
