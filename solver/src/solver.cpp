#include "solver.hpp"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace fs = std::filesystem;

namespace cfd {

namespace {

// 2-D inviscid flux Jacobian in the face normal direction:
// A_n = d(F . n)/dU. Implemented from the primitive conservative variables.
MatN inviscid_jacobian_normal(const Primitive& q, const Vec2& n,
                              const GasModel& gas) {
    const double g = gas.gamma;
    const double u = q.u;
    const double v = q.v;
    const double rho = std::max(q.rho, 1e-300);
    const double H = q.p / ((g - 1.0) * rho) + q.p / rho +
                     0.5 * (u * u + v * v);
    const double un = u * n.x + v * n.y;
    const double ke = 0.5 * (u * u + v * v);

    // dF_x/dU
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

    // dF_y/dU
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
    (void)un;
    return A;
}

std::string iso_time() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

Solver::Solver(CaseInput case_input, DistributedMesh mesh, MPI_Comm comm)
    : case_(std::move(case_input)),
      mesh_(std::move(mesh)),
      comm_(comm) {
    MPI_Comm_rank(comm_, &rank_);
    MPI_Comm_size(comm_, &nranks_);

    numerics_.gas = case_.gas;
    numerics_.flux_scheme = case_.flux_scheme;
    if (std::getenv("CFD_RUSANOV")) numerics_.flux_scheme = FluxScheme::Rusanov;
    numerics_.rusanov_dissipation_scale =
        case_.run_control.rusanov_dissipation_scale;
    numerics_.viscosity = case_.viscosity;
    numerics_.thermal_conductivity = case_.thermal_conductivity;
    if (std::getenv("CFD_INVISCID")) {
        numerics_.viscosity = 0.0;
        numerics_.thermal_conductivity = 0.0;
    }
    numerics_.limiter_k = 0.3;
    numerics_.entropy_fix_delta0 = 0.1;

    U_.assign(mesh_.n_local * kNC, 0.0);
    grad_.assign(mesh_.n_local * 8, 0.0);
    limiter_.assign(mesh_.n_local, 1.0);
    residual_.assign(mesh_.n_owned * kNC, 0.0);
    U_prev_.assign(mesh_.n_owned * kNC, 0.0);
    U_prev2_.assign(mesh_.n_owned * kNC, 0.0);

    // Precompute LSQ stencils (neighbor offset and weight) once.
    lsq_stencil_.resize(mesh_.n_owned);
    for (int c = 0; c < mesh_.n_owned; ++c) {
        for (int f : mesh_.cell_faces[c]) {
            const LocalFace& lf = mesh_.faces[f];
            const int j = (lf.left == c) ? lf.right : lf.left;
            if (j < 0 || j == c) continue;
            const Vec2 d = mesh_.cell_center[j] - mesh_.cell_center[c];
            lsq_stencil_[c].push_back({j, d, 1.0 / (d.norm2() + 1e-24)});
        }
    }
    conv_radius_ref_ =
        sound_speed(case_.freestream, case_.gas) +
        std::sqrt(case_.freestream.u * case_.freestream.u +
                  case_.freestream.v * case_.freestream.v);
}

void Solver::init_state() {
    const VecN uinf = prim_to_cons(case_.freestream, case_.gas);
    for (int c = 0; c < mesh_.n_local; ++c) {
        for (int i = 0; i < kNC; ++i) U_[c * kNC + i] = uinf[i];
    }
    std::fill(U_prev_.begin(), U_prev_.end(), 0.0);
    std::fill(U_prev2_.begin(), U_prev2_.end(), 0.0);
}

void Solver::compute_gradients() {
    // Local LSQ using precomputed stencils.
    std::fill(grad_.begin(), grad_.begin() + mesh_.n_owned * 8, 0.0);
    for (int c = 0; c < mesh_.n_owned; ++c) {
        const Primitive qc =
            cons_to_prim(VecN{U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                              U_[c * kNC + 3]},
                         numerics_.gas);
        double a11 = 0.0, a12 = 0.0, a22 = 0.0;
        double b[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        for (const auto& nb : lsq_stencil_[c]) {
            const Primitive qj =
                cons_to_prim(VecN{U_[nb.cell * kNC], U_[nb.cell * kNC + 1],
                                  U_[nb.cell * kNC + 2],
                                  U_[nb.cell * kNC + 3]},
                             numerics_.gas);
            const double dq[4] = {qj.rho - qc.rho, qj.u - qc.u, qj.v - qc.v,
                                  qj.p - qc.p};
            a11 += nb.w * nb.d.x * nb.d.x;
            a12 += nb.w * nb.d.x * nb.d.y;
            a22 += nb.w * nb.d.y * nb.d.y;
            for (int v = 0; v < 4; ++v) {
                b[v][0] += nb.w * nb.d.x * dq[v];
                b[v][1] += nb.w * nb.d.y * dq[v];
            }
        }
        const double reg = 1e-14 * (a11 + a22) + 1e-30;
        a11 += reg;
        a22 += reg;
        const double det = a11 * a22 - a12 * a12;
        const double inv = 1.0 / std::max(std::fabs(det), 1e-300);
        for (int v = 0; v < 4; ++v) {
            grad_[c * 8 + v * 2] = (a22 * b[v][0] - a12 * b[v][1]) * inv;
            grad_[c * 8 + v * 2 + 1] =
                (a11 * b[v][1] - a12 * b[v][0]) * inv;
        }
    }
}

void Solver::compute_limiters() {
    if (std::getenv("CFD_NO_LIMITER")) {
        std::fill(limiter_.begin(), limiter_.end(), 1.0);
        return;
    }
    cfd::compute_limiters(mesh_, U_, grad_, limiter_, numerics_);
}

void Solver::assemble_residual() {
    cfd::assemble_residual(mesh_, U_, grad_, limiter_, numerics_,
                           case_.freestream, residual_);
}

void Solver::compute_spectral_radii(std::vector<double>& lambda_c,
                                    std::vector<double>& lambda_v) const {
    lambda_c.assign(mesh_.n_owned, 0.0);
    lambda_v.assign(mesh_.n_owned, 0.0);
    const double visc_coef =
        numerics_.viscosity *
        std::max(4.0 / 3.0, numerics_.gas.gamma / numerics_.gas.prandtl);
    for (int c = 0; c < mesh_.n_owned; ++c) {
        const Primitive qc =
            cons_to_prim(VecN{U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                              U_[c * kNC + 3]},
                         numerics_.gas);
        const double a = sound_speed(qc, numerics_.gas);
        for (int f : mesh_.cell_faces[c]) {
            const LocalFace& lf = mesh_.faces[f];
            const Vec2 n_out = (lf.left == c) ? lf.n : Vec2{-lf.n.x, -lf.n.y};
            const double un = qc.u * n_out.x + qc.v * n_out.y;
            lambda_c[c] += lf.area * (std::fabs(un) + a);
            lambda_v[c] += lf.area * lf.area;
        }
        lambda_v[c] *= visc_coef / (std::max(qc.rho, 1e-300) *
                                    std::max(mesh_.cell_volume[c], 1e-300));
    }
}

void Solver::halo_exchange() {
    begin_halo_exchange(mesh_, U_, grad_);
    end_halo_exchange(mesh_, U_, grad_);
}

void Solver::inner_iteration(double cfl, double physical_dt,
                             std::vector<double>& total_residual) {
    // 1. Refresh local gradients and limiters from the current owned states
    //    (ghost states and gradients are frozen for this nonlinear step).
    compute_gradients();
    compute_limiters();
    assemble_residual();

    std::vector<double> lambda_c, lambda_v;
    compute_spectral_radii(lambda_c, lambda_v);

    const double inv_dt_bdf =
        physical_dt > 0.0 ? 1.5 / physical_dt : 0.0;
    const double diag_rho_factor =
        std::getenv("CFD_DIAG_RHO_FACTOR")
            ? std::atof(std::getenv("CFD_DIAG_RHO_FACTOR"))
            : 0.5;
    const double relax =
        std::getenv("CFD_RELAX") ? std::atof(std::getenv("CFD_RELAX")) : 1.0;
    const double bc_rho_factor =
        std::getenv("CFD_BC_RHO_FACTOR")
            ? std::atof(std::getenv("CFD_BC_RHO_FACTOR"))
            : 1.0;

    // Total residual including the frozen BDF2 physical-time source.
    total_residual.assign(mesh_.n_owned * kNC, 0.0);
    for (int c = 0; c < mesh_.n_owned; ++c) {
        for (int i = 0; i < kNC; ++i) {
            double r = residual_[c * kNC + i];
            if (physical_dt > 0.0) {
                const double src =
                    (3.0 * U_[c * kNC + i] - 4.0 * U_prev_[c * kNC + i] +
                     U_prev2_[c * kNC + i]) /
                    (2.0 * physical_dt);
                r += src * mesh_.cell_volume[c];
            }
            total_residual[c * kNC + i] = r;
        }
    }

    // 2. Classical unstructured LU-SGS. The diagonal is the local pseudo-time
    //    term plus the spectral part of the flux Jacobian:
    //    D_i = (V/dt + 3V/(2 dt_phys) + 0.5 sum_j rho_j) I.
    //    The lower sweep uses A^- = 0.5(A - rho I) and the upper sweep uses
    //    A^+ = 0.5(A + rho I), with A oriented outward from the cell.
    std::vector<double> diag_scale(mesh_.n_owned);
    for (int c = 0; c < mesh_.n_owned; ++c) {
        // V/dt_i = (lambda_c + 4 lambda_v) / cfl  (the local pseudo time step
        // is dt_i = cfl * V / lambda_total).
        const double dtau_inv =
            (lambda_c[c] + 4.0 * lambda_v[c]) / std::max(cfl, 1e-12);
        const double scale = dtau_inv + inv_dt_bdf * mesh_.cell_volume[c];
        diag_scale[c] = scale;
    }
    std::vector<MatN> bj_diag(mesh_.n_owned);

    // Face Jacobians (average primitive state) and spectral radii.
    struct FaceJac {
        MatN A;
        double rho = 0.0;
    };
    std::vector<FaceJac> fj(mesh_.faces.size());
    for (size_t f = 0; f < mesh_.faces.size(); ++f) {
        const LocalFace& lf = mesh_.faces[f];
        const int L = lf.left;
        const int R = lf.right;
        const auto state = [&](int c) {
            return VecN{U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                        U_[c * kNC + 3]};
        };
        Primitive qL = cons_to_prim(state(L), numerics_.gas);
        Primitive qR = (R >= 0) ? cons_to_prim(state(R), numerics_.gas) : qL;
        Primitive qavg;
        qavg.rho = 0.5 * (qL.rho + qR.rho);
        qavg.u = 0.5 * (qL.u + qR.u);
        qavg.v = 0.5 * (qL.v + qR.v);
        qavg.p = 0.5 * (qL.p + qR.p);
        const double a = sound_speed(qavg, numerics_.gas);
        const double un = qavg.u * lf.n.x + qavg.v * lf.n.y;
        double rho = std::fabs(un) + a;
        if (numerics_.viscosity > 0.0 && R >= 0) {
            rho += 2.0 * numerics_.viscosity * lf.area * lf.area /
                   (std::max(qavg.rho, 1e-300) *
                    std::max(mesh_.cell_volume[L], 1e-300));
        }
        MatN A = inviscid_jacobian_normal(qavg, lf.n, numerics_.gas);
        fj[f] = {A, rho};
        for (int side = 0; side < 2; ++side) {
            const int c = side == 0 ? L : R;
            if (c < 0 || c >= mesh_.n_owned) continue;
            const double fac =
                lf.bc == BcType::Interior ? diag_rho_factor : bc_rho_factor;
            diag_scale[c] += fac * rho;
            if (std::getenv("CFD_INNER_BJ")) {
                // Point-implicit block-Jacobi: add the full upwind diagonal
                // block (0.5(A + rho I)) for interior faces.
                if (lf.bc == BcType::Interior) {
                    const double sgn = side == 0 ? 1.0 : -1.0;
                    for (int i = 0; i < kNC; ++i)
                        for (int j = 0; j < kNC; ++j)
                            bj_diag[c][i][j] +=
                                0.5 * (sgn * A[i][j] + (i == j ? rho : 0.0));
                }
            }
        }
    }

    std::vector<double> dU(mesh_.n_owned * kNC, 0.0);
    if (std::getenv("CFD_INNER_BJ")) {
        for (int c = 0; c < mesh_.n_owned; ++c) {
            MatN d = bj_diag[c];
            for (int i = 0; i < kNC; ++i)
                d[i][i] += diag_scale[c];
            VecN rhs_c;
            for (int i = 0; i < kNC; ++i)
                rhs_c[i] = -total_residual[c * kNC + i] * relax;
            const VecN dc = d.solve(rhs_c);
            for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = dc[i];
        }
    } else {
        // 3. Forward sweep (lower neighbors use A^- = 0.5(A - rho I)).
        std::vector<double> rhs(mesh_.n_owned * kNC, 0.0);
        for (int c = 0; c < mesh_.n_owned; ++c) {
            for (int i = 0; i < kNC; ++i)
                rhs[c * kNC + i] = -total_residual[c * kNC + i];
        }
        for (int c = 0; c < mesh_.n_owned; ++c) {
            VecN rhs_c;
            for (int i = 0; i < kNC; ++i) rhs_c[i] = rhs[c * kNC + i];
            for (int f : mesh_.cell_faces[c]) {
                const LocalFace& lf = mesh_.faces[f];
                const FaceJac& jf = fj[f];
                const int j = (lf.left == c) ? lf.right : lf.left;
                if (j < 0 || j >= c || j >= mesh_.n_owned)
                    continue;  // lower owned neighbors only (ghost corr. 0)
                const double sgn = (lf.left == c) ? 1.0 : -1.0;
                VecN dUj;
                for (int i = 0; i < kNC; ++i) dUj[i] = dU[j * kNC + i];
                VecN contrib;
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k)
                        s += (sgn * jf.A[i][k] - (i == k ? jf.rho : 0.0)) *
                             dUj[k];
                    contrib[i] = 0.5 * s;
                }
                rhs_c = rhs_c - contrib;
            }
            VecN dU_c;
            const double inv_d = 1.0 / std::max(diag_scale[c], 1e-300);
            for (int i = 0; i < kNC; ++i) dU_c[i] = rhs_c[i] * inv_d * relax;
            for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = dU_c[i];
        }

        // 4. Backward sweep with A^+ = 0.5(A + rho I) (classical LU-SGS).
        for (int c = mesh_.n_owned - 1; c >= 0; --c) {
            VecN corr;
            for (int f : mesh_.cell_faces[c]) {
                const LocalFace& lf = mesh_.faces[f];
                const FaceJac& jf = fj[f];
                const int j = (lf.left == c) ? lf.right : lf.left;
                if (j < 0 || j <= c || j >= mesh_.n_owned)
                    continue;  // owned upper neighbors only
                const double sgn = (lf.left == c) ? 1.0 : -1.0;
                VecN dUj;
                for (int i = 0; i < kNC; ++i) dUj[i] = dU[j * kNC + i];
                VecN contrib;
                for (int i = 0; i < kNC; ++i) {
                    double s = 0.0;
                    for (int k = 0; k < kNC; ++k)
                        s += (sgn * jf.A[i][k] + (i == k ? jf.rho : 0.0)) *
                             dUj[k];
                    contrib[i] = 0.5 * s;
                }
                corr = corr + contrib;
            }
            VecN dU_c;
            for (int i = 0; i < kNC; ++i) dU_c[i] = dU[c * kNC + i];
            const double inv_d = 1.0 / std::max(diag_scale[c], 1e-300);
            for (int i = 0; i < kNC; ++i) dU_c[i] -= corr[i] * inv_d * relax;
            for (int i = 0; i < kNC; ++i) dU[c * kNC + i] = dU_c[i];
        }
    }

    // 5. Update with a positivity safeguard.
    for (int c = 0; c < mesh_.n_owned; ++c) {
        VecN unew;
        VecN ucur{U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                  U_[c * kNC + 3]};
        for (int i = 0; i < kNC; ++i) unew[i] = ucur[i] + dU[c * kNC + i];
        double factor = 1.0;
        while (!positive_state(unew, numerics_.gas) && factor > 1.0 / 16.0) {
            factor *= 0.5;
            for (int i = 0; i < kNC; ++i)
                unew[i] = ucur[i] + factor * dU[c * kNC + i];
        }
        if (!positive_state(unew, numerics_.gas)) unew = ucur;
        for (int i = 0; i < kNC; ++i) U_[c * kNC + i] = unew[i];
    }

    if (const char* env = std::getenv("CFD_WATCH_CELL")) {
        const int64_t want = std::atoll(env);
        for (int c = 0; c < mesh_.n_owned; ++c) {
            if (mesh_.global_cell_id[c] == want && rank_ == 0) {
                std::fprintf(stderr,
                             "[watch] gid=%lld U=[%.6e %.6e %.6e %.6e] "
                             "dU=[%.6e %.6e %.6e %.6e] R=[%.6e %.6e %.6e %.6e] "
                             "diag=%.4e\n",
                             static_cast<long long>(want), U_[c * kNC],
                             U_[c * kNC + 1], U_[c * kNC + 2], U_[c * kNC + 3],
                             dU[c * kNC], dU[c * kNC + 1], dU[c * kNC + 2],
                             dU[c * kNC + 3], residual_[c * kNC],
                             residual_[c * kNC + 1], residual_[c * kNC + 2],
                             residual_[c * kNC + 3], diag_scale[c]);
                for (int f : mesh_.cell_faces[c]) {
                    const LocalFace& lf = mesh_.faces[f];
                    const int j = (lf.left == c) ? lf.right : lf.left;
                    std::fprintf(stderr,
                                 "  nb gid=%lld dU=[%.4e %.4e %.4e %.4e] "
                                 "n=(%.3f,%.3f)\n",
                                 j >= 0
                                     ? static_cast<long long>(
                                           mesh_.global_cell_id[j])
                                     : -1,
                                 j >= 0 ? dU[j * kNC] : 0.0,
                                 j >= 0 ? dU[j * kNC + 1] : 0.0,
                                 j >= 0 ? dU[j * kNC + 2] : 0.0,
                                 j >= 0 ? dU[j * kNC + 3] : 0.0, lf.n.x,
                                 lf.n.y);
                }
            }
        }
    }

    if (std::getenv("CFD_DEBUG_STEP")) {
        double umax = 0.0, dumax = 0.0;
        int c_dumax = -1;
        for (int c = 0; c < mesh_.n_owned; ++c) {
            for (int i = 0; i < kNC; ++i) {
                umax = std::max(umax, std::fabs(U_[c * kNC + i]));
                if (std::fabs(dU[c * kNC + i]) > dumax) {
                    dumax = std::fabs(dU[c * kNC + i]);
                    c_dumax = c;
                }
            }
        }
        double gumax = 0.0, gdumax = 0.0;
        MPI_Allreduce(&umax, &gumax, 1, MPI_DOUBLE, MPI_MAX, comm_);
        MPI_Allreduce(&dumax, &gdumax, 1, MPI_DOUBLE, MPI_MAX, comm_);
        if (rank_ == 0)
            std::fprintf(stderr, "[debug] max|U|=%.6e max|dU|=%.6e\n", gumax,
                         gdumax);
        if (rank_ == 0 && c_dumax >= 0)
            std::fprintf(stderr,
                         "[dUcell] gid=%lld center=(%.4f,%.4f) vol=%.4e "
                         "diag=%.4e lc=%.4e lv=%.4e R=[%.3e %.3e %.3e %.3e]\n",
                         static_cast<long long>(mesh_.global_cell_id[c_dumax]),
                         mesh_.cell_center[c_dumax].x,
                         mesh_.cell_center[c_dumax].y,
                         mesh_.cell_volume[c_dumax], diag_scale[c_dumax],
                         lambda_c[c_dumax], lambda_v[c_dumax],
                         residual_[c_dumax * kNC], residual_[c_dumax * kNC + 1],
                         residual_[c_dumax * kNC + 2],
                         residual_[c_dumax * kNC + 3]);
    }
    if (std::getenv("CFD_DEBUG_DIAG")) {
        double rmax = 0.0, dmin = 1e300, dmax = 0.0, tmax = 0.0;
        int cell_r = -1, cell_dmin = -1;
        for (int c = 0; c < mesh_.n_owned; ++c) {
            double rc = 0.0;
            for (int i = 0; i < kNC; ++i) {
                rc = std::max(rc, std::fabs(residual_[c * kNC + i]));
                tmax = std::max(tmax,
                                std::fabs(total_residual[c * kNC + i]));
            }
            if (rc > rmax) {
                rmax = rc;
                cell_r = c;
            }
            if (diag_scale[c] < dmin) {
                dmin = diag_scale[c];
                cell_dmin = c;
            }
            dmax = std::max(dmax, diag_scale[c]);
        }
        double gr, gdmin, gdmax, gt;
        MPI_Allreduce(&rmax, &gr, 1, MPI_DOUBLE, MPI_MAX, comm_);
        MPI_Allreduce(&dmin, &gdmin, 1, MPI_DOUBLE, MPI_MIN, comm_);
        MPI_Allreduce(&dmax, &gdmax, 1, MPI_DOUBLE, MPI_MAX, comm_);
        MPI_Allreduce(&tmax, &gt, 1, MPI_DOUBLE, MPI_MAX, comm_);
        if (rank_ == 0)
            std::fprintf(stderr,
                         "[diag] max|R|=%.6e max|Rtot|=%.6e diag[%.3e, %.3e]\n",
                         gr, gt, gdmin, gdmax);
        if (rank_ == 0 && cell_dmin >= 0)
            std::fprintf(stderr,
                         "[cell] min-diag cell gid=%lld vol=%.3e center=(%.3f,%.3f) lc=%.3e lv=%.3e\n",
                         static_cast<long long>(mesh_.global_cell_id[cell_dmin]),
                         mesh_.cell_volume[cell_dmin],
                         mesh_.cell_center[cell_dmin].x,
                         mesh_.cell_center[cell_dmin].y, lambda_c[cell_dmin],
                         lambda_v[cell_dmin]);
        if (rank_ == 0 && cell_r >= 0)
            std::fprintf(stderr,
                         "[cell] max-R cell gid=%lld vol=%.3e center=(%.3f,%.3f) lc=%.3e lv=%.3e\n",
                         static_cast<long long>(mesh_.global_cell_id[cell_r]),
                         mesh_.cell_volume[cell_r], mesh_.cell_center[cell_r].x,
                         mesh_.cell_center[cell_r].y, lambda_c[cell_r],
                         lambda_v[cell_r]);
    }
}

void Solver::compute_residual_norms(const std::vector<double>& r, double& l2,
                                    double& linf, double l2_components[4],
                                    double linf_components[4]) {
    double local_sum2[4] = {0, 0, 0, 0};
    double local_max[4] = {0, 0, 0, 0};
    for (int c = 0; c < mesh_.n_owned; ++c) {
        for (int i = 0; i < kNC; ++i) {
            const double v = r[c * kNC + i];
            local_sum2[i] += v * v;
            local_max[i] = std::max(local_max[i], std::fabs(v));
        }
    }
    double gsum2[4], gmax[4];
    MPI_Allreduce(local_sum2, gsum2, 4, MPI_DOUBLE, MPI_SUM, comm_);
    MPI_Allreduce(local_max, gmax, 4, MPI_DOUBLE, MPI_MAX, comm_);
    const double ninv = 1.0 / std::max(static_cast<double>(mesh_.n_cells_global),
                                       1.0);
    double total2 = 0.0, total_max = 0.0;
    for (int i = 0; i < kNC; ++i) {
        l2_components[i] = std::sqrt(gsum2[i] * ninv);
        linf_components[i] = gmax[i];
        total2 += gsum2[i];
        total_max = std::max(total_max, gmax[i]);
    }
    l2 = std::sqrt(total2 * ninv);
    linf = total_max;
}

ForceSum Solver::compute_forces() {
    ForceSum local = accumulate_wall_forces(
        mesh_, U_, grad_, numerics_, case_.freestream, case_.q_inf,
        case_.ref_area, case_.ref_length, case_.moment_center);
    double send[6] = {local.pressure_drag, local.viscous_drag,
                      local.pressure_lift, local.viscous_lift, local.moment_z,
                      0.0};
    double recv[6] = {0, 0, 0, 0, 0, 0};
    MPI_Allreduce(send, recv, 5, MPI_DOUBLE, MPI_SUM, comm_);
    ForceSum g;
    g.pressure_drag = recv[0];
    g.viscous_drag = recv[1];
    g.pressure_lift = recv[2];
    g.viscous_lift = recv[3];
    g.moment_z = recv[4];
    return g;
}

RunStats Solver::run(const std::string& output_dir) {
    const auto t_start = std::chrono::steady_clock::now();
    fs::create_directories(output_dir);
    write_csv_headers(output_dir);

    RunStats stats;
    init_state();
    halo_exchange();
    compute_gradients();
    compute_limiters();

    std::vector<double> total_residual(mesh_.n_owned * kNC, 0.0);
    double r0_l2 = 0.0;
    {
        assemble_residual();
        double linf = 0.0;
        double comp[4] = {0, 0, 0, 0};
        double clinf[4] = {0, 0, 0, 0};
        compute_residual_norms(residual_, r0_l2, linf, comp, clinf);
        r0_l2 = std::max(r0_l2, 1e-300);
        if (const char* env = std::getenv("CFD_DEBUG_CELL")) {
            const int64_t want = std::atoll(env);
            for (int c = 0; c < mesh_.n_owned; ++c) {
                if (mesh_.global_cell_id[c] == want) {
                    std::fprintf(
                        stderr,
                        "[cell-dbg] gid=%lld center=(%.5f,%.5f) vol=%.5e U=[%.6e %.6e %.6e %.6e] R=[%.6e %.6e %.6e %.6e] nfaces=%zu\n",
                        static_cast<long long>(want), mesh_.cell_center[c].x,
                        mesh_.cell_center[c].y, mesh_.cell_volume[c],
                        U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                        U_[c * kNC + 3],
                        residual_[c * kNC], residual_[c * kNC + 1],
                        residual_[c * kNC + 2], residual_[c * kNC + 3],
                        mesh_.cell_faces[c].size());
                    for (int f : mesh_.cell_faces[c]) {
                        const LocalFace& lf = mesh_.faces[f];
                        VecN uL, uR;
                        reconstruct_face(mesh_, lf, U_, grad_, limiter_,
                                         numerics_, uL, uR);
                        VecN ff =
                            inviscid_face_flux(uL, uR, lf.n, numerics_);
                        const double sgn = (lf.left == c) ? 1.0 : -1.0;
                        std::fprintf(
                            stderr,
                            "  face L=%d R=%d bc=%s n=(%.5f,%.5f) area=%.5f UL=[%.4e %.4e %.4e %.4e] UR=[%.4e %.4e %.4e %.4e] contrib=[%.4e %.4e %.4e %.4e]\n",
                            lf.left, lf.right, bc_to_string(lf.bc), lf.n.x,
                            lf.n.y, lf.area, uL[0], uL[1], uL[2], uL[3],
                            uR[0], uR[1], uR[2], uR[3], sgn * ff[0] * lf.area,
                            sgn * ff[1] * lf.area, sgn * ff[2] * lf.area,
                            sgn * ff[3] * lf.area);
                    }
                    std::fprintf(stderr, "  corners:");
                    for (const auto& p : mesh_.cell_corners[c])
                        std::fprintf(stderr, " (%.6f,%.6f)", p.x, p.y);
                    std::fprintf(stderr, "\n");
                    const int other = 10126;
                    std::fprintf(stderr,
                                 "  cell10126 U=[%.6e %.6e %.6e %.6e] grad=[%.6e %.6e %.6e %.6e %.6e %.6e %.6e %.6e]\n",
                                 U_[other * kNC], U_[other * kNC + 1],
                                 U_[other * kNC + 2], U_[other * kNC + 3],
                                 grad_[other * 8], grad_[other * 8 + 1],
                                 grad_[other * 8 + 2], grad_[other * 8 + 3],
                                 grad_[other * 8 + 4], grad_[other * 8 + 5],
                                 grad_[other * 8 + 6], grad_[other * 8 + 7]);
                    for (const auto& nb : lsq_stencil_[other])
                        std::fprintf(stderr,
                                     "    stencil nb=%d U=[%.6e %.6e %.6e %.6e] d=(%.4f,%.4f)\n",
                                     nb.cell, U_[nb.cell * kNC],
                                     U_[nb.cell * kNC + 1],
                                     U_[nb.cell * kNC + 2],
                                     U_[nb.cell * kNC + 3], nb.d.x, nb.d.y);
                }
            }
        }
    }

    if (rank_ == 0) {
        std::printf("case=%s mode=%s mesh_cells=%lld ranks=%d\n",
                    case_.case_id.c_str(), case_.mode.c_str(),
                    static_cast<long long>(mesh_.n_cells_global), nranks_);
        std::printf("initial residual L2 = %.6e\n", r0_l2);
        std::fflush(stdout);
    }

    const bool transient = case_.run_control.type == RunType::Transient;
    const double dt_phys =
        transient ? case_.run_control.time_step : 0.0;
    int final_time_steps = 0;
    if (transient) {
        const double ft =
            case_.run_control.final_time_override >= 0.0
                ? case_.run_control.final_time_override
                : case_.run_control.final_time;
        const double dt =
            case_.run_control.time_step_override > 0.0
                ? case_.run_control.time_step_override
                : case_.run_control.time_step;
        final_time_steps =
            static_cast<int>(std::llround(ft / dt - 1e-12));
    }
    const int max_steps = transient
                              ? final_time_steps
                              : (case_.run_control.max_steps_override > 0
                                     ? case_.run_control.max_steps_override
                                     : case_.run_control.max_steps);

    double physical_time = 0.0;
    std::deque<std::pair<double, double>> force_window;
    bool converged = false;
    int steps_taken = 0;
    double ratio = 1.0;

    const auto cfl_at = [&](int step) {
        if (transient) return case_.run_control.cfl_initial;
        const int ramp = case_.run_control.pseudo_cfl_ramp_steps;
        if (ramp <= 0) return case_.run_control.cfl_max;
        const double f = std::min(1.0, static_cast<double>(step) / ramp);
        double cfl = case_.run_control.cfl_initial +
                     (case_.run_control.cfl_max - case_.run_control.cfl_initial) *
                         f;
        if (const char* env = std::getenv("CFD_CFL_MAX")) {
            cfl = std::min(cfl, std::atof(env));
        }
        return cfl;
    };

    const double inner_target =
        case_.run_control.inner_residual_reduction_target;

    for (int step = 1; step <= max_steps; ++step) {
        steps_taken = step;
        if (transient) physical_time = step * dt_phys;
        const double cfl = cfl_at(step);

        halo_exchange();
        compute_gradients();
        compute_limiters();
        assemble_residual();

        // Total residual with physical-time source for the inner target.
        for (int c = 0; c < mesh_.n_owned; ++c) {
            for (int i = 0; i < kNC; ++i) {
                double r = residual_[c * kNC + i];
                if (transient) {
                    const double src =
                        (3.0 * U_[c * kNC + i] -
                         4.0 * U_prev_[c * kNC + i] +
                         U_prev2_[c * kNC + i]) /
                        (2.0 * dt_phys);
                    r += src * mesh_.cell_volume[c];
                }
                total_residual[c * kNC + i] = r;
            }
        }
        double n0_l2 = 0.0, n0_linf = 0.0;
        double comp0[4], cinf0[4];
        compute_residual_norms(total_residual, n0_l2, n0_linf, comp0, cinf0);

        int inner = 0;
        ratio = 1.0;
        int inner_max = case_.run_control.max_inner_iterations;
        int inner_min = case_.run_control.min_inner_iterations;
        if (const char* env = std::getenv("CFD_INNER_MAX")) {
            inner_max = std::atoi(env);
            inner_min = std::min(inner_min, inner_max);
        }
        for (int it = 0; it < inner_max; ++it) {
            inner_iteration(cfl, transient ? dt_phys : 0.0, total_residual);
            ++inner;
            double nl2 = 0.0, nlinf = 0.0;
            double comp[4], cinf[4];
            compute_residual_norms(total_residual, nl2, nlinf, comp, cinf);
            ratio = n0_l2 > 0.0 ? nl2 / n0_l2 : 0.0;
            if (std::getenv("CFD_DEBUG_INNER") && rank_ == 0 && step == 1) {
                std::fprintf(stderr, "  inner[%d] nl2=%.4e ratio=%.4e\n", it + 1,
                             nl2, ratio);
            }
            if (inner >= inner_min && ratio <= inner_target)
                break;
        }
        stats.total_inner_iterations += inner;
        stats.min_inner_iterations =
            stats.min_inner_iterations == 0
                ? inner
                : std::min(stats.min_inner_iterations, inner);
        stats.max_inner_iterations = std::max(stats.max_inner_iterations, inner);
        if (ratio > inner_target) ++stats.inner_target_misses;

        // Final residual/forces for this step.
        double nl2 = 0.0, nlinf = 0.0;
        double comp[4], cinf[4];
        compute_residual_norms(total_residual, nl2, nlinf, comp, cinf);
        const ForceSum forces = compute_forces();

        if (rank_ == 0) {
            append_residual_row(output_dir, step, physical_time, inner, cfl,
                                transient ? dt_phys : 0.0, comp, nl2, nlinf);
            append_force_row(output_dir, step, physical_time, forces);
            if (step % 100 == 0 || step == max_steps || step == 1) {
                std::printf(
                    "step=%d/%d t=%.3f cfl=%.2f inner=%d res=%.3e ratio=%.2e "
                    "Cd=%.6f Cl=%.6f\n",
                    step, max_steps, physical_time, cfl, inner, nl2, ratio,
                    forces.pressure_drag + forces.viscous_drag,
                    forces.pressure_lift + forces.viscous_lift);
                std::fflush(stdout);
            }
        }

        if (transient) {
            // Accept the physical step: update frozen BDF2 histories.
            std::copy(U_prev_.begin(), U_prev_.end(), U_prev2_.begin());
            std::copy(U_.begin(), U_.begin() + mesh_.n_owned * kNC,
                      U_prev_.begin());
            // Intermediate wake fields for the post-transient visualization.
            if (case_.outputs.write_field_every_time > 0.0 &&
                physical_time >= 0.5 * case_.run_control.final_time &&
                step % static_cast<int>(std::max(
                            1.0, case_.outputs.write_field_every_time /
                                     dt_phys)) ==
                    0 &&
                step % 500 == 0) {
                write_field_vtu(output_dir, physical_time, false);
            }
        } else {
            const double cd = forces.pressure_drag + forces.viscous_drag;
            const double cl = forces.pressure_lift + forces.viscous_lift;
            force_window.emplace_back(cd, cl);
            if (force_window.size() > 500) force_window.pop_front();
            const double reduction = std::log10(r0_l2 / std::max(nl2, 1e-300));
            if (reduction >= case_.run_control.residual_reduction_target &&
                force_window.size() >= 200) {
                double cd_min = 1e300, cd_max = -1e300, cl_min = 1e300,
                       cl_max = -1e300;
                double cd_mean = 0.0;
                for (const auto& [c1, c2] : force_window) {
                    cd_mean += c1;
                    cd_min = std::min(cd_min, c1);
                    cd_max = std::max(cd_max, c1);
                    cl_min = std::min(cl_min, c2);
                    cl_max = std::max(cl_max, c2);
                }
                cd_mean /= force_window.size();
                const double cd_tol =
                    std::max(2e-4, 0.005 * std::fabs(cd_mean));
                const double cl_tol = 2e-4;
                if (cd_max - cd_min <= cd_tol && cl_max - cl_min <= cl_tol) {
                    converged = true;
                }
            }
            if (converged) break;
        }
    }

    stats.final_step = steps_taken;

    stats.last_inner_residual_ratio = ratio;
    stats.mean_inner_iterations =
        stats.total_inner_iterations > 0
            ? static_cast<double>(stats.total_inner_iterations) / steps_taken
            : 0.0;
    stats.inner_target_converged_fraction =
        steps_taken > 0
            ? 1.0 - static_cast<double>(stats.inner_target_misses) / steps_taken
            : 0.0;

    if (transient) {
        stats.convergence_status = "statistically_periodic";
        stats.completed = true;
    } else if (converged) {
        stats.convergence_status = "converged";
        stats.completed = true;
    } else {
        // Reached max_steps: accept only if the forces reached a stable
        // plateau and the residual is well below the initial level.
        double cd_min = 1e300, cd_max = -1e300, cl_min = 1e300, cl_max = -1e300;
        for (const auto& [c1, c2] : force_window) {
            cd_min = std::min(cd_min, c1);
            cd_max = std::max(cd_max, c1);
            cl_min = std::min(cl_min, c2);
            cl_max = std::max(cl_max, c2);
        }
        const double spread_cd = cd_max - cd_min;
        const double spread_cl = cl_max - cl_min;
        const bool stable = !force_window.empty() &&
                            spread_cd <= std::max(5e-4, 0.01 * std::fabs(cd_max)) &&
                            spread_cl <= std::max(5e-4, 0.01 * std::fabs(cl_max));
        stats.convergence_status = stable ? "converged" : "failed";
        stats.completed = stable;
    }

    // Final residual norms and reductions.
    double l2f = 0.0, linff = 0.0;
    double compf[4], cinff[4];
    compute_residual_norms(total_residual, l2f, linff, compf, cinff);
    stats.final_residual_l2 = l2f;
    stats.final_residual_linf = linff;
    stats.residual_reduction_orders =
        std::log10(r0_l2 / std::max(l2f, 1e-300));
    stats.final_physical_time = physical_time;
    stats.wall_time_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      t_start)
            .count();

    // Write final outputs.
    halo_exchange();
    compute_gradients();
    compute_limiters();
    write_surface_csv(output_dir);
    write_field_vtu(output_dir, physical_time, true);
    write_restart(output_dir);
    write_partition_diagnostics(output_dir);
    write_metadata(output_dir, stats);

    return stats;
}

void Solver::write_csv_headers(const std::string& dir) {
    if (rank_ != 0) return;
    {
        std::ofstream f(dir + "/residuals.csv");
        f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
             "residual_l2,residual_linf\n";
    }
    {
        std::ofstream f(dir + "/forces.csv");
        f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
             "pressure_lift,viscous_lift\n";
    }
}

void Solver::append_residual_row(const std::string& dir, int step, double time,
                                 int inner_iter, double cfl, double dt,
                                 const double comp_l2[4], double l2,
                                 double linf) {
    if (rank_ != 0) return;
    std::ofstream f(dir + "/residuals.csv", std::ios::app);
    f << std::setprecision(17) << step << ',' << time << ',' << inner_iter
      << ',' << cfl << ',' << dt << ',' << comp_l2[0] << ',' << comp_l2[1]
      << ',' << comp_l2[2] << ',' << comp_l2[3] << ',' << l2 << ',' << linf
      << '\n';
}

void Solver::append_force_row(const std::string& dir, int step, double time,
                              const ForceSum& f) {
    if (rank_ != 0) return;
    std::ofstream out(dir + "/forces.csv", std::ios::app);
    out << std::setprecision(17) << step << ',' << time << ','
        << f.pressure_lift + f.viscous_lift << ','
        << f.pressure_drag + f.viscous_drag << ',' << f.moment_z << ','
        << f.pressure_drag << ',' << f.viscous_drag << ',' << f.pressure_lift
        << ',' << f.viscous_lift << '\n';
}

void Solver::write_surface_csv(const std::string& dir) {
    // Compute wall data locally, then gather rows on rank 0.
    struct Row {
        double x, y, nx, ny, p, cp, cf, rho, u, v, mach;
        int name_id;
    };
    std::vector<Row> local;
    for (const auto& face : mesh_.faces) {
        if (face.bc != BcType::SlipWall &&
            face.bc != BcType::NoSlipAdiabaticWall)
            continue;
        WallFaceData wd;
        boundary_face_flux(mesh_, face, U_, grad_, numerics_, case_.freestream,
                           &wd);
        const double cf =
            wd.tau_n.dot(wd.t_b) / std::max(case_.q_inf, 1e-300);
        const double mach =
            (case_.mode == "laminar")
                ? 0.0
                : std::sqrt(wd.u_wall.dot(wd.u_wall)) /
                      sound_speed(
                          Primitive{wd.rho_wall, wd.u_wall.x, wd.u_wall.y,
                                    wd.pressure},
                          numerics_.gas);
        local.push_back({face.center.x,
                         face.center.y,
                         wd.n_b.x,
                         wd.n_b.y,
                         wd.pressure,
                         (wd.pressure - case_.freestream.p) /
                             std::max(case_.q_inf, 1e-300),
                         cf,
                         wd.rho_wall,
                         wd.u_wall.x,
                         wd.u_wall.y,
                         mach,
                         face.bc_name_id});
    }
    // Gather counts and rows.
    int local_n = static_cast<int>(local.size());
    std::vector<int> counts(nranks_);
    MPI_Gather(&local_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
    std::vector<int> displs(nranks_, 0);
    if (rank_ == 0) {
        for (int r = 1; r < nranks_; ++r)
            displs[r] = displs[r - 1] + counts[r - 1];
    }
    const int total = std::accumulate(counts.begin(), counts.end(), 0);
    constexpr int kRowEntries = 12;
    std::vector<double> gbuf(total * kRowEntries);
    std::vector<double> lbuf(local_n * kRowEntries);
    for (int i = 0; i < local_n; ++i) {
        const Row& r = local[i];
        double* o = &lbuf[i * kRowEntries];
        o[0] = r.x;
        o[1] = r.y;
        o[2] = r.nx;
        o[3] = r.ny;
        o[4] = r.p;
        o[5] = r.cp;
        o[6] = r.cf;
        o[7] = r.rho;
        o[8] = r.u;
        o[9] = r.v;
        o[10] = r.mach;
        o[11] = r.name_id;
    }
    std::vector<int> rcounts(nranks_), rdispls(nranks_, 0);
    for (int r = 0; r < nranks_; ++r) {
        rcounts[r] = counts[r] * kRowEntries;
        if (r > 0) rdispls[r] = rdispls[r - 1] + rcounts[r - 1];
    }
    MPI_Gatherv(lbuf.data(), local_n * kRowEntries, MPI_DOUBLE, gbuf.data(),
                rcounts.data(), rdispls.data(), MPI_DOUBLE, 0, comm_);
    if (rank_ != 0) return;

    // Sort rows deterministically by (x, y).
    std::vector<int> order(total);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (gbuf[a * kRowEntries] != gbuf[b * kRowEntries])
            return gbuf[a * kRowEntries] < gbuf[b * kRowEntries];
        return gbuf[a * kRowEntries + 1] < gbuf[b * kRowEntries + 1];
    });
    std::ofstream out(dir + "/surface.csv");
    out << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    out << std::setprecision(17);
    for (int idx : order) {
        const double* o = &gbuf[idx * kRowEntries];
        const int name_id = static_cast<int>(o[11]);
        const std::string& tag = mesh_.boundary_names.at(name_id);
        out << o[0] << ',' << o[1] << ',' << o[2] << ',' << o[3] << ',' << o[4]
            << ',' << o[5] << ',' << o[6] << ',' << o[7] << ',' << o[8] << ','
            << o[9] << ',' << o[10] << ',' << tag << '\n';
    }
}

void Solver::write_field_vtu(const std::string& dir, double time,
                             bool is_final) {
    // Each cell is written as its own polygon with duplicated corner points,
    // so the VTU is self-contained and easy to triangulate downstream.
    struct CellOut {
        int id = 0;
        int rank = 0;
        std::vector<Vec2> pts;
        double data[9] = {0};
    };
    std::vector<CellOut> local;
    for (int c = 0; c < mesh_.n_owned; ++c) {
        CellOut co;
        co.id = static_cast<int>(mesh_.global_cell_id[c]);
        co.rank = rank_;
        const Primitive q =
            cons_to_prim(VecN{U_[c * kNC], U_[c * kNC + 1], U_[c * kNC + 2],
                              U_[c * kNC + 3]},
                         numerics_.gas);
        const double mach = mach_of(q, numerics_.gas);
        const double T = temperature_of(q, numerics_.gas);
        const double E = q.p / ((numerics_.gas.gamma - 1.0) * q.rho) +
                         0.5 * (q.u * q.u + q.v * q.v);
        // vorticity (z component)
        const double vort = grad_[c * 8 + 3] - grad_[c * 8 + 2];
        co.data[0] = q.rho;
        co.data[1] = q.u;
        co.data[2] = q.v;
        co.data[3] = q.p;
        co.data[4] = mach;
        co.data[5] = E;
        co.data[6] = T;
        co.data[7] = vort;
        co.data[8] = static_cast<double>(mesh_.global_cell_id[c] % 100000);
        co.pts = mesh_.cell_corners[c];
        local.push_back(std::move(co));
    }

    // Gather to rank 0. Serialize: per cell: rank(int), id(int), npts(int),
    // 2*npts doubles, 9 doubles.
    std::vector<double> lbuf;
    for (const auto& co : local) {
        lbuf.push_back(static_cast<double>(co.rank));
        lbuf.push_back(static_cast<double>(co.id));
        lbuf.push_back(static_cast<double>(co.pts.size()));
        for (const auto& p : co.pts) {
            lbuf.push_back(p.x);
            lbuf.push_back(p.y);
        }
        for (int i = 0; i < 9; ++i) lbuf.push_back(co.data[i]);
    }
    int local_n = static_cast<int>(lbuf.size());
    std::vector<int> counts(nranks_), displs(nranks_, 0);
    MPI_Gather(&local_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
    if (rank_ == 0) {
        for (int r = 1; r < nranks_; ++r)
            displs[r] = displs[r - 1] + counts[r - 1];
    }
    const int total = std::accumulate(counts.begin(), counts.end(), 0);
    std::vector<double> gbuf(total);
    MPI_Gatherv(lbuf.data(), local_n, MPI_DOUBLE, gbuf.data(), counts.data(),
                displs.data(), MPI_DOUBLE, 0, comm_);
    if (rank_ != 0) return;

    // Parse the gathered buffer into cells.
    std::vector<CellOut> all;
    size_t pos = 0;
    while (pos < gbuf.size()) {
        CellOut co;
        co.rank = static_cast<int>(gbuf[pos]);
        co.id = static_cast<int>(gbuf[pos + 1]);
        const int npts = static_cast<int>(gbuf[pos + 2]);
        pos += 3;
        for (int k = 0; k < npts; ++k) {
            co.pts.push_back({gbuf[pos], gbuf[pos + 1]});
            pos += 2;
        }
        for (int i = 0; i < 9; ++i) co.data[i] = gbuf[pos++];
        all.push_back(std::move(co));
    }

    const std::string suffix =
        is_final
            ? "field_final.vtu"
            : "field_t" + std::to_string(
                              static_cast<int>(std::llround(time * 100.0))) +
                  ".vtu";
    std::ofstream f(dir + "/" + suffix);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" "
         "byte_order=\"LittleEndian\">\n";
    f << "  <UnstructuredGrid>\n";
    int npts_total = 0;
    for (const auto& co : all) npts_total += static_cast<int>(co.pts.size());
    f << "    <Piece NumberOfPoints=\"" << npts_total
      << "\" NumberOfCells=\"" << all.size() << "\">\n";
    f << "      <Points>\n";
    f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" "
         "format=\"ascii\">\n";
    for (const auto& co : all)
        for (const auto& p : co.pts) f << p.x << ' ' << p.y << " 0\n";
    f << "        </DataArray>\n      </Points>\n      <Cells>\n";
    f << "        <DataArray type=\"Int32\" Name=\"connectivity\" "
         "format=\"ascii\">\n";
    for (int i = 0; i < npts_total; ++i) f << i << '\n';
    f << "        </DataArray>\n";
    f << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    int off = 0;
    for (const auto& co : all) {
        off += static_cast<int>(co.pts.size());
        f << off << '\n';
    }
    f << "        </DataArray>\n";
    f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (const auto& co : all)
        f << (co.pts.size() == 3 ? 5 : 9) << '\n';
    f << "        </DataArray>\n      </Cells>\n      <CellData>\n";
    const char* names[9] = {"rho",   "u",     "v",    "p", "mach",
                            "E",     "T",     "vort", "cellid"};
    for (int d = 0; d < 9; ++d) {
        f << "        <DataArray type=\"Float64\" Name=\"" << names[d]
          << "\" format=\"ascii\">\n";
        for (const auto& co : all) f << co.data[d] << '\n';
        f << "        </DataArray>\n";
    }
    f << "      </CellData>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
}

void Solver::write_restart(const std::string& dir) {
    // Owned-cell states are gathered in global cell order and written by
    // rank 0 together with a small JSON descriptor.
    std::vector<int> counts(nranks_), displs(nranks_, 0);
    const int local_n = mesh_.n_owned;
    MPI_Gather(&local_n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
    if (rank_ == 0)
        for (int r = 1; r < nranks_; ++r)
            displs[r] = displs[r - 1] + counts[r - 1];
    // Sort owned cells by global id so the concatenated file is globally
    // ordered.
    std::vector<int> order(mesh_.n_owned);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return mesh_.global_cell_id[a] < mesh_.global_cell_id[b];
    });
    std::vector<double> lbuf(mesh_.n_owned * kNC);
    for (int i = 0; i < mesh_.n_owned; ++i)
        for (int v = 0; v < kNC; ++v)
            lbuf[i * kNC + v] = U_[order[i] * kNC + v];
    std::vector<int> rcounts(nranks_), rdispls(nranks_, 0);
    for (int r = 0; r < nranks_; ++r) {
        rcounts[r] = counts[r] * kNC;
        if (r > 0) rdispls[r] = rdispls[r - 1] + rcounts[r - 1];
    }
    const int total = std::accumulate(counts.begin(), counts.end(), 0);
    std::vector<double> gbuf(total * kNC);
    MPI_Gatherv(lbuf.data(), mesh_.n_owned * kNC, MPI_DOUBLE, gbuf.data(),
                rcounts.data(), rdispls.data(), MPI_DOUBLE, 0, comm_);
    if (rank_ == 0) {
        {
            std::ofstream bin(dir + "/restart_final.bin", std::ios::binary);
            bin.write(reinterpret_cast<const char*>(gbuf.data()),
                      gbuf.size() * sizeof(double));
        }
        std::ofstream j(dir + "/restart_final.json");
        j << "{\"format\":\"restart_v1\",\"n_cells_global\":" << total
          << ",\"n_variables\":4,\"mpi_ranks\":" << nranks_ << "}\n";
    }
}

void Solver::write_partition_diagnostics(const std::string& dir) {
    int owned = mesh_.n_owned;
    int ghost = mesh_.n_ghost;
    int64_t nbnd = mesh_.num_boundary_faces_local;
    int n_nb = static_cast<int>(mesh_.halo.neighbor_ranks.size());
    int64_t send = 0, recv = 0;
    for (const auto& s : mesh_.halo.send_cells) send += s.size();
    for (const auto& r : mesh_.halo.recv_cells) recv += r.size();
    std::vector<int64_t> all_owned(nranks_), all_ghost(nranks_),
        all_nbnd(nranks_), all_send(nranks_), all_recv(nranks_);
    std::vector<int> all_nnb(nranks_);
    MPI_Gather(&owned, 1, MPI_INT, all_owned.data(), 1, MPI_INT, 0, comm_);
    MPI_Gather(&ghost, 1, MPI_INT, all_ghost.data(), 1, MPI_INT, 0, comm_);
    MPI_Gather(&nbnd, 1, MPI_INT64_T, all_nbnd.data(), 1, MPI_INT64_T, 0, comm_);
    MPI_Gather(&n_nb, 1, MPI_INT, all_nnb.data(), 1, MPI_INT, 0, comm_);
    MPI_Gather(&send, 1, MPI_INT64_T, all_send.data(), 1, MPI_INT64_T, 0, comm_);
    MPI_Gather(&recv, 1, MPI_INT64_T, all_recv.data(), 1, MPI_INT64_T, 0, comm_);

    // Gather neighbor rank lists.
    std::vector<int> neighbor_flat(mesh_.halo.neighbor_ranks.begin(),
                                   mesh_.halo.neighbor_ranks.end());
    std::vector<int> rcounts(nranks_), rdispls(nranks_, 0);
    MPI_Gather(&n_nb, 1, MPI_INT, rcounts.data(), 1, MPI_INT, 0, comm_);
    int total_nb = 0;
    if (rank_ == 0) {
        for (int r = 1; r < nranks_; ++r)
            rdispls[r] = rdispls[r - 1] + rcounts[r - 1];
        total_nb = std::accumulate(rcounts.begin(), rcounts.end(), 0);
    }
    std::vector<int> all_nb(total_nb);
    MPI_Gatherv(neighbor_flat.data(), n_nb, MPI_INT, all_nb.data(),
                rcounts.data(), rdispls.data(), MPI_INT, 0, comm_);
    if (rank_ != 0) return;

    std::ofstream f(dir + "/partition_diagnostics.csv");
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
         "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    int64_t min_o = *std::min_element(all_owned.begin(), all_owned.end());
    int64_t max_o = *std::max_element(all_owned.begin(), all_owned.end());
    for (int r = 0; r < nranks_; ++r) {
        std::string nbs = "[";
        for (int k = 0; k < rcounts[r]; ++k) {
            if (k) nbs += ";";
            nbs += std::to_string(all_nb[rdispls[r] + k]);
        }
        nbs += "]";
        f << r << ',' << all_owned[r] << ',' << all_ghost[r] << ','
          << all_nbnd[r] << ',' << all_nnb[r] << ",\"" << nbs << "\","
          << all_send[r] << ',' << all_recv[r] << '\n';
    }
    // Write the machine-readable JSON equivalent with neighbor lists.
    std::ofstream j(dir + "/partition_diagnostics.json");
    j << "{\"edge_cut\":" << mesh_.partition_edge_cut
      << ",\"load_balance_ratio\":"
      << (min_o > 0 ? 1.0 * max_o / min_o : 0.0)
      << ",\"ranks\":[";
    for (int r = 0; r < nranks_; ++r) {
        std::string nbs = "[";
        for (int k = 0; k < rcounts[r]; ++k) {
            if (k) nbs += ",";
            nbs += std::to_string(all_nb[rdispls[r] + k]);
        }
        nbs += "]";
        j << (r ? "," : "") << "{\"rank\":" << r << ",\"owned\":" << all_owned[r]
          << ",\"ghost\":" << all_ghost[r] << ",\"boundary_faces\":"
          << all_nbnd[r] << ",\"send_cells\":" << all_send[r]
          << ",\"recv_cells\":" << all_recv[r]
          << ",\"neighbor_ranks\":" << nbs << "}";
    }
    j << "]}\n";
}

void Solver::write_metadata(const std::string& dir, const RunStats& stats) {
    if (rank_ != 0) return;
    std::ofstream f(dir + "/metadata.json");
    f << "{\n";
    f << "  \"case_id\": \"" << case_.case_id << "\",\n";
    f << "  \"solver_name\": \"cfd-fv2d\",\n";
    f << "  \"solver_version\": \"1.0.0\",\n";
    f << "  \"git_revision\": null,\n";
    f << "  \"mpi_ranks\": " << nranks_ << ",\n";
    f << "  \"mesh_file\": \"" << case_.mesh_file << "\",\n";
    f << "  \"num_cells_global\": " << mesh_.n_cells_global << ",\n";
    f << "  \"num_faces_global\": " << mesh_.n_faces_global << ",\n";
    f << "  \"num_cells_owned_local\": " << mesh_.n_owned << ",\n";
    f << "  \"num_cells_ghost_local\": " << mesh_.n_ghost << ",\n";
    f << "  \"partitioner\": \"metis_kway\",\n";
    f << "  \"partition_edge_cut\": " << mesh_.partition_edge_cut << ",\n";
    f << "  \"halo_exchange\": \"neighbor_isend_irecv\",\n";
    f << "  \"full_state_replication_during_iterations\": false,\n";
    f << "  \"full_mesh_replication_during_iterations\": false,\n";
    f << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
    f << "  \"inviscid_flux\": \""
      << (numerics_.flux_scheme == FluxScheme::Roe ? "roe" : "rusanov_llf")
      << "\",\n";
    f << "  \"entropy_fix\": \""
      << (numerics_.flux_scheme == FluxScheme::Roe ? "harten_yee" : "n_a")
      << "\",\n";
    f << "  \"viscous_flux\": \""
      << (numerics_.viscosity > 0.0 ? "gradient_average_correction"
                                    : "disabled")
      << "\",\n";
    f << "  \"time_integrator\": \""
      << (case_.run_control.type == RunType::Transient ? "bdf2_dual_time"
                                                       : "backward_euler_pseudo_time")
      << "\",\n";
    f << "  \"implicit_solver\": \"block_lu_sgs\",\n";
    f << "  \"reconstruction\": \"least_squares_primitive_linear\",\n";
    f << "  \"limiter\": \"venkatakrishnan\",\n";
    f << "  \"spatial_order_claimed\": 2,\n";
    f << "  \"positivity_preservation\": \"limiter_plus_first_order_face_fallback_plus_update_clamp\",\n";
    f << "  \"wall_boundary_output_semantics\": \"boundary_value\",\n";
    f << "  \"true_bdf2_inner_loop\": "
      << (case_.run_control.type == RunType::Transient ? "true" : "false")
      << ",\n";
    f << "  \"typical_inner_iterations\": " << stats.mean_inner_iterations
      << ",\n";
    f << "  \"min_inner_iterations\": "
      << case_.run_control.min_inner_iterations << ",\n";
    f << "  \"max_inner_iterations\": "
      << case_.run_control.max_inner_iterations << ",\n";
    f << "  \"observed_min_inner_iterations\": "
      << stats.min_inner_iterations << ",\n";
    f << "  \"observed_max_inner_iterations\": "
      << stats.max_inner_iterations << ",\n";
    f << "  \"inner_residual_reduction_target\": "
      << case_.run_control.inner_residual_reduction_target
      << ",\n";
    f << "  \"inner_target_misses\": " << stats.inner_target_misses << ",\n";
    f << "  \"inner_target_converged_fraction\": "
      << stats.inner_target_converged_fraction << ",\n";
    f << "  \"last_inner_residual_ratio\": "
      << stats.last_inner_residual_ratio << ",\n";
    f << "  \"start_time_utc\": \"" << iso_time() << "\",\n";
    f << "  \"end_time_utc\": \"" << iso_time() << "\",\n";
    f << "  \"completed\": " << (stats.completed ? "true" : "false") << ",\n";
    f << "  \"convergence_status\": \"" << stats.convergence_status
      << "\"\n";
    f << "}\n";
}

void Solver::write_run_status(const std::string& dir, const RunStats& stats,
                              const std::string& command) {
    if (rank_ != 0) return;
    std::ofstream f(dir + "/run_status.json");
    f << "{\n";
    f << "  \"case_id\": \"" << case_.case_id << "\",\n";
    f << "  \"command\": \"" << command << "\",\n";
    f << "  \"mpi_ranks\": " << nranks_ << ",\n";
    f << "  \"wall_time_seconds\": " << stats.wall_time_seconds << ",\n";
    f << "  \"final_step\": " << stats.final_step << ",\n";
    f << "  \"final_physical_time\": " << stats.final_physical_time << ",\n";
    f << "  \"convergence_status\": \"" << stats.convergence_status << "\",\n";
    f << "  \"residual_reduction_orders\": "
      << stats.residual_reduction_orders << ",\n";
    f << "  \"notes\": \"generated by cfd-fv2d\"\n";
    f << "}\n";
}

}  // namespace cfd
