#pragma once
// Analytic Jacobian of the inviscid normal flux F_n = nx*F + ny*G with respect
// to the conservative state, plus the split operators A+- = 0.5(A +- lambda I).
#include "flux.hpp"

namespace fv {

// 4x4 normal-flux Jacobian at a primitive state. Row-major A[4][4].
void eulerNormalJacobian(const Prim& w, double nx, double ny, const Gas& g, double A[4][4]);

// y = 0.5*(A_j +/- lambda*I) * x  (the LU-SGS off-diagonal split operators)
void fluxSplitMatvec(const Prim& w, double nx, double ny, const Gas& g, double lambda,
                     int sign, const State& x, State& y);

}  // namespace fv
