// Diagonal-dominance quantification for the cns2d LU-SGS operator.
//
// Gauss-Seidel on  D dU_i + sum_j O_ij dU_j = rhs  converges only if D is
// sufficiently dominant over the off-diagonal blocks.  Here we measure, for a
// uniform mesh, the induced-2-norm dominance ratio
//     S = ( sum_j ||O_ij|| ) / ||D||
// as a function of CFL.  S < 1 is the classical sufficient condition; S > 1
// admits divergence, and the measured spectral radius tracks it.
#include <cstdio>
#include <cmath>
#include <vector>
#include "solve/lusgs.h"
#include "physics/perfect_gas.h"

using namespace cns2d;

// Induced 2-norm of the matrix-free operator dU -> 0.5*area*(A.n dU - lambda dU),
// obtained by explicitly forming the 4x4 block via unit vectors and doing a few
// power iterations on B^T B.
static double blockNorm(const PerfectGas &gas, const ConsVec &U, Vec2 n, double lambda,
                        double area) {
  double B[4][4];
  for (int j = 0; j < 4; ++j) {
    ConsVec e{}; e[j] = 1.0;
    const ConsVec jac = fluxJacobianTimesVector(gas, U, n, e);
    for (int i = 0; i < 4; ++i) {
      B[i][j] = 0.5 * area * (jac[i] - lambda * e[i]);
    }
  }
  double v[4] = {1, 1, 1, 1};
  double s = 0.0;
  for (int it = 0; it < 500; ++it) {
    double Bv[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) Bv[i] += B[i][j] * v[j];
    double BtBv[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) BtBv[j] += B[i][j] * Bv[i];
    double nn = 0.0;
    for (int j = 0; j < 4; ++j) nn += BtBv[j] * BtBv[j];
    nn = std::sqrt(nn);
    if (nn == 0.0) return 0.0;
    for (int j = 0; j < 4; ++j) v[j] = BtBv[j] / nn;
    s = nn;
  }
  return std::sqrt(s);
}

int main() {
  GasProperties gp; gp.gamma = 1.4; gp.R = 1.0; gp.prandtl = 0.72;
  PerfectGas gas(gp);
  PrimVec W{1.0, 1.0, 0.0, 71.42857142857142};
  ConsVec U = gas.consFromPrim(W);
  const double a = gas.soundSpeed(W[kPrimRho], W[kPrimP]);
  const double h = 0.05;
  std::vector<Vec2> nrm{{1,0},{-1,0},{0,1},{0,-1}};

  double conv_radius = 0.0;
  double offsum = 0.0;
  for (const Vec2 &n : nrm) {
    const double un = W[kPrimU] * n.x + W[kPrimV] * n.y;
    const double lambda = std::fabs(un) + a;
    conv_radius += lambda * h;
    offsum += blockNorm(gas, U, n, lambda, h);
  }

  printf("Uniform mesh, M=0.1, h=%.3f.  sum_j ||O_ij|| = %.6g\n", h, offsum);
  printf("Diagonal as shipped: D = V/dtau + 0.5*conv_radius, conv_radius = %.6g\n\n", conv_radius);
  printf("    CFL       V/dtau        D(shipped)   dominance S=sum||O||/D\n");
  const double cfls[] = {0.5, 1.0, 5.0, 10.0, 30.0, 100.0, -1.0};
  for (double cfl : cfls) {
    const double V = h * h;
    const double vdt = (cfl > 0.0) ? conv_radius / cfl : 0.0;   // V/dtau
    const double D = vdt + 0.5 * conv_radius;
    if (cfl < 0) printf("    inf  "); else printf("  %6.1f", cfl);
    printf("   %11.5g   %11.5g   %8.4f%s\n", vdt, D, offsum / D,
           offsum / D > 1.0 ? "   <-- NOT DIAGONALLY DOMINANT" : "");
  }
  printf("\nAsymptotic (CFL -> inf) dominance with an inflated dissipation diagonal\n");
  printf("D = V/dtau + beta*0.5*conv_radius:\n");
  for (double beta : {1.0, 1.5, 2.0, 3.0, 4.0}) {
    const double D = beta * 0.5 * conv_radius;
    printf("   beta = %.1f  ->  S = %6.4f%s\n", beta, offsum / D,
           offsum / D < 1.0 ? "   (dominant: GS converges at ANY CFL)" : "");
  }
  return 0;
}
