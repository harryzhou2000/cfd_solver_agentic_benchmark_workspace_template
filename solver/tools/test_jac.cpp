// Numeric verification of eulerNormalJacobian against finite differences.
#include "jacobian.hpp"
#include <cstdio>
#include <cstdlib>
using namespace fv;
int main() {
  Gas g; g.gamma = 1.4; g.R = 1.0; g.Pr = 0.72;
  std::srand(42);
  double maxErr = 0.0;
  for (int t = 0; t < 200; ++t) {
    Prim w;
    w.rho = 0.5 + 2.0 * std::rand() / RAND_MAX;
    w.u = -1.5 + 3.0 * std::rand() / RAND_MAX;
    w.v = -1.5 + 3.0 * std::rand() / RAND_MAX;
    w.p = 0.3 + 3.0 * std::rand() / RAND_MAX;
    double ang = 2 * M_PI * std::rand() / RAND_MAX;
    double nx = cos(ang), ny = sin(ang);
    double A[4][4];
    eulerNormalJacobian(w, nx, ny, g, A);
    State U = w.toCons(g), dU;
    for (int i = 0; i < 4; ++i) dU[i] = (2.0 * std::rand() / RAND_MAX - 1.0);
    double d = 1e-7;
    State Up, Um;
    for (int i = 0; i < 4; ++i) { Up[i] = U[i] + d * dU[i]; Um[i] = U[i] - d * dU[i]; }
    State Fp = physFluxN(Prim::fromCons(Up, g), nx, ny, g);
    State Fm = physFluxN(Prim::fromCons(Um, g), nx, ny, g);
    for (int i = 0; i < 4; ++i) {
      double fd = (Fp[i] - Fm[i]) / (2 * d);
      double an = 0.0;
      for (int j = 0; j < 4; ++j) an += A[i][j] * dU[j];
      maxErr = std::max(maxErr, std::fabs(fd - an) / std::max(1.0, std::fabs(an)));
    }
  }
  printf("max relative FD-vs-analytic Jacobian error: %.3e\n", maxErr);
  return maxErr < 1e-4 ? 0 : 1;
}
