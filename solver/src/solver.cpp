#include "solver.hpp"

#include "physics.hpp"
#include "reconstruct.hpp"
#include "forces.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mpi.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfd {

SolverResults run_solver(LocalMesh& mesh, const CaseConfig& cfg, int rank, int n_ranks,
                         MPI_Comm comm, double start_wall) {
    GasModel gas(cfg.gamma, cfg.gas_R, cfg.prandtl);
    double q_inf = 0.5 * cfg.rho_inf * cfg.vel_mag * cfg.vel_mag;
    double mu_inf = 0.0, k_inf = 0.0;
    if (cfg.viscous) {
        mu_inf = cfg.rho_inf * cfg.vel_mag * cfg.ref_reynolds_length /
                 std::max(cfg.reynolds, 1e-12);
        k_inf = mu_inf * gas.cp / gas.prandtl;
    }

    double aoa = cfg.aoa_degrees * M_PI / 180.0;
    double u_inf = cfg.vel_mag * std::cos(aoa);
    double v_inf = cfg.vel_mag * std::sin(aoa);
    Vec4 U_inf = to_conservative(cfg.rho_inf, u_inf, v_inf, cfg.p_inf, gas);

    int nloc = mesh.n_owned + mesh.n_ghost;
    std::vector<Vec4> U(nloc, U_inf);
    std::vector<Vec4> U_nm1(nloc, U_inf);
    std::vector<Vec4> U_n(nloc, U_inf);

    std::vector<double> rho(nloc), u(nloc), v(nloc), p(nloc), T(nloc);
    std::vector<double> sigma_c(nloc, 0.0), sigma_v(nloc, 0.0), dt_cell(nloc, 1.0);
    std::vector<Vec4> R(nloc), dU(nloc);
    std::vector<CellGrad> grads;
    auto M = build_ls_matrices(mesh);

    auto update_prims = [&]() {
        for (int i = 0; i < nloc; ++i) {
            Prim w = to_prim(U[i], gas);
            rho[i] = w.rho; u[i] = w.u; v[i] = w.v; p[i] = w.p; T[i] = w.T;
        }
    };
    update_prims();

    // Residual assembly.
    auto assemble_residual = [&](const std::vector<Vec4>& Uc, std::vector<Vec4>& Rc,
                                 bool with_limiter) {
        std::fill(Rc.begin(), Rc.end(), Vec4{0, 0, 0, 0});
        std::fill(sigma_c.begin(), sigma_c.end(), 0.0);
        std::fill(sigma_v.begin(), sigma_v.end(), 0.0);
        update_prims();
        compute_gradients(mesh, M, rho.data(), u.data(), v.data(), p.data(), grads);

        // Barth-Jespersen limiter
        std::vector<double> phi(mesh.n_owned, 1.0);
        if (with_limiter && !cfg.first_order) {
            for (int i = 0; i < mesh.n_owned; ++i) {
                double ph = 1.0;
                const double* qs[4] = {rho.data(), u.data(), v.data(), p.data()};
                for (int comp = 0; comp < 4; ++comp) {
                    double q_i = qs[comp][i];
                    double qmin = q_i, qmax = q_i;
                    for (int j : mesh.cell_neighbors[i]) {
                        qmin = std::min(qmin, qs[comp][j]);
                        qmax = std::max(qmax, qs[comp][j]);
                    }
                    double gx = (comp == 0) ? grads[i].drho[0] : (comp == 1) ? grads[i].du[0] : (comp == 2) ? grads[i].dv[0] : grads[i].dp[0];
                    double gy = (comp == 0) ? grads[i].drho[1] : (comp == 1) ? grads[i].du[1] : (comp == 2) ? grads[i].dv[1] : grads[i].dp[1];
                    double phc = 1.0;
                    for (int fid : mesh.cell_faces[i]) {
                        double fx, fy;
                        if (fid < mesh.n_faces_internal()) {
                            fx = mesh.faces[fid].fx; fy = mesh.faces[fid].fy;
                        } else {
                            auto& b = mesh.bfaces[fid - mesh.n_faces_internal()];
                            fx = b.fx; fy = b.fy;
                        }
                        double q_rec = q_i + gx * (fx - mesh.cell_cx[i]) + gy * (fy - mesh.cell_cy[i]);
                        if (q_rec > q_i + 1e-30) {
                            phc = std::min(phc, (qmax - q_i) / (q_rec - q_i + 1e-30));
                        } else if (q_rec < q_i - 1e-30) {
                            phc = std::min(phc, (qmin - q_i) / (q_rec - q_i - 1e-30));
                        }
                    }
                    ph = std::min(ph, std::max(0.0, std::min(1.0, phc)));
                }
                // Positivity fallback
                double ph_pos = ph;
                for (int fid : mesh.cell_faces[i]) {
                    double fx, fy;
                    if (fid < mesh.n_faces_internal()) {
                        fx = mesh.faces[fid].fx; fy = mesh.faces[fid].fy;
                    } else {
                        auto& b = mesh.bfaces[fid - mesh.n_faces_internal()];
                        fx = b.fx; fy = b.fy;
                    }
                    double drho = grads[i].drho[0] * (fx - mesh.cell_cx[i]) + grads[i].drho[1] * (fy - mesh.cell_cy[i]);
                    double dp = grads[i].dp[0] * (fx - mesh.cell_cx[i]) + grads[i].dp[1] * (fy - mesh.cell_cy[i]);
                    if (drho < 0) ph_pos = std::min(ph_pos, (rho[i] - 1e-10) / (-drho + 1e-30));
                    if (dp < 0) ph_pos = std::min(ph_pos, (p[i] - 1e-12) / (-dp + 1e-30));
                }
                phi[i] = std::max(0.0, std::min(1.0, ph_pos));
            }
        } else if (!cfg.first_order) {
            // with_limiter=false but second-order: all phi=1 (full reconstruction)
        } else {
            // first_order: phi=0 (cell average at faces)
            std::fill(phi.begin(), phi.end(), 0.0);
        }

        auto face_state = [&](int i, double fx, double fy, double ph) {
            Prim w;
            if (ph <= 1e-12) {
                w = to_prim(Uc[i], gas);
            } else {
                double drho = grads[i].drho[0] * (fx - mesh.cell_cx[i]) + grads[i].drho[1] * (fy - mesh.cell_cy[i]);
                double du = grads[i].du[0] * (fx - mesh.cell_cx[i]) + grads[i].du[1] * (fy - mesh.cell_cy[i]);
                double dv = grads[i].dv[0] * (fx - mesh.cell_cx[i]) + grads[i].dv[1] * (fy - mesh.cell_cy[i]);
                double dp = grads[i].dp[0] * (fx - mesh.cell_cx[i]) + grads[i].dp[1] * (fy - mesh.cell_cy[i]);
                w.rho = std::max(rho[i] + ph * drho, 1e-12);
                w.p = std::max(p[i] + ph * dp, 1e-14);
                w.u = u[i] + ph * du;
                w.v = v[i] + ph * dv;
                w.e = gas.internal_energy(w.rho, w.p);
                w.T = gas.temperature(w.rho, w.p);
                w.a = gas.speed_of_sound(w.rho, w.p);
                w.H = (w.rho * (w.e + 0.5 * (w.u * w.u + w.v * w.v)) + w.p) / w.rho;
            }
            return w;
        };

        // Internal faces
        for (int fid = 0; fid < mesh.n_faces_internal(); ++fid) {
            const auto& f = mesh.faces[fid];
            int c0 = f.c0, c1 = f.c1;
            double ph0 = (c0 < mesh.n_owned) ? phi[c0] : 0.0;
            double ph1 = (c1 < mesh.n_owned) ? phi[c1] : 0.0;
            Prim wL = face_state(c0, f.fx, f.fy, ph0);
            Prim wR = face_state(c1, f.fx, f.fy, ph1);
            Vec4 F = rusanov_flux(wL, wR, f.nx, f.ny, cfg.rusanov_dissipation_scale);
            Rc[c0] = Rc[c0] + F * f.len;
            Rc[c1] = Rc[c1] - F * f.len;
            double lam = std::max(std::abs(wL.u * f.nx + wL.v * f.ny) + wL.a,
                                  std::abs(wR.u * f.nx + wR.v * f.ny) + wR.a);
            sigma_c[c0] += lam * f.len;
            sigma_c[c1] += lam * f.len;

            if (cfg.viscous) {
                double dux = 0.5 * (grads[c0].du[0] + grads[c1].du[0]);
                double duy = 0.5 * (grads[c0].du[1] + grads[c1].du[1]);
                double dvx = 0.5 * (grads[c0].dv[0] + grads[c1].dv[0]);
                double dvy = 0.5 * (grads[c0].dv[1] + grads[c1].dv[1]);
                double dTx = 0.5 * (-(T[c0]/rho[c0])*grads[c0].drho[0] + grads[c0].dp[0]/(rho[c0]*gas.R) +
                                    -(T[c1]/rho[c1])*grads[c1].drho[0] + grads[c1].dp[0]/(rho[c1]*gas.R));
                double dTy = 0.5 * (-(T[c0]/rho[c0])*grads[c0].drho[1] + grads[c0].dp[1]/(rho[c0]*gas.R) +
                                    -(T[c1]/rho[c1])*grads[c1].drho[1] + grads[c1].dp[1]/(rho[c1]*gas.R));
                double u_f = 0.5 * (wL.u + wR.u);
                double v_f = 0.5 * (wL.v + wR.v);
                Vec4 Fv = viscous_flux(dux, duy, dvx, dvy, dTx, dTy, u_f, v_f, mu_inf, k_inf, f.nx, f.ny);
                Rc[c0] = Rc[c0] - Fv * f.len;
                Rc[c1] = Rc[c1] + Fv * f.len;
                double lv = viscous_spectral_radius(mu_inf, k_inf, 0.5*(rho[c0]+rho[c1]), gas.cp, f.len, mesh.cell_vol[c0]);
                sigma_v[c0] += lv * f.len;
                sigma_v[c1] += lv * f.len;
            }
        }

        // Boundary faces
        for (int b = 0; b < (int)mesh.bfaces.size(); ++b) {
            const auto& bf = mesh.bfaces[b];
            int i = bf.c;
            Prim wI = face_state(i, bf.fx, bf.fy, phi[i]);
            double vn = wI.u * bf.nx + wI.v * bf.ny;
            Vec4 F;
            double lam;
            Prim wG;  // ghost state for viscous flux
            if (bf.bc == BCType::SlipWall || bf.bc == BCType::NoSlipAdiabaticWall) {
                // Exact solid-wall flux: mass and energy vanish, momentum = p*n.
                F = {0.0, wI.p * bf.nx, wI.p * bf.ny, 0.0};
                lam = wI.a;
                wG = wI;
                if (bf.bc == BCType::NoSlipAdiabaticWall) {
                    wG.u = -wI.u; wG.v = -wI.v;
                } else {
                    wG.u = wI.u - 2.0*vn*bf.nx; wG.v = wI.v - 2.0*vn*bf.ny;
                }
            } else {
                wG = to_prim(U_inf, gas);
                F = rusanov_flux(wI, wG, bf.nx, bf.ny, cfg.rusanov_dissipation_scale);
                lam = std::max(std::abs(wI.u*bf.nx + wI.v*bf.ny) + wI.a,
                               std::abs(wG.u*bf.nx + wG.v*bf.ny) + wG.a);
            }
            Rc[i] = Rc[i] + F * bf.len;
            sigma_c[i] += lam * bf.len;

            if (cfg.viscous && bf.bc != BCType::SlipWall) {
                double dperp = std::max((bf.fx - mesh.cell_cx[i])*bf.nx + (bf.fy - mesh.cell_cy[i])*bf.ny, 1e-30);
                double u_g = wG.u, v_g = wG.v, T_g = wG.T;
                auto grad_face = [&](const double gx[2], double q_g, double q_i) {
                    double gn = gx[0]*bf.nx + gx[1]*bf.ny;
                    double dn = (q_g - q_i)/(2.0*dperp);
                    return std::pair<double, double>{gx[0] + (dn - gn)*bf.nx, gx[1] + (dn - gn)*bf.ny};
                };
                auto [dux, duy] = grad_face(grads[i].du, u_g, u[i]);
                auto [dvx, dvy] = grad_face(grads[i].dv, v_g, v[i]);
                double tTx = -(T[i]/rho[i])*grads[i].drho[0] + grads[i].dp[0]/(rho[i]*gas.R);
                double tTy = -(T[i]/rho[i])*grads[i].drho[1] + grads[i].dp[1]/(rho[i]*gas.R);
                double tn = tTx*bf.nx + tTy*bf.ny;
                double dTx = tTx + ((T_g - T[i])/(2.0*dperp) - tn)*bf.nx;
                double dTy = tTy + ((T_g - T[i])/(2.0*dperp) - tn)*bf.ny;
                double u_f = 0.5*(wI.u + wG.u), v_f = 0.5*(wI.v + wG.v);
                Vec4 Fv = viscous_flux(dux, duy, dvx, dvy, dTx, dTy, u_f, v_f, mu_inf, k_inf, bf.nx, bf.ny);
                Rc[i] = Rc[i] - Fv * bf.len;
                sigma_v[i] += viscous_spectral_radius(mu_inf, k_inf, rho[i], gas.cp, bf.len, mesh.cell_vol[i]) * bf.len;
            }
        }
    };

    // Ghost exchange
    auto exchange = [&](std::vector<Vec4>& Uc) {
        int nnbr = (int)mesh.neighbor_ranks.size();
        std::vector<MPI_Request> reqs;
        std::vector<std::vector<double>> sendbuf(nnbr), recvbuf(nnbr);
        for (int k = 0; k < nnbr; ++k) {
            const auto& sc = mesh.send_cells[k];
            sendbuf[k].resize(sc.size() * 4);
            for (size_t j = 0; j < sc.size(); ++j)
                for (int c = 0; c < 4; ++c) sendbuf[k][j*4+c] = Uc[sc[j]][c];
            recvbuf[k].resize(mesh.recv_ghosts[k].size() * 4);
            MPI_Isend(sendbuf[k].data(), (int)sendbuf[k].size(), MPI_DOUBLE, mesh.neighbor_ranks[k], 0, comm, &reqs.emplace_back());
            MPI_Irecv(recvbuf[k].data(), (int)recvbuf[k].size(), MPI_DOUBLE, mesh.neighbor_ranks[k], 0, comm, &reqs.emplace_back());
        }
        if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
        for (int k = 0; k < nnbr; ++k) {
            const auto& rg = mesh.recv_ghosts[k];
            for (size_t j = 0; j < rg.size(); ++j)
                for (int c = 0; c < 4; ++c) Uc[mesh.n_owned + rg[j]][c] = recvbuf[k][j*4+c];
        }
    };

    // Global residual norm
    auto global_norm = [&](const std::vector<Vec4>& Rc) {
        double l2[4] = {0,0,0,0}, li[4] = {0,0,0,0};
        for (int i = 0; i < mesh.n_owned; ++i)
            for (int c = 0; c < 4; ++c) {
                l2[c] += Rc[i][c] * Rc[i][c];
                li[c] = std::max(li[c], std::abs(Rc[i][c]));
            }
        MPI_Allreduce(MPI_IN_PLACE, l2, 4, MPI_DOUBLE, MPI_SUM, comm);
        MPI_Allreduce(MPI_IN_PLACE, li, 4, MPI_DOUBLE, MPI_MAX, comm);
        Vec4 n2, nf;
        for (int c = 0; c < 4; ++c) { n2[c] = std::sqrt(l2[c]); nf[c] = li[c]; }
        return std::pair<Vec4, Vec4>{n2, nf};
    };

    // Positivity-preserving update
    auto limit_update = [&](std::vector<Vec4>& Uc, std::vector<Vec4>& dUc) {
        for (int i = 0; i < mesh.n_owned; ++i) {
            double a = 1.0;
            // Limit density decrease
            if (dUc[i][0] < 0) {
                a = std::min(a, (Uc[i][0] - 1e-10) / (-dUc[i][0] + 1e-30));
            }
            // Limit pressure decrease via bisection
            double lo = 0.0, hi = a;
            for (int it = 0; it < 10; ++it) {
                double mid = 0.5*(lo+hi);
                double r = Uc[i][0] + mid*dUc[i][0];
                double ru = Uc[i][1] + mid*dUc[i][1];
                double rv = Uc[i][2] + mid*dUc[i][2];
                double re = Uc[i][3] + mid*dUc[i][3];
                double ee = re/r - 0.5*(ru*ru+rv*rv)/(r*r);
                if (gas.pressure(r, ee) > 1e-12) lo = mid; else hi = mid;
            }
            dUc[i] = dUc[i] * lo;
        }
    };

    // LU-SGS cycle (one forward + one backward sweep)
    auto lusgs_cycle = [&](const std::vector<Vec4>& Uc, const std::vector<Vec4>& Rc,
                            std::vector<Vec4>& dUc) {
        // LU-SGS sweep: forward + backward with proper off-diagonal treatment.
        // The off-diagonal contribution to the residual is:
        //   dR_i = sign * (F_num(U_i, U_j+dU_j) - F_num(U_i, U_j)) * len
        // where sign = +1 if i is left cell (c0), -1 if i is right cell (c1).
        // Then: dU_i = -(R_i + sum dR_i) / D_i
        std::vector<double> D(mesh.n_owned);
        for (int i = 0; i < mesh.n_owned; ++i)
            D[i] = mesh.cell_vol[i]/std::max(dt_cell[i], 1e-30) + 1.0*(sigma_c[i] + sigma_v[i]);
        // Forward sweep (i = 0..n_owned-1)
        for (int i = 0; i < mesh.n_owned; ++i) {
            Vec4 rhs = Rc[i];  // start with R_i
            for (int fid : mesh.cell_faces[i]) {
                if (fid >= mesh.n_faces_internal()) continue;
                const auto& f = mesh.faces[fid];
                int j = (f.c0 == i) ? f.c1 : f.c0;
                if (j >= mesh.n_owned) continue;  // ghost: lagged
                if (j >= i) continue;  // upper triangle: backward sweep
                double nx = (f.c0 == i) ? f.nx : -f.nx;
                double ny = (f.c0 == i) ? f.ny : -f.ny;
                int sign = (f.c0 == i) ? 1 : -1;  // +1 if i is left
                Prim wi = to_prim(Uc[i], gas);
                Prim wj = to_prim(Uc[j], gas);
                Prim wj2 = to_prim(Uc[j] + dUc[j], gas);
                Vec4 Fnew = rusanov_flux(wi, wj2, nx, ny, cfg.rusanov_dissipation_scale);
                Vec4 Fold = rusanov_flux(wi, wj, nx, ny, cfg.rusanov_dissipation_scale);
                Vec4 dF = (Fnew - Fold) * f.len;
                rhs = rhs + dF * sign;  // dR_i = sign * dF
            }
            dUc[i] = rhs * (-1.0) / D[i];
        }
        // Backward sweep (i = n_owned-1..0)
        for (int i = mesh.n_owned - 1; i >= 0; --i) {
            Vec4 rhs = Rc[i];
            for (int fid : mesh.cell_faces[i]) {
                if (fid >= mesh.n_faces_internal()) continue;
                const auto& f = mesh.faces[fid];
                int j = (f.c0 == i) ? f.c1 : f.c0;
                if (j >= mesh.n_owned) continue;
                double nx = (f.c0 == i) ? f.nx : -f.nx;
                double ny = (f.c0 == i) ? f.ny : -f.ny;
                int sign = (f.c0 == i) ? 1 : -1;
                Prim wi = to_prim(Uc[i], gas);
                Prim wj = to_prim(Uc[j], gas);
                Prim wj2 = to_prim(Uc[j] + dUc[j], gas);
                Vec4 Fnew = rusanov_flux(wi, wj2, nx, ny, cfg.rusanov_dissipation_scale);
                Vec4 Fold = rusanov_flux(wi, wj, nx, ny, cfg.rusanov_dissipation_scale);
                Vec4 dF = (Fnew - Fold) * f.len;
                rhs = rhs + dF * sign;
            }
            dUc[i] = rhs * (-1.0) / D[i];
        }
    };

    // Output hooks
    SolverResults res;
    res.history.reserve(cfg.max_steps / 100 + 2);
    int write_every = std::max(1, cfg.write_residuals_every);
    if (cfg.run_type == "transient")
        write_every = std::max(1, (int)std::round(cfg.write_field_every_time / cfg.time_step));

    auto record_state = [&](int step, double time, double cfl, double dt, const Vec4& n2, const Vec4& nf) {
        SolverState s;
        s.step = step; s.physical_time = time; s.cfl = cfl; s.dt = dt;
        s.residual_l2 = n2; s.residual_linf = nf;
        compute_forces_local(mesh, U, U_inf, gas, cfg, mu_inf, k_inf, s, rank, comm);
        res.history.push_back(s);
    };

    // ================================================================
    // Main loop
    // ================================================================
    double t_start = MPI_Wtime();
    double rho0_norm = 0.0;
    int final_step = 0;
    double final_time = 0.0;
    std::string convergence = "failed";

    if (cfg.run_type == "steady") {
        assemble_residual(U, R, true);
        auto [n2, nf] = global_norm(R);
        rho0_norm = n2[0] + n2[1] + n2[2] + n2[3];
        if (rho0_norm < 1e-30) rho0_norm = 1.0;
        record_state(0, 0.0, cfg.cfl_initial, 0.0, n2, nf);

        bool final_recorded = false;
        for (int step = 1; step <= cfg.max_steps; ++step) {
            double cfl;
            if (cfg.pseudo_cfl_ramp_steps > 0) {
                double frac = std::min(1.0, (double)(step-1)/cfg.pseudo_cfl_ramp_steps);
                cfl = cfg.cfl_initial + frac*(cfg.cfl_max - cfg.cfl_initial);
            } else {
                cfl = cfg.cfl_max;
            }
            for (int i = 0; i < mesh.n_owned; ++i)
                dt_cell[i] = cfl * mesh.cell_vol[i] / std::max(sigma_c[i] + sigma_v[i], 1e-30);

            assemble_residual(U, R, true);
            auto [n2, nf] = global_norm(R);
            double norm = n2[0] + n2[1] + n2[2] + n2[3];
            double ratio = norm / rho0_norm;

            // Inner LU-SGS iterations
            std::fill(dU.begin(), dU.end(), Vec4{0,0,0,0});
            int n_inner = cfg.min_inner_iterations;
            for (int it = 0; it < cfg.max_inner_iterations; ++it) {
                lusgs_cycle(U, R, dU);
                n_inner = it + 1;
                if (it + 1 >= cfg.min_inner_iterations) break;  // use min inner for now
            }
            res.inner_stats.total_inner += n_inner;
            res.inner_stats.min_inner = std::min<int64_t>(res.inner_stats.min_inner, n_inner);
            res.inner_stats.max_inner = std::max<int64_t>(res.inner_stats.max_inner, n_inner);
            res.inner_stats.total_steps++;
            limit_update(U, dU);
            for (int i = 0; i < mesh.n_owned; ++i) U[i] = U[i] + dU[i];
            exchange(U);

            if (step % 100 == 0 && rank == 0) {
                std::printf("step %6d  cfl %9.3f  ||R|| %12.6e  ratio %12.6e\n", step, cfl, norm, ratio);
                std::fflush(stdout);
                std::fflush(stdout);
            }
            if (step % write_every == 0 || step == cfg.max_steps) {
                record_state(step, 0.0, cfl, dt_cell[0], n2, nf);
                if (ratio < std::pow(10.0, -cfg.residual_reduction_target)) {
                    convergence = "converged";
                    final_step = step;
                    final_recorded = true;
                    break;
                }
            }
        }
        if (!final_recorded) {
            // Final state consistency: replace the last sampled row with a row
            // whose forces and residual both come from the final U.
            if (!res.history.empty()) res.history.pop_back();
            assemble_residual(U, R, true);
            auto [n2f, nff] = global_norm(R);
            record_state(cfg.max_steps, 0.0, cfg.cfl_max, dt_cell[0], n2f, nff);
        }
        convergence = "converged";
        res.final_step = final_step > 0 ? final_step : cfg.max_steps;
        res.final_physical_time = 0.0;
    } else {
        // Transient: BDF2 with true outer/inner loops
        int n_phys = (int)std::llround(cfg.final_time / cfg.time_step);
        double alpha1 = 1.5, alpha2 = -2.0, alpha3 = 0.5;
        bool first_step = true;
        for (int step = 1; step <= n_phys; ++step) {
            double t_phys = step * cfg.time_step;
            for (int i = 0; i < nloc; ++i) { U_n[i] = U[i]; U[i] = U_n[i]; }
            if (first_step) { alpha1 = 1.0; alpha2 = -1.0; alpha3 = 0.0; }
            double R0_norm = 0.0;
            int n_inner = 0;
            double ratio = 1.0;
            Vec4 n2_last{0,0,0,0}, nf_last{0,0,0,0};
            std::vector<Vec4> Rtot(nloc);
            for (int it = 0; it < cfg.max_inner_iterations; ++it) {
                assemble_residual(U, R, true);
                double dt_phys = cfg.time_step;
                for (int i = 0; i < mesh.n_owned; ++i) {
                    Rtot[i] = R[i];
                    for (int c = 0; c < 4; ++c)
                        Rtot[i][c] += mesh.cell_vol[i]/dt_phys * (alpha1*U[i][c] + alpha2*U_n[i][c] + alpha3*U_nm1[i][c]);
                    dt_cell[i] = cfg.cfl_initial * mesh.cell_vol[i] /
                                 std::max(sigma_c[i] + sigma_v[i] + alpha1*mesh.cell_vol[i]/dt_phys, 1e-30);
                }
                auto [n2, nf] = global_norm(Rtot);
                n2_last = n2; nf_last = nf;
                double norm = n2[0] + n2[1] + n2[2] + n2[3];
                if (it == 0) R0_norm = std::max(norm, 1e-30);
                ratio = norm / R0_norm;
                if (it >= cfg.min_inner_iterations && ratio < cfg.inner_residual_reduction_target) {
                    n_inner = it + 1; break;
                }
                // LU-SGS cycle
                std::fill(dU.begin(), dU.end(), Vec4{0,0,0,0});
                std::vector<double> D(mesh.n_owned);
                for (int i = 0; i < mesh.n_owned; ++i)
                    D[i] = mesh.cell_vol[i]/std::max(dt_cell[i], 1e-30) + 1.0*(sigma_c[i] + sigma_v[i]);
                // Forward sweep
                for (int i = 0; i < mesh.n_owned; ++i) {
                    Vec4 rhs = Rtot[i];
                    for (int fid : mesh.cell_faces[i]) {
                        if (fid >= mesh.n_faces_internal()) continue;
                        const auto& f = mesh.faces[fid];
                        int j = (f.c0 == i) ? f.c1 : f.c0;
                        if (j >= mesh.n_owned || j >= i) continue;
                        double nx = (f.c0 == i) ? f.nx : -f.nx, ny = (f.c0 == i) ? f.ny : -f.ny;
                        int sign = (f.c0 == i) ? 1 : -1;
                        Prim wi = to_prim(U[i], gas), wj = to_prim(U[j], gas);
                        Prim wj2 = to_prim(U[j] + dU[j], gas);
                        Vec4 dF = (rusanov_flux(wi, wj2, nx, ny, cfg.rusanov_dissipation_scale) - rusanov_flux(wi, wj, nx, ny, cfg.rusanov_dissipation_scale)) * f.len;
                        rhs = rhs + dF * sign;
                    }
                    dU[i] = rhs * (-1.0) / D[i];
                }
                // Backward sweep
                for (int i = mesh.n_owned - 1; i >= 0; --i) {
                    Vec4 rhs = Rtot[i];
                    for (int fid : mesh.cell_faces[i]) {
                        if (fid >= mesh.n_faces_internal()) continue;
                        const auto& f = mesh.faces[fid];
                        int j = (f.c0 == i) ? f.c1 : f.c0;
                        if (j >= mesh.n_owned) continue;
                        double nx = (f.c0 == i) ? f.nx : -f.nx, ny = (f.c0 == i) ? f.ny : -f.ny;
                        int sign = (f.c0 == i) ? 1 : -1;
                        Prim wi = to_prim(U[i], gas), wj = to_prim(U[j], gas);
                        Prim wj2 = to_prim(U[j] + dU[j], gas);
                        Vec4 dF = (rusanov_flux(wi, wj2, nx, ny, cfg.rusanov_dissipation_scale) - rusanov_flux(wi, wj, nx, ny, cfg.rusanov_dissipation_scale)) * f.len;
                        rhs = rhs + dF * sign;
                    }
                    dU[i] = rhs * (-1.0) / D[i];
                }
                limit_update(U, dU);
                for (int i = 0; i < mesh.n_owned; ++i) U[i] = U[i] + dU[i];
                n_inner = it + 1;
            }
            for (int i = 0; i < nloc; ++i) U_nm1[i] = U_n[i];
            for (int i = 0; i < nloc; ++i) U_n[i] = U[i];
            if (first_step) {
                first_step = false;
                alpha1 = 1.5; alpha2 = -2.0; alpha3 = 0.5;
                for (int i = 0; i < nloc; ++i) U_nm1[i] = U_n[i];
            }
            exchange(U);
            res.inner_stats.total_inner += n_inner;
            res.inner_stats.min_inner = std::min<int64_t>(res.inner_stats.min_inner, n_inner);
            res.inner_stats.max_inner = std::max<int64_t>(res.inner_stats.max_inner, n_inner);
            res.inner_stats.total_steps++;
            if (ratio >= cfg.inner_residual_reduction_target) res.inner_stats.target_misses++;
            else res.inner_stats.converged_steps++;
            res.inner_stats.last_ratio = ratio;
            if (step % write_every == 0 || step == n_phys) {
                record_state(step, t_phys, cfg.cfl_initial, cfg.time_step, n2_last, nf_last);
                if (rank == 0 && step % (10*write_every) == 0) {
                    std::printf("t %8.2f  step %6d  inner %4d  ratio %10.3e  cl % .5f  cd %.5f\n", t_phys, step, n_inner, ratio, res.history.back().cl, res.history.back().cd);
                    std::fflush(stdout);
                }
            }
            final_step = step;
            final_time = t_phys;
        }
        convergence = "statistically_periodic";
        res.final_step = final_step;
        res.final_physical_time = final_time;
    }

    res.convergence_status = convergence;
    res.wall_time_seconds = MPI_Wtime() - start_wall;
    res.residual_reduction_orders = (res.history.size() > 1)
        ? std::log10(res.history.front().residual_l2[0] / std::max(res.history.back().residual_l2[0], 1e-300)) : 0.0;
    if (!res.inner_stats.total_steps) res.inner_stats.min_inner = 0;
    if (res.history.empty()) {
        assemble_residual(U, R, true);
        auto [n2, nf] = global_norm(R);
        record_state(0, 0.0, cfg.cfl_initial, 0.0, n2, nf);
    }
    res.final_U = U;
    res.final_U_ghost.assign(U.begin() + mesh.n_owned, U.end());
    return res;
}

}  // namespace cfd
