#include <cmath>
#include <cstdio>

#include "common.hpp"

using namespace cfd;

// Local copy of the Jacobian implementation to test against finite
// differences of the conservative flux (kept in sync with solver.cpp).
static MatN jacobian_n(const Primitive& q, const Vec2& n, const GasModel& gas) {
    const double g = gas.gamma;
    const double u = q.u;
    const double v = q.v;
    const double rho = std::max(q.rho, 1e-300);
    const double H = q.p / ((g - 1.0) * rho) + q.p / rho +
                     0.5 * (u * u + v * v);
    const double ke = 0.5 * (u * u + v * v);
    MatN Ax;
    Ax[0][1] = 1.0;
    Ax[1][0] = 0.5 * (g - 3.0) * u * u + 0.5 * (g - 1.0) * v * v;
    Ax[1][1] = (3.0 - g) * u;
    Ax[1][2] = -(g - 1.0) * v;
    Ax[1][3] = g - 1.0;
    Ax[2][0] = -u * v;
    Ax[2][1] = v;
    Ax[2][2] = u;
    Ax[3][0] = u * ((g - 1.0) * ke - H);
    Ax[3][1] = H - (g - 1.0) * u * u;
    Ax[3][2] = -(g - 1.0) * u * v;
    Ax[3][3] = g * u;
    MatN Ay;
    Ay[0][2] = 1.0;
    Ay[1][0] = -u * v;
    Ay[1][1] = v;
    Ay[1][2] = u;
    Ay[2][0] = 0.5 * (g - 1.0) * u * u + 0.5 * (g - 3.0) * v * v;
    Ay[2][1] = -(g - 1.0) * u;
    Ay[2][2] = (3.0 - g) * v;
    Ay[2][3] = g - 1.0;
    Ay[3][0] = v * ((g - 1.0) * ke - H);
    Ay[3][1] = -(g - 1.0) * u * v;
    Ay[3][2] = H - (g - 1.0) * v * v;
    Ay[3][3] = g * v;
    MatN A;
    for (int i = 0; i < kNC; ++i)
        for (int j = 0; j < kNC; ++j)
            A[i][j] = Ax[i][j] * n.x + Ay[i][j] * n.y;
    return A;
}

int main() {
    GasModel gas{1.4, 1.0, 0.72};
    const Primitive q0{1.3, 0.8, -0.2, 1.1};
    const Vec2 n{0.7, -0.714142842854};
    const MatN A = jacobian_n(q0, n, gas);
    const VecN u0 = prim_to_cons(q0, gas);
    const double eps = 1e-6;
    double maxerr = 0.0;
    for (int j = 0; j < kNC; ++j) {
        VecN up = u0, um = u0;
        up[j] += eps;
        um[j] -= eps;
        const Primitive qp = cons_to_prim(up, gas);
        const Primitive qm = cons_to_prim(um, gas);
        const VecN fp = inviscid_flux_normal(qp, n, gas);
        const VecN fm = inviscid_flux_normal(qm, n, gas);
        for (int i = 0; i < kNC; ++i) {
            const double fd = (fp[i] - fm[i]) / (2.0 * eps);
            const double err = std::fabs(fd - A[i][j]) /
                               std::max(1.0, std::fabs(fd));
            maxerr = std::max(maxerr, err);
            if (err > 1e-6) {
                printf("A[%d][%d] fd=%.12g analytic=%.12g\n", i, j, fd,
                       A[i][j]);
            }
        }
    }
    printf("max relative error = %.3e\n", maxerr);
    return maxerr > 1e-5 ? 1 : 0;
}
