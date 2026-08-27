#include <cstdio>
#include <cmath>
#include <random>
#include "solve/lusgs.h"
#include "numerics/riemann_flux.h"
#include "physics/perfect_gas.h"

using namespace cns2d;

// Central-difference the true Euler normal flux along direction dU and compare
// with the analytic matrix-free Jacobian product used by LU-SGS.
int main() {
  GasProperties gp; gp.gamma = 1.4; gp.R = 1.0; gp.prandtl = 0.72;
  PerfectGas gas(gp);

  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> Urho(0.2, 3.0);
  std::uniform_real_distribution<double> Uvel(-2.5, 2.5);
  std::uniform_real_distribution<double> Up(0.5, 90.0);
  std::uniform_real_distribution<double> Uang(0.0, 6.283185307179586);
  std::uniform_real_distribution<double> Udir(-1.0, 1.0);

  double worst_rel = 0.0;
  int worst_case = -1, worst_comp = -1;
  double worst_detail[8] = {0};

  const int N = 20000;
  for (int t = 0; t < N; ++t) {
    PrimVec W{Urho(rng), Uvel(rng), Uvel(rng), Up(rng)};
    ConsVec U = gas.consFromPrim(W);
    const double ang = Uang(rng);
    Vec2 n{std::cos(ang), std::sin(ang)};

    // Random admissible perturbation direction, scaled to the state magnitude.
    ConsVec dU{};
    double scale = 0.0;
    for (int k = 0; k < kNumVars; ++k) { dU[k] = Udir(rng); scale = std::max(scale, std::abs(U[k])); }
    for (int k = 0; k < kNumVars; ++k) dU[k] *= scale;

    const ConsVec analytic = fluxJacobianTimesVector(gas, U, n, dU);

    // Central difference: (F(U+h dU) - F(U-h dU)) / (2h), h chosen small but
    // large enough that cancellation stays well above double roundoff.
    const double h = 1.0e-7;
    ConsVec Up_{}, Um_{};
    for (int k = 0; k < kNumVars; ++k) { Up_[k] = U[k] + h * dU[k]; Um_[k] = U[k] - h * dU[k]; }
    // Skip if the perturbed state left the admissible region.
    if (Up_[kRho] <= 0 || Um_[kRho] <= 0) continue;
    if (gas.pressureFromCons(Up_) <= 0 || gas.pressureFromCons(Um_) <= 0) continue;

    const ConsVec Fp = eulerNormalFlux(gas, gas.primFromCons(Up_), n);
    const ConsVec Fm = eulerNormalFlux(gas, gas.primFromCons(Um_), n);

    double ref = 0.0;
    for (int k = 0; k < kNumVars; ++k) ref = std::max(ref, std::abs(analytic[k]));
    ref = std::max(ref, 1.0e-30);

    for (int k = 0; k < kNumVars; ++k) {
      const double fd = (Fp[k] - Fm[k]) / (2.0 * h);
      const double rel = std::abs(fd - analytic[k]) / ref;
      if (rel > worst_rel) {
        worst_rel = rel; worst_case = t; worst_comp = k;
        worst_detail[0] = fd; worst_detail[1] = analytic[k];
        worst_detail[2] = W[0]; worst_detail[3] = W[1];
        worst_detail[4] = W[2]; worst_detail[5] = W[3];
        worst_detail[6] = n.x; worst_detail[7] = n.y;
      }
    }
  }

  printf("fluxJacobianTimesVector vs central-difference of eulerNormalFlux\n");
  printf("  samples            : %d random admissible states/normals/directions\n", N);
  printf("  worst relative err : %.3e  (case %d, component %d)\n", worst_rel, worst_case, worst_comp);
  printf("  at that worst case : fd = % .12e   analytic = % .12e\n", worst_detail[0], worst_detail[1]);
  printf("  state              : rho=%.4g u=%.4g v=%.4g p=%.4g  n=(%.4f,%.4f)\n",
         worst_detail[2], worst_detail[3], worst_detail[4], worst_detail[5],
         worst_detail[6], worst_detail[7]);
  printf("  VERDICT            : %s\n",
         worst_rel < 1.0e-6 ? "JACOBIAN PRODUCT IS CORRECT (matches FD to central-difference accuracy)"
                            : "JACOBIAN PRODUCT IS WRONG");
  return 0;
}
