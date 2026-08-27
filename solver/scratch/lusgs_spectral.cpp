// Standalone spectral-radius probe for the LU-SGS iteration used by cns2d.
//
// Reproduces EXACTLY the operator from src/solve/lusgs.cpp:
//   diagonal_[c] = V/dtau + 0.5 * sum_f |lambda_f| * area_f        (buildDiagonal)
//   O_ij dU_j    = 0.5 * area * ( A(U_j).n dU_j - lambda_j dU_j )  (offDiagonalAction)
// on a uniform periodic Cartesian torus, so there are no boundary faces and no
// mesh-quality effects: whatever we see is the intrinsic behaviour of the
// linear operator and of the symmetric Gauss-Seidel sweeps applied to it.
//
// With rhs = 0 the sweep sequence is a pure error-propagation operator, so the
// asymptotic growth factor of ||dU|| IS the spectral radius of the iteration.
// rho < 1 => more sweeps help.  rho >= 1 => NO number of sweeps can converge.
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "solve/lusgs.h"
#include "physics/perfect_gas.h"

using namespace cns2d;

static const int N = 20;            // N x N cells
static const double h = 0.05;       // uniform cell size

struct Probe {
  PerfectGas gas;
  PrimVec W{};
  ConsVec U{};
  double a{0.0};
  std::vector<Vec2> nrm{{1,0},{-1,0},{0,1},{0,-1}};

  int idx(int i, int j) const { return ((j + N) % N) * N + ((i + N) % N); }

  // conv_radius as accumulated in residual.cpp: sum over faces of the Roe
  // max wave speed times face area.  Uniform state => Roe average == cell state.
  double convRadius() const {
    double s = 0.0;
    for (const Vec2 &n : nrm) {
      const double un = W[kPrimU] * n.x + W[kPrimV] * n.y;
      s += (std::fabs(un) + a) * h;
    }
    return s;
  }

  // One symmetric Gauss-Seidel iteration (forward then backward) with rhs = 0,
  // matching ImplicitSolver::solve's sweep structure.
  void sweep(std::vector<ConsVec> &dU, double diag) const {
    const double inv_d = 1.0 / diag;
    for (int pass = 0; pass < 2; ++pass) {
      const int begin = (pass == 0) ? 0 : N * N - 1;
      const int end   = (pass == 0) ? N * N : -1;
      const int step  = (pass == 0) ? 1 : -1;
      for (int c = begin; c != end; c += step) {
        const int i = c % N, j = c / N;
        ConsVec acc{};  // rhs = 0
        for (int d = 0; d < 4; ++d) {
          const Vec2 n = nrm[d];
          const int ni = i + (n.x > 0 ? 1 : (n.x < 0 ? -1 : 0));
          const int nj = j + (n.y > 0 ? 1 : (n.y < 0 ? -1 : 0));
          const ConsVec &duj = dU[idx(ni, nj)];
          const ConsVec jac = fluxJacobianTimesVector(gas, U, n, duj);
          const double un = W[kPrimU] * n.x + W[kPrimV] * n.y;
          const double lambda = std::fabs(un) + a;
          for (int k = 0; k < kNumVars; ++k) {
            acc[k] -= 0.5 * h * (jac[k] - lambda * duj[k]);
          }
        }
        for (int k = 0; k < kNumVars; ++k) dU[c][k] = acc[k] * inv_d;
      }
    }
  }

  double spectralRadius(double cfl, double diag_beta) const {
    const double V = h * h;
    const double cr = convRadius();
    // dtau = cfl * V / conv_radius  (solve/local_time_step.cpp)
    const double inv_dtau_term = (cfl > 0.0) ? cr / cfl : 0.0;  // == V/dtau
    const double diag = inv_dtau_term + diag_beta * 0.5 * cr;

    std::mt19937 rng(7);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<ConsVec> dU(N * N);
    for (auto &d : dU) for (int k = 0; k < kNumVars; ++k) d[k] = dist(rng);

    auto nrm2 = [&](const std::vector<ConsVec> &v) {
      double s = 0.0;
      for (const auto &e : v) for (int k = 0; k < kNumVars; ++k) s += e[k] * e[k];
      return std::sqrt(s);
    };

    double rho = 0.0;
    for (int it = 0; it < 400; ++it) {
      const double before = nrm2(dU);
      if (before == 0.0 || !std::isfinite(before)) break;
      for (auto &d : dU) for (int k = 0; k < kNumVars; ++k) d[k] /= before;
      sweep(dU, diag);
      rho = nrm2(dU);
    }
    return rho;
  }
};

int main() {
  GasProperties gp; gp.gamma = 1.4; gp.R = 1.0; gp.prandtl = 0.72;
  Probe P;
  P.gas = PerfectGas(gp);
  // The dbg_cyl_inviscid freestream: M = 0.1, rho = 1, |u| = 1, p = 71.42857.
  P.W = {1.0, 1.0, 0.0, 71.42857142857142};
  P.U = P.gas.consFromPrim(P.W);
  P.a = P.gas.soundSpeed(P.W[kPrimRho], P.W[kPrimP]);

  printf("cns2d LU-SGS iteration spectral radius, uniform periodic mesh, M=%.2f\n",
         std::sqrt(P.W[kPrimU]*P.W[kPrimU]) / P.a);
  printf("(rho >= 1 means the symmetric Gauss-Seidel sweeps CANNOT converge,\n");
  printf(" no matter how many sweeps the driver adds)\n\n");
  printf("   CFL        rho(as-shipped)   rho(diag x2)   rho(diag x4)\n");
  const double cfls[] = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 50.0, 100.0, 1000.0, -1.0};
  for (double cfl : cfls) {
    const double r1 = P.spectralRadius(cfl, 1.0);
    const double r2 = P.spectralRadius(cfl, 2.0);
    const double r4 = P.spectralRadius(cfl, 4.0);
    if (cfl < 0) printf("   inf   ");
    else         printf("  %7.1f", cfl);
    printf("        %10.4f     %10.4f     %10.4f%s\n", r1, r2, r4,
           r1 >= 1.0 ? "   <-- DIVERGENT" : "");
  }
  return 0;
}
