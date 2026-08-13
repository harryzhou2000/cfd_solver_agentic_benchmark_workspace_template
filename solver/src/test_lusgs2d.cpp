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

struct Face {
    int L, R;
    Vec2 n;  // outward from L
    double area;
};

int main(int argc, char** argv) {
    const double cfl = argc > 1 ? std::atof(argv[1]) : 16.0;
    const int variant = argc > 2 ? std::atoi(argv[2]) : 0;  // sweep choice
    const int diag_mode = argc > 3 ? std::atoi(argv[3]) : 0;  // diagonal
    const double relax = argc > 4 ? std::atof(argv[4]) : 1.0;
    GasModel gas{1.4, 1.0, 0.72};
    const Primitive q0{1.0, 1.0, 0.0, 31.746031746031743};
    const double vol = 1.0;

    // 2x2 square: cells 0..3, four interior faces.
    std::vector<Face> faces = {
        {0, 1, {1.0, 0.0}, 1.0},
        {0, 2, {0.0, 1.0}, 1.0},
        {1, 3, {0.0, 1.0}, 1.0},
        {2, 3, {1.0, 0.0}, 1.0},
    };
    // Optional slip-wall faces on the outer boundary (pressure-only flux).
    std::vector<std::pair<int, Vec2>> wall_faces = {
        {0, {-1.0, 0.0}},
        {1, {1.0, 0.0}},
        {2, {-1.0, 0.0}},
        {3, {1.0, 0.0}},
    };
    std::vector<std::vector<int>> cf(4);
    for (int f = 0; f < 4; ++f) {
        cf[faces[f].L].push_back(f);
        cf[faces[f].R].push_back(f);
    }
    std::vector<VecN> U(4);
    const VecN ub = prim_to_cons(q0, gas);
    for (int c = 0; c < 4; ++c) U[c] = ub;
    U[3][0] *= 1.01;
    U[3][3] *= 1.01;

    for (int it = 0; it < 500; ++it) {
        std::vector<VecN> R(4);
        for (const auto& f : faces) {
            const Primitive qL = cons_to_prim(U[f.L], gas);
            const Primitive qR = cons_to_prim(U[f.R], gas);
            const double aL = sound_speed(qL, gas);
            const double aR = sound_speed(qR, gas);
            const double lam =
                std::max(std::fabs(qL.u * f.n.x + qL.v * f.n.y) + aL,
                         std::fabs(qR.u * f.n.x + qR.v * f.n.y) + aR);
            const VecN FL = inviscid_flux_normal(qL, f.n, gas);
            const VecN FR = inviscid_flux_normal(qR, f.n, gas);
            VecN flux;
            for (int i = 0; i < kNC; ++i)
                flux[i] = 0.5 * (FL[i] + FR[i]) -
                          0.5 * lam * (U[f.R][i] - U[f.L][i]);
            for (int i = 0; i < kNC; ++i) {
                R[f.L][i] += flux[i] * f.area;
                R[f.R][i] -= flux[i] * f.area;
            }
        }
        // Wall contributions: p * n_out (outward from the cell).
        for (const auto& [c, nw] : wall_faces) {
            const Primitive qc = cons_to_prim(U[c], gas);
            R[c][1] += qc.p * nw.x;
            R[c][2] += qc.p * nw.y;
        }
        std::vector<double> D(4), dU(16, 0.0);
        for (int c = 0; c < 4; ++c) {
            const Primitive qc = cons_to_prim(U[c], gas);
            const double a = sound_speed(qc, gas);
            double lam = 0.0, rho_sum = 0.0;
            for (int f : cf[c]) {
                const Vec2 n_out = (faces[f].L == c) ? faces[f].n
                                                     : Vec2{-faces[f].n.x,
                                                             -faces[f].n.y};
                lam += faces[f].area * (std::fabs(qc.u * n_out.x +
                                                  qc.v * n_out.y) +
                                        a);
                rho_sum += std::fabs(qc.u * n_out.x + qc.v * n_out.y) + a;
            }
            // Add wall-face spectral damping to the diagonal.
            for (const auto& [wc, nw] : wall_faces) {
                if (wc != c) continue;
                const double a = sound_speed(qc, gas);
                rho_sum += std::fabs(qc.u * nw.x + qc.v * nw.y) + a;
            }
            const double dtau_inv = lam / (cfl * vol);
            D[c] = dtau_inv * vol;
            if (diag_mode == 0)
                D[c] += 0.5 * rho_sum;
            else if (diag_mode == 1)
                D[c] += rho_sum;
            else if (diag_mode == 2) {
                for (int f : cf[c]) {
                    const Primitive qc2 = cons_to_prim(U[c], gas);
                    const Primitive qn =
                        cons_to_prim(U[(faces[f].L == c) ? faces[f].R
                                                        : faces[f].L],
                                     gas);
                    Primitive qa;
                    qa.rho = 0.5 * (qc2.rho + qn.rho);
                    qa.u = 0.5 * (qc2.u + qn.u);
                    qa.v = 0.5 * (qc2.v + qn.v);
                    qa.p = 0.5 * (qc2.p + qn.p);
                    const double a = sound_speed(qa, gas);
                    const Vec2 n_out =
                        (faces[f].L == c)
                            ? faces[f].n
                            : Vec2{-faces[f].n.x, -faces[f].n.y};
                    const double rho =
                        std::fabs(qa.u * n_out.x + qa.v * n_out.y) + a;
                    const MatN A = jacobian_n(qa, n_out, gas);
                    D[c] += 0.5 * (A[0][0] + rho);  // diagonal entry 0,0
                }
            }
        }
        // Forward sweep (A^- for lower neighbors).
        for (int c = 0; c < 4; ++c) {
            VecN rhs;
            for (int i = 0; i < kNC; ++i) rhs[i] = -R[c][i];
            for (int f : cf[c]) {
                const int j = (faces[f].L == c) ? faces[f].R : faces[f].L;
                if (j < 0 || j >= c) continue;
                const Primitive qc = cons_to_prim(U[c], gas);
                const Primitive qn = cons_to_prim(U[j], gas);
                Primitive qa;
                qa.rho = 0.5 * (qc.rho + qn.rho);
                qa.u = 0.5 * (qc.u + qn.u);
                qa.v = 0.5 * (qc.v + qn.v);
                qa.p = 0.5 * (qc.p + qn.p);
                const double a = sound_speed(qa, gas);
                const Vec2 n_out = (faces[f].L == c) ? faces[f].n
                                                     : Vec2{-faces[f].n.x,
                                                             -faces[f].n.y};
                const double rho =
                    std::fabs(qa.u * n_out.x + qa.v * n_out.y) + a;
                const MatN A = jacobian_n(qa, n_out, gas);
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k)
                        s += (A[i][k] - (i == k ? rho : 0.0)) * dU[j * kNC + k];
                    rhs[i] -= 0.5 * s;
                }
            }
            for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = rhs[i] / D[c];
        }
        // Backward sweep.
        for (int c = 3; c >= 0; --c) {
            VecN corr;
            for (int f : cf[c]) {
                const int j = (faces[f].L == c) ? faces[f].R : faces[f].L;
                if (j < 0 || j <= c) continue;
                const Primitive qc = cons_to_prim(U[c], gas);
                const Primitive qn = cons_to_prim(U[j], gas);
                Primitive qa;
                qa.rho = 0.5 * (qc.rho + qn.rho);
                qa.u = 0.5 * (qc.u + qn.u);
                qa.v = 0.5 * (qc.v + qn.v);
                qa.p = 0.5 * (qc.p + qn.p);
                const double a = sound_speed(qa, gas);
                const Vec2 n_out = (faces[f].L == c) ? faces[f].n
                                                     : Vec2{-faces[f].n.x,
                                                             -faces[f].n.y};
                const double rho =
                    std::fabs(qa.u * n_out.x + qa.v * n_out.y) + a;
                const MatN A = jacobian_n(qa, n_out, gas);
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k) {
                        if (variant == 0)
                            s += (A[i][k] + (i == k ? rho : 0.0)) *
                                 dU[j * kNC + k];
                        else
                            s += (A[i][k] - (i == k ? rho : 0.0)) *
                                 dU[j * kNC + k];
                    }
                    corr[i] += 0.5 * s;
                }
            }
            for (int i = 0; i < kNC; ++i) dU[c * kNC + i] -= corr[i] / D[c];
        }
        double rn = 0.0;
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < kNC; ++i) {
                U[c][i] += relax * dU[c * kNC + i];
                rn += R[c][i] * R[c][i];
            }
        if (it % 50 == 0)
            printf("  iter %d residual %.4e maxU %.4e\n", it, std::sqrt(rn),
                   std::max({std::fabs(U[0][0]), std::fabs(U[3][3])}));
        if (!std::isfinite(std::sqrt(rn)) || std::sqrt(rn) > 1e6) {
            printf("DIVERGED at iter %d\n", it);
            return 1;
        }
    }
    printf("stable: cfl=%.1f variant=%d diag=%d relax=%.2f\n", cfl, variant,
           diag_mode, relax);
    return 0;
}
