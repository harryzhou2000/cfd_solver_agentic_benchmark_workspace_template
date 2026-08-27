// Probe of the slip-wall flux identity claimed by the report.
//
// This calls the SOLVER'S OWN roeFlux() and slipWallState() rather than a
// reimplementation, so the answer is a property of the shipped code and not of a
// transcription of it.  The report claimed that feeding the mirrored ghost state
// to the Roe flux yields exactly [0, p nx, p ny, 0]; this measures how well that
// holds over a range of interior states and face normals.
//
// Built and run by report/check_slip_flux.sh.  It reads and writes nothing.

#include <cmath>
#include <cstdio>

#include "numerics/riemann_flux.h"
#include "physics/boundary_conditions.h"
#include "physics/perfect_gas.h"

using namespace cns2d;

namespace {

struct Case {
  Real rho, u, v, p, nx, ny;
  const char *label;
};

}  // namespace

int main() {
  GasProperties props;
  props.gamma = 1.4;
  props.R = 1.0 / 1.4;  // only ratios matter for this identity
  props.prandtl = 0.72;
  const PerfectGas gas(props);

  const Case cases[] = {
      {1.0, 0.3, 0.1, 1.0 / 1.4, 0.0, 1.0, "V=(0.3,0.1) n=+y"},
      {1.0, 0.9, -0.2, 1.0 / 1.4, 0.6, 0.8, "V=(0.9,-0.2) n=(.6,.8)"},
      {1.2, 0.5, 0.4, 1.9, 0.70710678118654752, 0.70710678118654752,
       "V=(0.5,0.4) n=(.707,.707)"},
      {1.0, 1.0, 0.0, 1.0 / 1.4, 0.0, 1.0, "V=(1,0) n=+y (tangential)"},
      {1.0, 0.0, 0.0, 1.0 / 1.4, 0.0, 1.0, "V=0 (stagnation)"},
  };

  std::printf("%-30s %12s %12s %12s %12s %10s\n", "state", "mass", "nrm-mom", "p", "energy",
              "u_n");
  for (const Case &c : cases) {
    PrimVec WL{};
    WL[kPrimRho] = c.rho;
    WL[kPrimU] = c.u;
    WL[kPrimV] = c.v;
    WL[kPrimP] = c.p;
    const Vec2 n{c.nx, c.ny};

    const ConsVec UL = gas.consFromPrim(WL);
    const ConsVec UR = slipWallState(UL, n);
    const PrimVec WR = gas.primFromCons(UR);

    Real max_wave = 0.0;
    const ConsVec H =
        riemannFlux(RiemannFluxType::kRoeEntropyFix, gas, WL, WR, n, 1.0, max_wave);
    const Real nmom = H[1] * c.nx + H[2] * c.ny;
    const Real un = c.u * c.nx + c.v * c.ny;
    std::printf("%-30s %12.3e %12.6f %12.6f %12.3e %10.3f\n", c.label, H[0], nmom, c.p, H[3], un);
  }
  return 0;
}
