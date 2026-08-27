#include "jacobian.hpp"

namespace fv {

void eulerNormalJacobian(const Prim& w, double nx, double ny, const Gas& g, double A[4][4]) {
  double rho = w.rho, u = w.u, v = w.v, p = w.p;
  double gm1 = g.gamma - 1.0;
  double q2 = u * u + v * v;
  double un = u * nx + v * ny;
  double theta = 0.5 * gm1 * q2;
  double E = p / (gm1 * rho) + 0.5 * q2;  // specific total energy
  double W = rho * E + p;                 // rhoE + p
  double H = W / rho;                     // total enthalpy
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) A[i][j] = 0.0;
  // row 0: rho*un = m nx + n ny
  A[0][0] = 0.0; A[0][1] = nx; A[0][2] = ny; A[0][3] = 0.0;
  // row 1: m un + p nx
  A[1][0] = -u * un + nx * theta;
  A[1][1] = un + u * nx - nx * gm1 * u;
  A[1][2] = u * ny - nx * gm1 * v;
  A[1][3] = nx * gm1;
  // row 2: n un + p ny
  A[2][0] = -v * un + ny * theta;
  A[2][1] = v * nx - ny * gm1 * u;
  A[2][2] = un + v * ny - ny * gm1 * v;
  A[2][3] = ny * gm1;
  // row 3: un * W
  A[3][0] = un * (theta - H);
  A[3][1] = nx * H - gm1 * u * un;
  A[3][2] = ny * H - gm1 * v * un;
  A[3][3] = g.gamma * un;
}

void fluxSplitMatvec(const Prim& w, double nx, double ny, const Gas& g, double lambda,
                     int sign, const State& x, State& y) {
  double A[4][4];
  eulerNormalJacobian(w, nx, ny, g, A);
  for (int i = 0; i < 4; ++i) {
    double s = 0.0;
    for (int j = 0; j < 4; ++j) s += A[i][j] * x[j];
    y[i] = 0.5 * (s + sign * lambda * x[i]);
  }
}

}  // namespace fv
