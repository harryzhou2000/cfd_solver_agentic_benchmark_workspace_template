// Independent check of the slip-wall flux identity, calling the shipped code.
#include <cmath>
#include <cstdio>

#include "numerics/riemann_flux.h"
#include "physics/boundary_conditions.h"
#include "physics/perfect_gas.h"

using namespace cns2d;

int main() {
  GasProperties props;
  props.gamma = 1.4;
  props.R = 1.0;
  const PerfectGas gas(props);
  const Real p = 1.0 / (1.4 * 0.15 * 0.15) * 0.15 * 0.15 * 1.4;  // = 1.0

  struct Case { Real u, v, nx, ny; };
  const Case cases[] = {
      {0.3, 0.1, 0.0, 1.0},
      {0.9, -0.2, 0.6, 0.8},
      {0.5, 0.4, 0.7071067811865476, 0.7071067811865476},
      {1.0, 0.0, 0.0, 1.0},
      {0.0, 0.0, 0.0, 1.0},
      {1e-13, 0.0, 0.0, 1.0},
  };
  std::printf("%-26s %12s %14s %12s %10s\n", "state", "mass", "norm-mom-p", "energy", "u_n");
  for (const Case &c : cases) {
    const Vec2 n{c.nx, c.ny};
    const PrimVec Wi{1.0, c.u, c.v, p};
    const ConsVec Ui = gas.consFromPrim(Wi);
    const ConsVec Ub = slipWallState(Ui, n);
    const PrimVec Wb = gas.primFromCons(Ub);
    Real smax = 0.0;
    const ConsVec f = riemannFlux(RiemannFluxType::kRoeEntropyFix, gas, Wi, Wb, n, 1.0, smax);
    const Real un = c.u * c.nx + c.v * c.ny;
    const Real norm_mom = f[kRhoU] * c.nx + f[kRhoV] * c.ny;
    std::printf("V=(%.2g,%.2g) n=(%.3g,%.3g) %12.3e %14.6f %12.3e %10.3g\n", c.u, c.v, c.nx, c.ny,
                f[kRho], norm_mom - p, f[kRhoE], un);
  }
  return 0;
}
