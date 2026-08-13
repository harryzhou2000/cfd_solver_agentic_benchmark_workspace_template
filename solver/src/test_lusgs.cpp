#include <cmath>
#include <cstdio>
#include <vector>

#include "common.hpp"

using namespace cfd;

static MatN jacobian_n(const Primitive& q, const Vec2& n, const GasModel& gas) {
    const double g = gas.gamma;
    const double u = q.u;
    const double v = q.v;
    const double rho = std::max(q.rho, 1e-300);
    const double H = q.p / ((g - 1.0) * rho) + q.p / rho +
                     0.5 * (u * u + v * v);
    const double ke = 0.5 * (u * u + v * v);
    MatN Ax, Ay;
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
    const int ncells = 4;
    const Primitive q{1.0, 1.0, 0.0, 71.42857142857142};
    const Vec2 n{1.0, 0.0};
    const double area = 1.0;
    const double vol = 1.0;
    const double cfl = 1e6;

    // lambda_c per cell = 2 * area * (|u|+a) (two faces per cell).
    const double a = std::sqrt(gas.gamma * q.p / q.rho);
    const double lam = 2.0 * area * (std::fabs(q.u) + a);
    const double dtau_inv = lam / (cfl * vol);

    const MatN A = jacobian_n(q, n, gas);
    const double rho = std::fabs(q.u) + a;

    // Build the flux-split Jacobian M = D + L + U.
    // Interior faces: (0-1), (1-2), (2-3).
    std::vector<MatN> D(ncells);
    std::vector<MatN> M(ncells * ncells);
    for (int c = 0; c < ncells; ++c) {
        // Classical LU-SGS diagonal: V/dt + 0.5 * sum_faces(rho_j) (two
        // interior faces for each of the 4 cells here).
        for (int i = 0; i < kNC; ++i)
            D[c][i][i] = dtau_inv * vol + rho;
        M[c * ncells + c] = D[c];
    }
    for (int c = 0; c < ncells - 1; ++c) {
        for (int i = 0; i < kNC; ++i)
            for (int j = 0; j < kNC; ++j) {
                // A^+ = 0.5(A + rho I), A^- = 0.5(A - rho I)
                const double Am = 0.5 * (A[i][j] - (i == j ? rho : 0.0));
                M[c * ncells + c + 1][i][j] += Am;   // dR_c/dU_{c+1}
                M[(c + 1) * ncells + c][i][j] +=      // dR_{c+1}/dU_c
                    0.5 * (-A[i][j] - (i == j ? rho : 0.0));
            }
    }

    // RHS (random-ish but fixed).
    std::vector<double> R(ncells * kNC);
    for (int c = 0; c < ncells; ++c)
        for (int i = 0; i < kNC; ++i)
            R[c * kNC + i] = 0.1 * (c + 1) * (i + 1) - 0.3;

    // Exact solve of M x = -R via Gauss elimination on the full system.
    const int N = ncells * kNC;
    std::vector<double> Ms(N * N, 0.0), rhs(N, 0.0);
    for (int c = 0; c < ncells; ++c)
        for (int d = 0; d < ncells; ++d)
            for (int i = 0; i < kNC; ++i)
                for (int j = 0; j < kNC; ++j)
                    Ms[(c * kNC + i) * N + (d * kNC + j)] =
                        M[c * ncells + d][i][j];
    for (int i = 0; i < N; ++i) rhs[i] = -R[i];
    std::vector<double> x(N);
    for (int col = 0; col < N; ++col) {
        int piv = col;
        for (int r = col + 1; r < N; ++r)
            if (std::fabs(Ms[r * N + col]) > std::fabs(Ms[piv * N + col]))
                piv = r;
        for (int j = 0; j < N; ++j)
            std::swap(Ms[col * N + j], Ms[piv * N + j]);
        std::swap(rhs[col], rhs[piv]);
        for (int r = col + 1; r < N; ++r) {
            const double f = Ms[r * N + col] / Ms[col * N + col];
            for (int j = col; j < N; ++j) Ms[r * N + j] -= f * Ms[col * N + j];
            rhs[r] -= f * rhs[col];
        }
    }
    for (int r = N - 1; r >= 0; --r) {
        double s = rhs[r];
        for (int j = r + 1; j < N; ++j) s -= Ms[r * N + j] * x[j];
        x[r] = s / Ms[r * N + r];
    }

    // One LU-SGS factorization step with A^- in both sweeps.
    std::vector<double> dU(N, 0.0);
    std::vector<double> work(N, 0.0);
    for (int c = 0; c < ncells; ++c) {
        VecN rhs_c;
        for (int i = 0; i < kNC; ++i) rhs_c[i] = -R[c * kNC + i];
        if (c > 0) {
            const double sgn = 1.0;  // outward from c to c-1 is -n
            VecN contrib;
            for (int i = 0; i < kNC; ++i) {
                double s = 0.0;
                for (int k = 0; k < kNC; ++k)
                    s += (sgn * -A[i][k] - (i == k ? rho : 0.0)) *
                         dU[(c - 1) * kNC + k];
                contrib[i] = 0.5 * s;
            }
            rhs_c = rhs_c - contrib;
        }
        const VecN dc = D[c].solve(rhs_c);
        for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = dc[i];
    }
    for (int c = ncells - 1; c >= 0; --c) {
        VecN corr;
        if (c < ncells - 1) {
            const double sgn = 1.0;  // outward from c to c+1 is +n
            VecN contrib;
            for (int i = 0; i < kNC; ++i) {
                double s = 0.0;
                for (int k = 0; k < kNC; ++k)
                    s += (sgn * A[i][k] + (i == k ? rho : 0.0)) *
                         dU[(c + 1) * kNC + k];
                contrib[i] = 0.5 * s;
            }
            corr = corr + contrib;
        }
        VecN dc;
        for (int i = 0; i < kNC; ++i) dc[i] = dU[c * kNC + i];
        const VecN adj = D[c].solve(corr);
        dc = dc - adj;
        for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = dc[i];
    }
    double rel = 0.0, nx = 0.0;
    for (int i = 0; i < N; ++i) {
        rel += (dU[i] - x[i]) * (dU[i] - x[i]);
        nx += x[i] * x[i];
    }
    printf("one-step LU-SGS relative error = %.4e\n", std::sqrt(rel / nx));

    // Iterative behavior: solve M x = -R by repeated LU-SGS updates.
    std::vector<double> xi(N, 0.0);
    for (int it = 0; it < 200; ++it) {
        std::vector<double> resid = R;
        for (int c = 0; c < ncells; ++c)
            for (int d = 0; d < ncells; ++d)
                for (int i = 0; i < kNC; ++i)
                    for (int j = 0; j < kNC; ++j)
                        resid[c * kNC + i] +=
                            M[c * ncells + d][i][j] * xi[d * kNC + j];
        std::vector<double> dd(N, 0.0);
        for (int c = 0; c < ncells; ++c) {
            VecN rhs_c;
            for (int i = 0; i < kNC; ++i) rhs_c[i] = -resid[c * kNC + i];
            if (c > 0) {
                VecN contrib;
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k)
                        s += (-A[i][k] - (i == k ? rho : 0.0)) *
                             dd[(c - 1) * kNC + k];
                    contrib[i] = 0.5 * s;
                }
                rhs_c = rhs_c - contrib;
            }
            const VecN dc = D[c].solve(rhs_c);
            for (int i = 0; i < kNC; ++i) dd[c * kNC + i] = dc[i];
        }
        for (int c = ncells - 1; c >= 0; --c) {
            VecN corr;
            if (c < ncells - 1) {
                VecN contrib;
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k)
                        s += (A[i][k] + (i == k ? rho : 0.0)) *
                             dd[(c + 1) * kNC + k];
                    contrib[i] = 0.5 * s;
                }
                corr = corr + contrib;
            }
            VecN dc;
            for (int i = 0; i < kNC; ++i) dc[i] = dd[c * kNC + i];
            const VecN adj = D[c].solve(corr);
            dc = dc - adj;
            for (int i = 0; i < kNC; ++i) dd[c * kNC + i] = dc[i];
        }
        for (int i = 0; i < N; ++i) xi[i] += dd[i];
        double rn = 0.0;
        for (double v : resid) rn += v * v;
        if (it < 10 || it % 20 == 0)
            printf("  iter %d residual norm %.4e\n", it, std::sqrt(rn));
        if (std::sqrt(rn) > 1e6) {
            printf("DIVERGED at iter %d\n", it);
            return 1;
        }
    }
    printf("--- nonlinear Rusanov iteration ---\n");
    {
        // 4-cell chain, uniform base state plus a perturbation in cell 3.
        std::vector<VecN> U(ncells);
        const VecN ub = prim_to_cons(q, gas);
        for (int c = 0; c < ncells; ++c) U[c] = ub;
        U[3][0] *= 1.01;
        U[3][3] *= 1.01;
        for (int it = 0; it < 300; ++it) {
            // Residual: faces (0-1),(1-2),(2-3), no boundary faces.
            std::vector<VecN> R(ncells);
            for (int c = 0; c < ncells - 1; ++c) {
                const Primitive qL = cons_to_prim(U[c], gas);
                const Primitive qR = cons_to_prim(U[c + 1], gas);
                const double aL = sound_speed(qL, gas);
                const double aR = sound_speed(qR, gas);
                const double lam =
                    std::max(std::fabs(qL.u * n.x + qL.v * n.y) + aL,
                             std::fabs(qR.u * n.x + qR.v * n.y) + aR);
                const VecN FL = inviscid_flux_normal(qL, n, gas);
                const VecN FR = inviscid_flux_normal(qR, n, gas);
                VecN f;
                for (int i = 0; i < kNC; ++i)
                    f[i] = 0.5 * (FL[i] + FR[i]) -
                           0.5 * lam * (U[c + 1][i] - U[c][i]);
                for (int i = 0; i < kNC; ++i) {
                    R[c][i] += f[i] * area;
                    R[c + 1][i] -= f[i] * area;
                }
            }
            // Classical LU-SGS sweeps with per-cell diagonal.
            std::vector<double> D(ncells), dU(ncells * kNC, 0.0);
            for (int c = 0; c < ncells; ++c) {
                const Primitive qc = cons_to_prim(U[c], gas);
                const double a = sound_speed(qc, gas);
                const double lam = 2.0 * area * (std::fabs(qc.u) + a);
                const double dtau_inv = lam / (16.0 * vol);
                double rho_sum = 0.0;
                if (c > 0)
                    rho_sum += std::fabs(qc.u * (-n.x) + qc.v * (-n.y)) + a;
                if (c < ncells - 1)
                    rho_sum += std::fabs(qc.u * n.x + qc.v * n.y) + a;
                D[c] = dtau_inv * vol + rho_sum;  // variant: full sum rho
            }
            for (int c = 0; c < ncells; ++c) {
                VecN rhs;
                for (int i = 0; i < kNC; ++i) rhs[i] = -R[c][i];
                if (c > 0) {
                    const Primitive qc = cons_to_prim(U[c], gas);
                    const Primitive qn = cons_to_prim(U[c - 1], gas);
                    Primitive qa;
                    qa.rho = 0.5 * (qc.rho + qn.rho);
                    qa.u = 0.5 * (qc.u + qn.u);
                    qa.v = 0.5 * (qc.v + qn.v);
                    qa.p = 0.5 * (qc.p + qn.p);
                    const double a = sound_speed(qa, gas);
                    const double rho =
                        std::fabs(qa.u * n.x + qa.v * n.y) + a;
                    const MatN A = jacobian_n(qa, Vec2{-n.x, -n.y}, gas);
                    for (int i = 0; i < kNC; ++i) {
                        double s = 0.0;
                        for (int k = 0; k < kNC; ++k)
                            s += (A[i][k] - (i == k ? rho : 0.0)) *
                                 dU[(c - 1) * kNC + k];
                        rhs[i] -= 0.5 * s;
                    }
                }
                for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = rhs[i] / D[c];
            }
            for (int c = ncells - 1; c >= 0; --c) {
                if (c < ncells - 1) {
                    const Primitive qc = cons_to_prim(U[c], gas);
                    const Primitive qn = cons_to_prim(U[c + 1], gas);
                    Primitive qa;
                    qa.rho = 0.5 * (qc.rho + qn.rho);
                    qa.u = 0.5 * (qc.u + qn.u);
                    qa.v = 0.5 * (qc.v + qn.v);
                    qa.p = 0.5 * (qc.p + qn.p);
                    const double a = sound_speed(qa, gas);
                    const double rho =
                        std::fabs(qa.u * n.x + qa.v * n.y) + a;
                    const MatN A = jacobian_n(qa, n, gas);
                    for (int i = 0; i < kNC; ++i) {
                        double s = 0.0;
                        for (int k = 0; k < kNC; ++k)
                            s += (A[i][k] + (i == k ? rho : 0.0)) *
                                 dU[(c + 1) * kNC + k];
                        dU[c * kNC + i] -= 0.5 * s / D[c];
                    }
                }
            }
            double rn = 0.0;
            for (int c = 0; c < ncells; ++c) {
                for (int i = 0; i < kNC; ++i) {
                    U[c][i] += dU[c * kNC + i];
                    rn += R[c][i] * R[c][i];
                }
            }
            if (it % 20 == 0)
                printf("  nonlinear iter %d residual %.4e maxU %.4e\n", it,
                       std::sqrt(rn),
                       std::max({std::fabs(U[0][0]), std::fabs(U[3][3])}));
            if (!std::isfinite(std::sqrt(rn)) || std::sqrt(rn) > 1e6) {
                printf("NONLINEAR DIVERGED at iter %d\n", it);
                return 1;
            }
        }
        printf("nonlinear stable over 300 iterations\n");
    }
    return 0;
}
