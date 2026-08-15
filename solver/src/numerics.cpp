#include "numerics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

namespace cfd {

namespace {

// ---------------------------------------------------------------------------
// Exact 4x4 Jacobian of the inviscid flux F(U)·n wrt conservative U.
// ---------------------------------------------------------------------------
Mat4 euler_jacobian(const State& U, Real nx, Real ny, Real gamma, Real R) {
    const Prims q = prims_from_state(U, gamma, R);
    const Real u = q.u, v = q.v;
    const Real V2 = u * u + v * v;
    const Real gm1 = gamma - 1.0;
    const Real H = q.e + q.p / q.rho + 0.5 * V2;

    Mat4 Ax, Ay;
    Ax << 0.0, 1.0, 0.0, 0.0,
        0.5 * ((gamma - 3.0) * u * u + gm1 * v * v), (3.0 - gamma) * u,
        -gm1 * v, gm1,
        -u * v, v, u, 0.0,
        u * (0.5 * gm1 * V2 - H), H - gm1 * u * u, -gm1 * u * v, gamma * u;
    Ay << 0.0, 0.0, 1.0, 0.0,
        -u * v, v, u, 0.0,
        0.5 * (gm1 * u * u + (gamma - 3.0) * v * v), -gm1 * u,
        (3.0 - gamma) * v, gm1,
        v * (0.5 * gm1 * V2 - H), -gm1 * u * v, H - gm1 * v * v, gamma * v;
    return nx * Ax + ny * Ay;
}

// Jacobian of the impermeable wall flux (0, p nx, p ny, 0) wrt interior state.
Mat4 wall_jacobian(const State& U, Real nx, Real ny, Real gamma, Real R) {
    const Prims q = prims_from_state(U, gamma, R);
    const Real V2 = q.u * q.u + q.v * q.v;
    const Real gm1 = gamma - 1.0;
    State dpdU;
    dpdU << 0.5 * gm1 * V2, -gm1 * q.u, -gm1 * q.v, gm1;
    Mat4 J = Mat4::Zero();
    J.row(1) = nx * dpdU.transpose();
    J.row(2) = ny * dpdU.transpose();
    return J;
}

// Jacobian of the no-slip wall viscous traction flux
// F = (0, -tau·n, 0) wrt the interior conservative state.  The traction is
// built from the mirrored-ghost construction with wall distance d.
Mat4 wall_traction_jacobian(const State& U, Real nx, Real ny, Real S,
                            Real d, Real mu, Real gamma, Real R) {
    const Prims q = prims_from_state(U, gamma, R);
    const Real rho = std::max(q.rho, 1e-30);
    const Real c = -mu * S / std::max(d, 1e-14);
    const Real a11 = -((4.0 / 3.0) * nx * nx + ny * ny);
    const Real a12 = -(1.0 / 3.0) * nx * ny;
    const Real a22 = -(nx * nx + (4.0 / 3.0) * ny * ny);
    Mat4 J = Mat4::Zero();
    J(1, 0) = c * (a11 * (-q.u / rho) + a12 * (-q.v / rho));
    J(1, 1) = c * a11 / rho;
    J(1, 2) = c * a12 / rho;
    J(2, 0) = c * (a12 * (-q.u / rho) + a22 * (-q.v / rho));
    J(2, 1) = c * a12 / rho;
    J(2, 2) = c * a22 / rho;
    return J;
}

// Dense 4x4 solve with partial pivoting.
State solve4(Mat4 A, State b) {
    for (int pivot = 0; pivot < 4; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 4; ++row) {
            if (std::abs(A(row, pivot)) > std::abs(A(best, pivot))) best = row;
        }
        if (best != pivot) {
            A.row(pivot).swap(A.row(best));
            std::swap(b[pivot], b[best]);
        }
        const Real diag = A(pivot, pivot);
        if (!std::isfinite(diag) || std::abs(diag) < 1e-30) {
            throw std::runtime_error("singular implicit 4x4 block");
        }
        for (int row = pivot + 1; row < 4; ++row) {
            const Real mult = A(row, pivot) / diag;
            if (mult == 0.0) continue;
            A(row, pivot) = 0.0;
            for (int col = pivot + 1; col < 4; ++col) {
                A(row, col) -= mult * A(pivot, col);
            }
            b[row] -= mult * b[pivot];
        }
    }
    State x;
    for (int rev = 3; rev >= 0; --rev) {
        Real val = b[rev];
        for (int col = rev + 1; col < 4; ++col) val -= A(rev, col) * x[col];
        x[rev] = val / A(rev, rev);
    }
    return x;
}

// No-slip wall stress from the mirrored-ghost construction:
// du/dn = -u_cell/d, grad = (du/dn) n^T.
void wall_stress(Real u, Real v, Real nx, Real ny, Real d, Real mu,
                 Real& tau_xx, Real& tau_yy, Real& tau_xy) {
    const Real ux = -u * nx / d;
    const Real uy = -u * ny / d;
    const Real vx = -v * nx / d;
    const Real vy = -v * ny / d;
    const Real div = ux + vy;
    tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
    tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
    tau_xy = mu * (uy + vx);
}

bool state_ok(const State& s, Real gamma) {
    if (!(s[0] > 0.0)) return false;
    const Real ke = 0.5 * (s[1] * s[1] + s[2] * s[2]) / s[0];
    return (gamma - 1.0) * (s[3] - ke) > 0.0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Solver::Solver(const CaseInput& ci, const LocalMesh& lm, const GlobalMesh& gm,
               const Partition& part, MPI_Comm comm, int rank, int n_ranks)
    : ci_(ci), lm_(lm), gm_(gm), part_(part),
      comm_(comm), rank_(rank), n_ranks_(n_ranks) {

    const Index nf = (Index)lm_.faces.size();
    face_bc_.assign(nf, BcKind::Interior);
    face_len_.assign(nf, 0.0);
    face_tag_.assign(nf, "");

    // Map family tag -> BC from the case file
    std::map<Index, BcKind> tag_bc;
    for (const auto& [fam, type] : ci_.boundary_conditions) {
        auto it = gm_.tag_of_family.find(fam);
        if (it == gm_.tag_of_family.end()) {
            throw std::runtime_error("case boundary family '" + fam +
                                     "' not found in mesh boundary families");
        }
        tag_bc[it->second] = bc_from_name(type);
    }

    for (Index f = 0; f < nf; f++) {
        const Face& face = lm_.faces[f];
        face_len_[f] = face.normal.norm();
        if (face.bc_tag != 0) {
            auto it = tag_bc.find(face.bc_tag);
            face_bc_[f] = (it != tag_bc.end()) ? it->second : BcKind::Farfield;
            for (const auto& [fam, tag] : gm_.tag_of_family) {
                if (tag == face.bc_tag) face_tag_[f] = fam;
            }
            if (face_tag_[f].empty()) face_tag_[f] = "wall";
        }
    }

    U_.assign(lm_.n_total, ci_.freestream);

    vols_.resize(lm_.n_total, 1.0);
    cents_.resize(lm_.n_total);
    for (Index i = 0; i < lm_.n_owned; i++) {
        vols_[i] = lm_.owned[i].volume;
        cents_[i] = lm_.owned[i].centroid;
    }
    for (Index i = 0; i < lm_.n_ghost; i++) {
        const Index gi = lm_.n_owned + i;
        vols_[gi] = lm_.ghost[i].volume;
        cents_[gi] = lm_.ghost[i].centroid;
    }
}

// ---------------------------------------------------------------------------
// Halo exchange
// ---------------------------------------------------------------------------
void Solver::exchange_state() {
    const int n_nbr = (int)lm_.neighbors.size();
    if (n_nbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<Real>> sendbufs(n_nbr), recvbufs(n_nbr);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        sendbufs[i].resize(nb.send_cells.size() * 4);
        for (size_t k = 0; k < nb.send_cells.size(); k++) {
            const Index lc = nb.send_cells[k];
            for (int c = 0; c < 4; c++) sendbufs[i][k * 4 + c] = U_[lc][c];
        }
        reqs.emplace_back();
        MPI_Isend(sendbufs[i].data(), (int)sendbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 77, comm_, &reqs.back());
    }
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        recvbufs[i].resize(nb.recv_cells.size() * 4);
        reqs.emplace_back();
        MPI_Irecv(recvbufs[i].data(), (int)recvbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 77, comm_, &reqs.back());
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        for (size_t k = 0; k < nb.recv_cells.size(); k++) {
            const Index lc = nb.recv_cells[k];
            for (int c = 0; c < 4; c++) U_[lc][c] = recvbufs[i][k * 4 + c];
        }
    }
}

void Solver::exchange_correction(std::vector<State>& corr) {
    const int n_nbr = (int)lm_.neighbors.size();
    if (n_nbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<Real>> sendbufs(n_nbr), recvbufs(n_nbr);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        sendbufs[i].resize(nb.send_cells.size() * 4);
        for (size_t k = 0; k < nb.send_cells.size(); k++) {
            const Index lc = nb.send_cells[k];
            for (int c = 0; c < 4; c++) sendbufs[i][k * 4 + c] = corr[lc][c];
        }
        reqs.emplace_back();
        MPI_Isend(sendbufs[i].data(), (int)sendbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 78, comm_, &reqs.back());
    }
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        recvbufs[i].resize(nb.recv_cells.size() * 4);
        reqs.emplace_back();
        MPI_Irecv(recvbufs[i].data(), (int)recvbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 78, comm_, &reqs.back());
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = lm_.neighbors[i];
        for (size_t k = 0; k < nb.recv_cells.size(); k++) {
            const Index lc = nb.recv_cells[k];
            for (int c = 0; c < 4; c++) corr[lc][c] = recvbufs[i][k * 4 + c];
        }
    }
}

// ---------------------------------------------------------------------------
// Local time step
// ---------------------------------------------------------------------------
void Solver::compute_local_dt(Real* dt, Real cfl) {
    const Real gamma = ci_.gamma;
    const Real R = ci_.R;
    const Real mu = ci_.mu_ref;
    const Real prandtl = ci_.prandtl;
    const bool viscous = ci_.physics_mode == "laminar";
    const Real cv = R / (gamma - 1.0);
    const Real kk = (viscous && mu > 0.0) ? mu * gamma * R / ((gamma - 1.0) * prandtl) : 0.0;

    std::vector<Real> spec(lm_.n_total, 0.0);
    for (const Face& face : lm_.faces) {
        const Index L = face.left;
        if (L < 0 || L >= lm_.n_total) continue;
        const Real len = face_len_[face.id];
        const Prims qL = prims_from_state(U_[L], gamma, R);
        const Real un = qL.u * face.normal[0] + qL.v * face.normal[1];
        spec[L] += (std::abs(un) + qL.a) * len;
        if (face.right >= 0) {
            const Prims qR = prims_from_state(U_[face.right], gamma, R);
            const Real unR = qR.u * face.normal[0] + qR.v * face.normal[1];
            spec[face.right] += (std::abs(unR) + qR.a) * len;
            if (viscous && mu > 0.0) {
                const Real dvL = std::max(mu / qL.rho, kk / (qL.rho * cv));
                const Real dvR = std::max(mu / qR.rho, kk / (qR.rho * cv));
                spec[L] += dvL * len * len / std::max(vols_[L], 1e-30);
                spec[face.right] += dvR * len * len / std::max(vols_[face.right], 1e-30);
            }
        } else if (viscous && mu > 0.0) {
            const Real dv = std::max(mu / qL.rho, kk / (qL.rho * cv));
            spec[L] += dv * len * len / std::max(vols_[L], 1e-30);
        }
    }
    for (Index i = 0; i < lm_.n_owned; i++) {
        dt[i] = cfl * lm_.owned[i].volume / std::max(spec[i], 1e-30);
    }
}

// ---------------------------------------------------------------------------
// Residual assembly
// ---------------------------------------------------------------------------
void Solver::compute_residual(std::vector<State>& R, ResNorm& norm,
                              bool is_transient, Real dt,
                              const std::vector<State>& U_n,
                              const std::vector<State>& U_nm1) {
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;
    const Real prandtl = ci_.prandtl;
    const Real mu = ci_.mu_ref;
    const bool viscous = ci_.physics_mode == "laminar";
    const Real diss = ci_.rusanov_dissipation_scale;

    std::vector<State> spatial(lm_.n_total, State::Zero());
    if (getenv("CFD_TRACE")) std::cerr << "[trace] gradients begin\n";
    GradWork gw;
    compute_gradients(*this, U_, gw);
    if (getenv("CFD_TRACE")) std::cerr << "[trace] gradients done, limiter begin\n";
    barth_limiter(*this, U_, gw);
    if (getenv("CFD_TRACE")) std::cerr << "[trace] limiter done, faces begin\n";

    Index nf_done = 0;
    for (const Face& face : lm_.faces) {
        const Index L = face.left;
        if (L < 0) continue;
        if (getenv("CFD_TRACE") && nf_done % 5000 == 0)
            std::cerr << "[trace] face " << nf_done << "/" << lm_.faces.size() << "\n";
        nf_done++;
        const Index Rc = face.right;
        const Real S = face_len_[face.id];
        if (S <= 0.0) continue;
        const Real nx = face.normal[0] / S;
        const Real ny = face.normal[1] / S;

        if (Rc >= 0) {
            // Interior face: second-order reconstruction
            Prims qL, qR;
            reconstruct_face(*this, U_, gw, face.id, qL, qR);
            const State UL = state_from_prims(qL, gamma);
            const State UR = state_from_prims(qR, gamma);
            const State flux = rusanov_flux(UL, UR, nx, ny, S, gamma, Rgas, diss);
            spatial[L] += flux;
            spatial[Rc] -= flux;

            if (viscous) {
                // Corrected central gradient: average cell gradients, strip
                // the component along the cell connector, add the exact
                // one-sided difference (phi_R - phi_L)/|dr|^2 * dr.
                const Vec2 dr = cents_[Rc] - cents_[L];
                const Real dr2 = dr.squaredNorm();
                auto corrected = [&](const Vec2& gL, const Vec2& gR,
                                     Real phiL, Real phiR) {
                    Vec2 gavg = 0.5 * (gL + gR);
                    if (dr2 > 1e-30) {
                        const Real proj = gavg.dot(dr) / dr2;
                        const Real slope = (phiR - phiL) / dr2;
                        gavg += (slope - proj) * dr;
                    }
                    return gavg;
                };
                const Vec2 gu = corrected(gw.grads[L].gu, gw.grads[Rc].gu,
                                          gw.prims[L].u, gw.prims[Rc].u);
                const Vec2 gv = corrected(gw.grads[L].gv, gw.grads[Rc].gv,
                                          gw.prims[L].v, gw.prims[Rc].v);
                const Vec2 gT = corrected(gw.grads[L].gT, gw.grads[Rc].gT,
                                          gw.prims[L].T, gw.prims[Rc].T);
                const State Fv = viscous_flux(qL, qR, gu, gv, gT, nx, ny, S,
                                              mu, gamma, Rgas, prandtl);
                spatial[L] -= Fv;
                spatial[Rc] += Fv;
            }
        } else {
            // Boundary face
            const BcKind btype = face_bc_[face.id];
            const Prims qL = prims_from_state(U_[L], gamma, Rgas);
            State Fb = State::Zero();

            switch (btype) {
            case BcKind::Farfield: {
                Fb = rusanov_flux(U_[L], ci_.freestream, nx, ny, S, gamma, Rgas, diss);
                break;
            }
            case BcKind::SlipWall: {
                // Specular reflection of the wall-normal velocity; the full
                // numerical flux damps the normal component during the
                // transient and degenerates to the pressure flux at tangency.
                const Real un = qL.u * nx + qL.v * ny;
                Prims qg = qL;
                qg.u -= 2.0 * un * nx;
                qg.v -= 2.0 * un * ny;
                Fb = rusanov_flux(U_[L], state_from_prims(qg, gamma),
                                  nx, ny, S, gamma, Rgas, diss);
                break;
            }
            case BcKind::NoSlipWall: {
                Fb = pressure_flux(qL.p, nx, ny, S);
                if (viscous) {
                    const Vec2 dvec = face.centroid - cents_[L];
                    const Real d = std::fabs(dvec.dot(face.normal)) / S;
                    const Real d_eff = std::max(
                        d, 0.5 * std::sqrt(std::max(vols_[L], 1e-30)));
                    Real tau_xx, tau_yy, tau_xy;
                    wall_stress(qL.u, qL.v, nx, ny, d_eff, mu,
                                tau_xx, tau_yy, tau_xy);
                    const Real txx_n = tau_xx * nx + tau_xy * ny;
                    const Real tyy_n = tau_xy * nx + tau_yy * ny;
                    // The wall face contributes +(p n - tau·n) S.
                    Fb[1] -= txx_n * S;
                    Fb[2] -= tyy_n * S;
                }
                break;
            }
            default:
                throw std::runtime_error("unhandled boundary condition");
            }
            spatial[L] += Fb;
        }
    }

    // R = -spatial/V + physical-time term
    if (getenv("CFD_TRACE")) std::cerr << "[trace] faces done, time term\n";
    for (Index i = 0; i < lm_.n_owned; i++) {
        State Rt = -spatial[i] / lm_.owned[i].volume;
        if (is_transient) {
            // Dual-time residual: R* = -(R_space + R_time) with
            // R_time = (3U - 4U^n + U^{n-1}) / (2 dt)
            Rt -= (3.0 * U_[i] - 4.0 * U_n[i] + U_nm1[i]) / (2.0 * dt);
        }
        R[i] = Rt;
    }

    // Global norm: L2 over cells of the volume-weighted flux imbalance
    // |R_i * V_i|.  All reductions are global MPI reductions.
    if (getenv("CFD_TRACE")) std::cerr << "[trace] norm begin\n";
    Real l2 = 0.0, linf = 0.0;
    Real rho_n = 0.0, rhou_n = 0.0, rhov_n = 0.0, rhoE_n = 0.0;
    for (Index i = 0; i < lm_.n_owned; i++) {
        const Real vol = lm_.owned[i].volume;
        const State w = R[i] * vol;
        l2 += w.squaredNorm();
        linf = std::max({linf, std::abs(R[i][0]), std::abs(R[i][1]),
                         std::abs(R[i][2]), std::abs(R[i][3])});
        rho_n += w[0] * w[0];
        rhou_n += w[1] * w[1];
        rhov_n += w[2] * w[2];
        rhoE_n += w[3] * w[3];
    }
    Real red[5] = {l2, rho_n, rhou_n, rhov_n, rhoE_n};
    MPI_Allreduce(MPI_IN_PLACE, red, 5, MPI_DOUBLE, MPI_SUM, comm_);
    MPI_Allreduce(MPI_IN_PLACE, &linf, 1, MPI_DOUBLE, MPI_MAX, comm_);
    norm.l2 = std::sqrt(red[0]);
    norm.linf = linf;
    norm.rho = std::sqrt(red[1]);
    norm.rhou = std::sqrt(red[2]);
    norm.rhov = std::sqrt(red[3]);
    norm.rhoE = std::sqrt(red[4]);
}


// ---------------------------------------------------------------------------
// WLS primitive gradients (1/d^2) with mirrored-ghost boundary samples.
// ---------------------------------------------------------------------------
void compute_gradients(const Solver& solver, const std::vector<State>& U,
                       GradWork& gw) {
    const auto& lm = solver.mesh();
    const auto& ci = solver.case_input();
    const Real gamma = ci.gamma, R = ci.R;

    gw.grads.resize(lm.n_total);
    for (auto& g : gw.grads) {
        g.grho.setZero(); g.gu.setZero(); g.gv.setZero(); g.gT.setZero();
    }
    gw.prims.assign(lm.n_total, Prims{});
    for (Index i = 0; i < lm.n_total; i++) {
        gw.prims[i] = prims_from_state(U[i], gamma, R);
    }

    for (Index i = 0; i < lm.n_owned; i++) {
        const Vec2 cc = solver.local_centroids()[i];
        const Prims& qc = gw.prims[i];

        Real mxx = 0, mxy = 0, myy = 0;
        Real bx[4] = {0, 0, 0, 0}, by[4] = {0, 0, 0, 0};
        Real strongest_d2 = 0, sx = 0, sy = 0;

        auto add_sample = [&](const Vec2& loc, const Prims& q) {
            const Vec2 dr = loc - cc;
            const Real d2 = dr.squaredNorm();
            if (!(d2 > 1e-30)) return;
            const Real w = 1.0 / d2;
            mxx += w * dr[0] * dr[0];
            mxy += w * dr[0] * dr[1];
            myy += w * dr[1] * dr[1];
            if (d2 > strongest_d2) { strongest_d2 = d2; sx = dr[0]; sy = dr[1]; }
            const Real vals[4] = {q.rho - qc.rho, q.u - qc.u, q.v - qc.v, q.T - qc.T};
            for (int k = 0; k < 4; k++) {
                bx[k] += w * dr[0] * vals[k];
                by[k] += w * dr[1] * vals[k];
            }
        };

        for (Index fid : lm.owned[i].faces) {
            const Face& face = lm.faces[fid];
            const Index nb = (face.left == i) ? face.right : face.left;
            if (nb >= 0) {
                add_sample(solver.local_centroids()[nb], gw.prims[nb]);
            } else {
                Vec2 gcent = 2.0 * face.centroid - cc;
                Prims qg = qc;
                const BcKind bt = solver.face_bc()[fid];
                if (bt == BcKind::Farfield) {
                    qg = prims_from_state(ci.freestream, gamma, R);
                } else if (bt == BcKind::NoSlipWall) {
                    // No-slip wall: use the cell's own value (wall shear is
                    // imposed separately by the wall_viscous_stress term).
                    qg = qc;
                } else if (bt == BcKind::SlipWall) {
                    const Real nxn = face.normal[0], nyn = face.normal[1];
                    const Real Sn = std::sqrt(nxn * nxn + nyn * nyn);
                    if (Sn > 0) {
                        const Real un = qc.u * nxn / Sn + qc.v * nyn / Sn;
                        qg.u = qc.u - 2.0 * un * nxn / Sn;
                        qg.v = qc.v - 2.0 * un * nyn / Sn;
                    }
                }
                add_sample(gcent, qg);
            }
        }

        const Real trace = mxx + myy;
        const Real det = mxx * myy - mxy * mxy;
        const bool full_rank = trace > 0 && det > 1e-13 * trace * trace;
        Vec2 g[4];
        if (full_rank) {
            const Real inv_det = 1.0 / det;
            for (int k = 0; k < 4; k++) {
                g[k][0] = (myy * bx[k] - mxy * by[k]) * inv_det;
                g[k][1] = (mxx * by[k] - mxy * bx[k]) * inv_det;
            }
        } else if (strongest_d2 > 0) {
            const Real inv = 1.0 / std::sqrt(strongest_d2);
            const Real dx = sx * inv, dy = sy * inv;
            Real denom = 0, num[4] = {0, 0, 0, 0};
            for (Index fid : lm.owned[i].faces) {
                const Face& face = lm.faces[fid];
                const Index nb = (face.left == i) ? face.right : face.left;
                Vec2 loc; Prims q;
                if (nb >= 0) {
                    loc = solver.local_centroids()[nb];
                    q = gw.prims[nb];
                } else {
                    loc = 2.0 * face.centroid - cc;
                    q = qc;
                    const BcKind bt = solver.face_bc()[fid];
                    if (bt == BcKind::Farfield) {
                        q = prims_from_state(ci.freestream, gamma, R);
                    } else if (bt == BcKind::NoSlipWall) {
                        q = qc;
                    } else if (bt == BcKind::SlipWall) {
                        const Real nxn = face.normal[0], nyn = face.normal[1];
                        const Real Sn = std::sqrt(nxn * nxn + nyn * nyn);
                        if (Sn > 0) {
                            const Real un = qc.u * nxn / Sn + qc.v * nyn / Sn;
                            q.u = qc.u - 2.0 * un * nxn / Sn;
                            q.v = qc.v - 2.0 * un * nyn / Sn;
                        }
                    }
                }
                const Vec2 dr = loc - cc;
                const Real d2 = dr.squaredNorm();
                if (!(d2 > 1e-30)) continue;
                const Real w = 1.0 / d2;
                const Real proj = dr[0] * dx + dr[1] * dy;
                denom += w * proj * proj;
                const Real vals[4] = {q.rho - qc.rho, q.u - qc.u, q.v - qc.v, q.T - qc.T};
                for (int k = 0; k < 4; k++) num[k] += w * proj * vals[k];
            }
            for (int k = 0; k < 4; k++) {
                const Real dd = (denom > 1e-30) ? num[k] / denom : 0.0;
                g[k][0] = dd * dx; g[k][1] = dd * dy;
            }
        } else {
            for (int k = 0; k < 4; k++) { g[k][0] = 0; g[k][1] = 0; }
        }
        gw.grads[i].grho = g[0];
        gw.grads[i].gu = g[1];
        gw.grads[i].gv = g[2];
        gw.grads[i].gT = g[3];
    }

    // Average owned-cell gradients into ghost cells
    std::vector<int> gcount(lm.n_ghost, 0);
    for (const Face& face : lm.faces) {
        const Index L = face.left, Rc = face.right;
        if (Rc >= lm.n_owned && Rc < lm.n_total && L >= 0 && L < lm.n_owned) {
            const Index g = Rc - lm.n_owned;
            gw.grads[Rc].grho += gw.grads[L].grho;
            gw.grads[Rc].gu += gw.grads[L].gu;
            gw.grads[Rc].gv += gw.grads[L].gv;
            gw.grads[Rc].gT += gw.grads[L].gT;
            gcount[g]++;
        }
    }
    for (Index g = 0; g < lm.n_ghost; g++) {
        if (gcount[g] > 0) {
            const Real inv = 1.0 / (Real)gcount[g];
            gw.grads[lm.n_owned + g].grho *= inv;
            gw.grads[lm.n_owned + g].gu *= inv;
            gw.grads[lm.n_owned + g].gv *= inv;
            gw.grads[lm.n_owned + g].gT *= inv;
        }
    }
}

// ---------------------------------------------------------------------------
// Barth-Jespersen limiter on primitive variables.
// ---------------------------------------------------------------------------
void barth_limiter(const Solver& solver, const std::vector<State>& U,
                   GradWork& gw) {
    const auto& lm = solver.mesh();
    const auto& ci = solver.case_input();
    const Real gamma = ci.gamma, R = ci.R;
    gw.phi.assign(lm.n_owned, 1.0);

    std::vector<Prims> mins(lm.n_owned), maxs(lm.n_owned);
    for (Index i = 0; i < lm.n_owned; i++) {
        mins[i] = gw.prims[i];
        maxs[i] = gw.prims[i];
        for (Index fid : lm.owned[i].faces) {
            const Face& face = lm.faces[fid];
            const Index nb = (face.left == i) ? face.right : face.left;
            Prims q;
            if (nb >= 0) {
                q = gw.prims[nb];
            } else {
                q = gw.prims[i];
                const BcKind bt = solver.face_bc()[fid];
                if (bt == BcKind::Farfield) {
                    q = prims_from_state(ci.freestream, gamma, R);
                } else if (bt == BcKind::NoSlipWall) {
                    q.u = 0.0; q.v = 0.0;
                } else if (bt == BcKind::SlipWall) {
                    const Real nxn = face.normal[0], nyn = face.normal[1];
                    const Real Sn = std::sqrt(nxn * nxn + nyn * nyn);
                    if (Sn > 0) {
                        const Real un = gw.prims[i].u * nxn / Sn + gw.prims[i].v * nyn / Sn;
                        q.u = gw.prims[i].u - 2.0 * un * nxn / Sn;
                        q.v = gw.prims[i].v - 2.0 * un * nyn / Sn;
                    }
                }
            }
            mins[i].rho = std::min(mins[i].rho, q.rho);
            mins[i].u = std::min(mins[i].u, q.u);
            mins[i].v = std::min(mins[i].v, q.v);
            mins[i].T = std::min(mins[i].T, q.T);
            maxs[i].rho = std::max(maxs[i].rho, q.rho);
            maxs[i].u = std::max(maxs[i].u, q.u);
            maxs[i].v = std::max(maxs[i].v, q.v);
            maxs[i].T = std::max(maxs[i].T, q.T);
        }
    }

    for (Index i = 0; i < lm.n_owned; i++) {
        const auto& g = gw.grads[i];
        const Prims& qc = gw.prims[i];
        Real phi = 1.0;
        const Vec2* grads[4] = {&g.grho, &g.gu, &g.gv, &g.gT};
        const Real qmin[4] = {mins[i].rho, mins[i].u, mins[i].v, mins[i].T};
        const Real qmax[4] = {maxs[i].rho, maxs[i].u, maxs[i].v, maxs[i].T};
        const Real q0[4] = {qc.rho, qc.u, qc.v, qc.T};

        for (Index fid : lm.owned[i].faces) {
            const Face& face = lm.faces[fid];
            const Vec2 dr = face.centroid - solver.local_centroids()[i];
            for (int v = 0; v < 4; v++) {
                const Real delta = grads[v]->dot(dr);
                Real phiv = 1.0;
                if (delta > 1e-30) {
                    const Real dm = qmax[v] - q0[v];
                    phiv = (dm <= 1e-30) ? 0.0 : dm / delta;
                } else if (delta < -1e-30) {
                    const Real dm = qmin[v] - q0[v];
                    phiv = (dm >= -1e-30) ? 0.0 : dm / delta;
                }
                if (phiv < 0) phiv = 0;
                if (phiv > 1) phiv = 1;
                phi = std::min(phi, phiv);
            }
        }
        gw.phi[i] = std::max(phi, 0.0);
    }
}

// ---------------------------------------------------------------------------
// Reconstruct left/right primitive states at a face.
// ---------------------------------------------------------------------------
void reconstruct_face(const Solver& solver, const std::vector<State>& U,
                      const GradWork& gw, Index face_id,
                      Prims& qL, Prims& qR) {
    const auto& lm = solver.mesh();
    const Face& face = lm.faces[face_id];
    const Index L = face.left, Rc = face.right;
    const auto& ci = solver.case_input();

    const Real phiL = (L < lm.n_owned) ? gw.phi[L] : 0.0;
    const Real phiR = (Rc >= 0 && Rc < lm.n_owned) ? gw.phi[Rc] : 0.0;

    auto recon = [&](Index c, Real phi) -> Prims {
        Prims q = gw.prims[c];
        if (solver.first_order_phase()) return q;
        // Tiny-volume fallback for degenerate sliver cells
        if (solver.local_volumes()[c] < 1.0e-7) return q;
        const auto& g = gw.grads[c];
        const Vec2 dr = face.centroid - solver.local_centroids()[c];
        q.rho += phi * g.grho.dot(dr);
        q.u += phi * g.gu.dot(dr);
        q.v += phi * g.gv.dot(dr);
        q.T += phi * g.gT.dot(dr);
        if (q.rho <= 0.0 || q.T <= 0.0) return gw.prims[c];
        q.p = q.rho * ci.R * q.T;
        q.a = std::sqrt(ci.gamma * q.p / q.rho);
        q.e = q.p / ((ci.gamma - 1.0) * q.rho);
        return q;
    };

    qL = recon(L, phiL);
    qR = (Rc >= 0) ? recon(Rc, phiR) : qL;
}

// ---------------------------------------------------------------------------
// LU-SGS sweep
// ---------------------------------------------------------------------------
void Solver::lusgs_sweep(const std::vector<State>& R, std::vector<State>& U,
                         const Real* dt_local, bool is_transient, Real dt_phys) {
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;
    const Real diss = ci_.rusanov_dissipation_scale;
    const Real mu = ci_.mu_ref;
    const Real prandtl = ci_.prandtl;
    const bool viscous = ci_.physics_mode == "laminar";
    const Real cv = Rgas / (gamma - 1.0);
    const Real kk = (viscous && mu > 0.0) ? mu * gamma * Rgas / ((gamma - 1.0) * prandtl) : 0.0;
    const Real omega = getenv("CFD_LUSGS_RELAX") ? std::atof(getenv("CFD_LUSGS_RELAX")) : 0.7;
    const Real diag_factor = getenv("CFD_LUSGS_DIAG_FACTOR")
        ? std::atof(getenv("CFD_LUSGS_DIAG_FACTOR")) : 1.0;

    std::vector<Real> lam_plus(lm_.faces.size(), 0.0);
    std::vector<Real> lam_minus(lm_.faces.size(), 0.0);
    for (const Face& face : lm_.faces) {
        const Index L = face.left, Rc = face.right;
        const Real S = face_len_[face.id];
        if (S <= 0.0) continue;
        const Real nx = face.normal[0] / S, ny = face.normal[1] / S;
        Real un = 0.0, kappa = 0.0;
        if (L >= 0) {
            const Prims qL = prims_from_state(U[L], gamma, Rgas);
            const Real unL = qL.u * nx + qL.v * ny;
            un = unL;
            kappa = std::abs(unL) + qL.a;
        }
        if (Rc >= 0) {
            const Prims qR = prims_from_state(U[Rc], gamma, Rgas);
            const Real unR = qR.u * nx + qR.v * ny;
            un = 0.5 * (un + unR);
            kappa = std::max(kappa, std::abs(unR) + qR.a);
        }
        kappa *= diss;
        lam_plus[face.id] = 0.5 * (kappa + un) * S;
        lam_minus[face.id] = 0.5 * (kappa - un) * S;
    }

    std::vector<Real> diag(lm_.n_total, 0.0);
    for (Index i = 0; i < lm_.n_owned; i++) {
        Real d = lm_.owned[i].volume / std::max(dt_local[i], 1e-30);
        if (is_transient) d += 1.5 * lm_.owned[i].volume / dt_phys;
        Real dv_i = 0.0;
        if (viscous && mu > 0.0) {
            const Prims qi = prims_from_state(U[i], gamma, Rgas);
            dv_i = std::max(mu / qi.rho, kk / (qi.rho * cv));
        }
        for (Index fid : lm_.owned[i].faces) {
            const Real Sf = face_len_[fid];
            d += diag_factor * 0.5 * (lam_plus[fid] + lam_minus[fid]);
            if (viscous && mu > 0.0) d += diag_factor * dv_i * Sf * Sf / lm_.owned[i].volume;
        }
        diag[i] = d;
    }

    std::vector<State> dU(lm_.n_owned, State::Zero());

    for (Index i = 0; i < lm_.n_owned; i++) {
        State rhs = R[i] * lm_.owned[i].volume;
        for (Index fid : lm_.owned[i].faces) {
            const Face& face = lm_.faces[fid];
            const Index nb = (face.left == i) ? face.right : face.left;
            if (nb < 0 || nb >= lm_.n_owned || nb >= i) continue;
            const Real coeff = (face.left == i) ? lam_minus[fid] : lam_plus[fid];
            rhs += coeff * dU[nb];
        }
        dU[i] = omega * rhs / diag[i];
    }

    for (Index i = lm_.n_owned - 1; i >= 0; i--) {
        State rhs = R[i] * lm_.owned[i].volume;
        for (Index fid : lm_.owned[i].faces) {
            const Face& face = lm_.faces[fid];
            const Index nb = (face.left == i) ? face.right : face.left;
            if (nb < 0 || nb >= lm_.n_owned || nb <= i) continue;
            const Real coeff = (face.left == i) ? lam_minus[fid] : lam_plus[fid];
            rhs += coeff * dU[nb];
        }
        dU[i] = omega * rhs / diag[i];
    }

    const Real limiter_frac = 0.25;
    for (Index i = 0; i < lm_.n_owned; i++) {
        const State& Ui = U[i];
        const Prims qi = prims_from_state(Ui, gamma, Rgas);
        const Real scale[4] = {std::max(Ui[0], 1e-30),
                               Ui[0] * (std::abs(qi.u) + qi.a),
                               Ui[0] * (std::abs(qi.v) + qi.a),
                               std::max(std::abs(Ui[3]), qi.p / (gamma - 1.0))};
        Real ratio = 0.0;
        for (int c = 0; c < 4; c++) {
            ratio = std::max(ratio, std::abs(dU[i][c]) / std::max(scale[c], 1e-30));
        }
        Real alpha = (ratio > limiter_frac) ? limiter_frac / ratio : 1.0;
        for (int bt = 0; bt < 45; bt++) {
            const State trial = Ui + alpha * dU[i];
            if (trial[0] > 0.0) {
                const Real ke = 0.5 * (trial[1] * trial[1] + trial[2] * trial[2]) / trial[0];
                if ((gamma - 1.0) * (trial[3] - ke) > 0.0) break;
            }
            alpha *= 0.5;
        }
        if (alpha > 0.0) U[i] += alpha * dU[i];
    }
}

// ---------------------------------------------------------------------------
// Assemble the 4x4 block Jacobian
// ---------------------------------------------------------------------------
void Solver::assemble_jacobian(std::vector<Mat4>& block_diag,
                               std::vector<FaceImplicit>& fimp,
                               const Real* dt_local, Real dt_phys) const {
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;
    const Real diss = ci_.rusanov_dissipation_scale;
    const Real mu = ci_.mu_ref;
    const Real prandtl = ci_.prandtl;
    const bool viscous = ci_.physics_mode == "laminar";
    const Real visc_coeff = (4.0 / 3.0 + gamma / prandtl);

    for (const Face& face : lm_.faces) {
        const Index L = face.left;
        if (L < 0 || L >= lm_.n_owned) continue;
        const Index Rc = face.right;
        const Real S = face_len_[face.id];
        if (S <= 0.0) continue;
        const Real nx = face.normal[0] / S;
        const Real ny = face.normal[1] / S;

        const Prims qL = prims_from_state(U_[L], gamma, Rgas);
        if (Rc >= 0 && Rc < lm_.n_total) {
            const Prims qR = prims_from_state(U_[Rc], gamma, Rgas);
            const Real unL = qL.u * nx + qL.v * ny;
            const Real unR = qR.u * nx + qR.v * ny;
            const Real kappa = diss * std::max(std::abs(unL) + qL.a, std::abs(unR) + qR.a);
            const Mat4 A_L = euler_jacobian(U_[L], nx, ny, gamma, Rgas);
            const Mat4 A_R = euler_jacobian(U_[Rc], nx, ny, gamma, Rgas);
            Real viscous_diag = 0.0;
            if (viscous && mu > 0.0) {
                const Vec2 dr = cents_[Rc] - cents_[L];
                const Real dist = dr.norm();
                const Real d = std::max(std::fabs(dr.dot(face.normal)) / S, 0.1 * dist);
                const Real rho_face = 0.5 * (qL.rho + qR.rho);
                viscous_diag = (mu / std::max(rho_face, 1e-30)) * visc_coeff *
                               S / std::max(d, 1e-14);
            }
            fimp[face.id].off_owner = (0.5 * A_R - 0.5 * kappa * Mat4::Identity()) * S;
            fimp[face.id].off_neighbor = (-0.5 * A_L - 0.5 * kappa * Mat4::Identity()) * S;
            if (viscous_diag > 0.0) {
                fimp[face.id].off_owner -= viscous_diag * Mat4::Identity();
                fimp[face.id].off_neighbor -= viscous_diag * Mat4::Identity();
            }
            fimp[face.id].used = true;
            block_diag[L] += (0.5 * A_L + 0.5 * kappa * Mat4::Identity()) * S;
            if (viscous_diag > 0.0) block_diag[L] += viscous_diag * Mat4::Identity();
            if (Rc < lm_.n_owned) {
                block_diag[Rc] += (-0.5 * A_R + 0.5 * kappa * Mat4::Identity()) * S;
                if (viscous_diag > 0.0) block_diag[Rc] += viscous_diag * Mat4::Identity();
            }
        } else {
            const BcKind btype = face_bc_[face.id];
            const Real unL = qL.u * nx + qL.v * ny;
            const Real kappa = diss * (std::abs(unL) + qL.a);
            Real viscous_diag = 0.0;
            if (viscous && mu > 0.0) {
                const Vec2 dvec = face.centroid - cents_[L];
                const Real dist = dvec.norm();
                const Real d = std::max(std::fabs(dvec.dot(face.normal)) / S, 0.1 * dist);
                viscous_diag = (mu / std::max(qL.rho, 1e-30)) * visc_coeff *
                               S / std::max(d, 1e-14);
            }
            if (btype == BcKind::Farfield) {
                block_diag[L] += (kappa * S + viscous_diag) * Mat4::Identity();
            } else {
                block_diag[L] += wall_jacobian(U_[L], nx, ny, gamma, Rgas) * S;
                if (viscous && btype == BcKind::NoSlipWall) {
                    const Vec2 dvec = face.centroid - cents_[L];
                    const Real d = std::fabs(dvec.dot(face.normal)) / S;
                    const Real d_eff = std::max(
                        d, 0.5 * std::sqrt(std::max(vols_[L], 1e-30)));
                    if (d_eff > 0.0) {
                        block_diag[L] += wall_traction_jacobian(
                            U_[L], nx, ny, S, d_eff, mu, gamma, Rgas);
                    }
                } else if (viscous_diag > 0.0) {
                    block_diag[L] += viscous_diag * Mat4::Identity();
                }
            }
        }
    }

    for (Index i = 0; i < lm_.n_owned; i++) {
        block_diag[i] += (lm_.owned[i].volume / std::max(dt_local[i], 1e-30)) * Mat4::Identity();
    }
    if (dt_phys > 0.0) {
        const Real bdf = 1.5 / dt_phys;
        for (Index i = 0; i < lm_.n_owned; i++) {
            block_diag[i] += (bdf * lm_.owned[i].volume) * Mat4::Identity();
        }
    }
}

// ---------------------------------------------------------------------------
// Block-Jacobi correction
// ---------------------------------------------------------------------------
Real Solver::block_jacobi_correction(const std::vector<State>& R,
                                     const std::vector<State>& pseudo_old,
                                     const Real* dt_local, Real dt_phys) {
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;
    const Index n_owned = lm_.n_owned;
    const Index n_total = lm_.n_total;

    std::vector<Real> c(n_owned, 0.0);
    for (Index i = 0; i < n_owned; i++) {
        c[i] = lm_.owned[i].volume / std::max(dt_local[i], 1e-30);
    }

    Real local_l2 = 0.0;
    for (Index i = 0; i < n_owned; i++) {
        const State G = (dt_phys > 0.0)
            ? R[i]
            : R[i] - (U_[i] - pseudo_old[i]) / std::max(dt_local[i], 1e-30);
        local_l2 += (G * lm_.owned[i].volume).squaredNorm();
    }
    MPI_Allreduce(MPI_IN_PLACE, &local_l2, 1, MPI_DOUBLE, MPI_SUM, comm_);
    const Real pseudo_norm = std::sqrt(local_l2);

    std::vector<FaceImplicit> fimp(lm_.faces.size());
    std::vector<Mat4> block_diag(n_owned, Mat4::Zero());
    assemble_jacobian(block_diag, fimp, dt_local, dt_phys);

    const int min_sweeps = 2;
    const int max_sweeps = 8;
    const Real target_ratio = 0.05;
    const Real damp = 0.8;

    std::vector<State> correction(n_total, State::Zero());
    std::vector<State> previous(n_total, State::Zero());
    std::vector<State> next(n_owned);
    int sweeps = 0;
    Real initial_change = 1.0;
    for (; sweeps < max_sweeps; sweeps++) {
        exchange_correction(correction);
        previous = correction;
        for (Index i = 0; i < n_owned; i++) {
            State neighbor_sum = State::Zero();
            for (Index fid : lm_.owned[i].faces) {
                const Face& face = lm_.faces[fid];
                const Index other = (face.left == i) ? face.right : face.left;
                if (other < 0 || !fimp[fid].used) continue;
                const Mat4& blk = (face.left == i)
                    ? fimp[fid].off_owner : fimp[fid].off_neighbor;
                neighbor_sum += blk * previous[other];
            }
            State rhs = R[i] * lm_.owned[i].volume - neighbor_sum;
            if (dt_phys <= 0.0) rhs -= c[i] * (U_[i] - pseudo_old[i]);
            const State jacobi = solve4(block_diag[i], rhs);
            next[i] = damp * jacobi + (1.0 - damp) * previous[i];
        }
        Real local_change = 0.0;
        for (Index i = 0; i < n_owned; i++) {
            local_change += (next[i] - previous[i]).squaredNorm();
            correction[i] = next[i];
        }
        Real global_change = 0.0;
        MPI_Allreduce(&local_change, &global_change, 1, MPI_DOUBLE, MPI_SUM, comm_);
        global_change = std::sqrt(global_change);
        if (sweeps == 0) initial_change = std::max(global_change, 1e-300);
        if (sweeps + 1 >= min_sweeps && global_change / initial_change <= target_ratio) {
            ++sweeps;
            break;
        }
    }

    for (Index i = 0; i < n_owned; i++) {
        const State& Ui = U_[i];
        const Prims qi = prims_from_state(Ui, gamma, Rgas);
        const Real scale[4] = {std::max(Ui[0], 1e-30),
                               Ui[0] * (std::abs(qi.u) + qi.a),
                               Ui[0] * (std::abs(qi.v) + qi.a),
                               std::max(std::abs(Ui[3]), qi.p / (gamma - 1.0))};
        Real ratio = 0.0;
        for (int c = 0; c < 4; c++) {
            ratio = std::max(ratio, std::abs(correction[i][c]) / std::max(scale[c], 1e-30));
        }
        Real alpha = (ratio > 0.25) ? 0.25 / ratio : 1.0;
        for (int bt = 0; bt < 45; bt++) {
            const State trial = Ui + alpha * correction[i];
            if (trial[0] > 0.0) {
                const Real ke = 0.5 * (trial[1] * trial[1] + trial[2] * trial[2]) / trial[0];
                if ((gamma - 1.0) * (trial[3] - ke) > 0.0) break;
            }
            alpha *= 0.5;
        }
        if (alpha > 0.0) U_[i] += alpha * correction[i];
    }
    return pseudo_norm;
}

// ---------------------------------------------------------------------------
// Newton-GMRES correction
// ---------------------------------------------------------------------------
Real Solver::newton_gmres_correction(const std::vector<State>& R,
                                     const Real* dt_local, Real dt_phys) {
    const Index n_owned = lm_.n_owned;
    const Index n_total = lm_.n_total;
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;

    std::vector<State> b(n_owned);
    Real pn_l2 = 0.0;
    for (Index i = 0; i < n_owned; i++) {
        b[i] = R[i] * lm_.owned[i].volume;
        pn_l2 += b[i].squaredNorm();
    }
    MPI_Allreduce(MPI_IN_PLACE, &pn_l2, 1, MPI_DOUBLE, MPI_SUM, comm_);
    const Real pn = std::sqrt(pn_l2);
    if (pn < 1e-300) return pn;

    std::vector<FaceImplicit> fimp(lm_.faces.size());
    std::vector<Mat4> block_diag(n_owned, Mat4::Zero());
    assemble_jacobian(block_diag, fimp, dt_local, dt_phys);

    const int m = 30;
    int max_iter = 200;
    Real tol_rel = 1e-10;

    auto global_dot = [&](const std::vector<State>& a,
                           const std::vector<State>& bb) -> Real {
        Real s = 0.0;
        for (Index i = 0; i < n_owned; i++) s += a[i].dot(bb[i]);
        MPI_Allreduce(MPI_IN_PLACE, &s, 1, MPI_DOUBLE, MPI_SUM, comm_);
        return s;
    };

    auto apply_A = [&](const std::vector<State>& vin,
                        std::vector<State>& out) {
        std::vector<State> v_full(n_total, State::Zero());
        for (Index i = 0; i < n_owned; i++) v_full[i] = vin[i];
        exchange_correction(v_full);
        for (Index i = 0; i < n_owned; i++) {
            out[i] = block_diag[i] * v_full[i];
        }
        for (const Face& face : lm_.faces) {
            const Index L = face.left;
            if (L < 0 || L >= n_owned) continue;
            const Index Rc = face.right;
            if (Rc < 0 || !fimp[face.id].used) continue;
            out[L] += fimp[face.id].off_owner * v_full[Rc];
            if (Rc < n_owned) out[Rc] += fimp[face.id].off_neighbor * v_full[L];
        }
    };

    std::vector<State> z(n_owned);
    for (Index i = 0; i < n_owned; i++) z[i] = solve4(block_diag[i], b[i]);
    Real beta = std::sqrt(std::max(global_dot(z, z), 1e-300));
    const Real beta0 = beta;
    std::vector<State> x(n_owned, State::Zero());
    if (beta < 1e-30) return pn;

    std::vector<std::vector<State>> V(m + 1, std::vector<State>(n_owned));
    std::vector<Real> H((m + 1) * m, 0.0);
    std::vector<Real> cs(m, 0.0), sn(m, 0.0);
    std::vector<Real> g(m + 1, 0.0);
    std::vector<State> w(n_owned), tmp(n_owned);
    int total_steps = 0;
    Real resid = beta;

    while (total_steps < max_iter && resid > tol_rel * beta0) {
        for (Index i = 0; i < n_owned; i++) V[0][i] = z[i] / beta;
        g[0] = beta;
        for (int j = 0; j <= m; j++) if (j > 0) g[j] = 0.0;

        int k = 0;
        for (k = 0; k < m && total_steps + k < max_iter; k++) {
            for (Index i = 0; i < n_owned; i++) tmp[i] = solve4(block_diag[i], V[k][i]);
            apply_A(tmp, w);
            for (int j = 0; j <= k; j++) {
                const Real h = global_dot(w, V[j]);
                H[j + k * (m + 1)] = h;
                for (Index i = 0; i < n_owned; i++) w[i] -= h * V[j][i];
            }
            const Real hnn = std::sqrt(std::max(global_dot(w, w), 1e-300));
            H[(k + 1) + k * (m + 1)] = hnn;
            if (hnn > 1e-30) {
                for (Index i = 0; i < n_owned; i++) V[k + 1][i] = w[i] / hnn;
            }
            for (int j = 0; j < k; j++) {
                const Real c = cs[j], s = sn[j];
                const Real hj = H[j + k * (m + 1)];
                const Real hj1 = H[(j + 1) + k * (m + 1)];
                H[j + k * (m + 1)] = c * hj + s * hj1;
                H[(j + 1) + k * (m + 1)] = -s * hj + c * hj1;
            }
            const Real f = H[k + k * (m + 1)];
            const Real gg = H[(k + 1) + k * (m + 1)];
            const Real r = std::sqrt(f * f + gg * gg);
            cs[k] = (r > 1e-300) ? f / r : 1.0;
            sn[k] = (r > 1e-300) ? gg / r : 0.0;
            H[k + k * (m + 1)] = r;
            H[(k + 1) + k * (m + 1)] = 0.0;
            const Real gk = g[k], gk1 = g[k + 1];
            g[k] = cs[k] * gk + sn[k] * gk1;
            g[k + 1] = -sn[k] * gk + cs[k] * gk1;
            resid = std::abs(g[k + 1]);
            if (resid <= tol_rel * beta0 || hnn <= 1e-30) { k++; break; }
        }
        total_steps += k;

        std::vector<Real> y(k, 0.0);
        for (int i = k - 1; i >= 0; i--) {
            Real acc = 0.0;
            for (int j = i + 1; j < k; j++) acc += H[i + j * (m + 1)] * y[j];
            const Real diag = H[i + i * (m + 1)];
            y[i] = (std::abs(diag) > 1e-300) ? (g[i] - acc) / diag : 0.0;
        }
        for (Index i = 0; i < n_owned; i++) tmp[i] = State::Zero();
        for (int j = 0; j < k; j++) {
            for (Index i = 0; i < n_owned; i++) tmp[i] += y[j] * V[j][i];
        }
        for (Index i = 0; i < n_owned; i++) {
            x[i] += solve4(block_diag[i], tmp[i]);
        }

        apply_A(x, w);
        for (Index i = 0; i < n_owned; i++) z[i] = b[i] - w[i];
        for (Index i = 0; i < n_owned; i++) z[i] = solve4(block_diag[i], z[i]);
        beta = std::sqrt(std::max(global_dot(z, z), 1e-300));
        resid = beta;
    }

    for (Index i = 0; i < n_owned; i++) {
        const State& Ui = U_[i];
        const Prims qi = prims_from_state(Ui, gamma, Rgas);
        const Real scale[4] = {std::max(Ui[0], 1e-30),
                               Ui[0] * (std::abs(qi.u) + qi.a),
                               Ui[0] * (std::abs(qi.v) + qi.a),
                               std::max(std::abs(Ui[3]), qi.p / (gamma - 1.0))};
        Real ratio = 0.0;
        for (int c = 0; c < 4; c++) {
            ratio = std::max(ratio, std::abs(x[i][c]) / std::max(scale[c], 1e-30));
        }
        Real alpha = (ratio > 10.0) ? 10.0 / ratio : 1.0;
        for (int bt = 0; bt < 45; bt++) {
            const State trial = Ui + alpha * x[i];
            if (trial[0] > 0.0) {
                const Real ke = 0.5 * (trial[1] * trial[1] + trial[2] * trial[2]) / trial[0];
                if ((gamma - 1.0) * (trial[3] - ke) > 0.0) break;
            }
            alpha *= 0.5;
        }
        if (alpha > 0.0) U_[i] += alpha * x[i];
    }
    return pn;
}

// ---------------------------------------------------------------------------
// Forces and surface output
// ---------------------------------------------------------------------------
ForceCoeffs Solver::compute_forces(bool gather_surface) {
    const Real gamma = ci_.gamma;
    const Real Rgas = ci_.R;
    const Real mu = ci_.mu_ref;
    const bool viscous = ci_.physics_mode == "laminar";
    const Real qinf = 0.5 * ci_.rho_inf *
                      (ci_.u_inf * ci_.u_inf + ci_.v_inf * ci_.v_inf);
    const Real c_ref = ci_.ref_length;
    const Real a_ref = ci_.ref_area;

    ForceCoeffs fc;
    std::vector<WallRow> rows;

    for (const Face& face : lm_.faces) {
        if (face.right >= 0) continue;
        const BcKind btype = face_bc_[face.id];
        if (btype != BcKind::SlipWall && btype != BcKind::NoSlipWall) continue;

        const Index L = face.left;
        const Prims qL = prims_from_state(U_[L], gamma, Rgas);
        const Real S = face_len_[face.id];
        if (S <= 0.0) continue;
        const Real nxn = face.normal[0] / S;
        const Real nyn = face.normal[1] / S;
        const Real pressure = qL.p;

        Real tau_xx = 0.0, tau_yy = 0.0, tau_xy = 0.0;
        if (viscous && btype == BcKind::NoSlipWall) {
            const Vec2 dvec = face.centroid - cents_[L];
            const Real d = std::fabs(dvec.dot(face.normal)) / S;
            const Real d_eff = std::max(d, 0.5 * std::sqrt(std::max(vols_[L], 1e-30)));
            if (d_eff > 0.0) {
                wall_stress(qL.u, qL.v, nxn, nyn, d_eff, mu, tau_xx, tau_yy, tau_xy);
            }
        }

        const Real txx_n = tau_xx * nxn + tau_xy * nyn;
        const Real tyy_n = tau_xy * nxn + tau_yy * nyn;
        const Real Fx = (pressure * nxn - txx_n) * S;
        const Real Fy = (pressure * nyn - tyy_n) * S;

        fc.pressure_drag += pressure * nxn * S;
        fc.pressure_lift += pressure * nyn * S;
        fc.viscous_drag += Fx - pressure * nxn * S;
        fc.viscous_lift += Fy - pressure * nyn * S;

        const Real rx = face.centroid[0] - ci_.moment_center[0];
        const Real ry = face.centroid[1] - ci_.moment_center[1];
        fc.cmz += rx * Fy - ry * Fx;

        if (gather_surface) {
            WallRow row;
            row.x = face.centroid[0];
            row.y = face.centroid[1];
            row.nx = nxn;
            row.ny = nyn;
            row.pressure = pressure;
            row.cp = (pressure - ci_.p_inf) / qinf;
            row.cf = 0.0;
            if (viscous && btype == BcKind::NoSlipWall) {
                const Real t_tang = std::sqrt(txx_n * txx_n + tyy_n * tyy_n);
                row.cf = t_tang / qinf / c_ref;
            }
            row.rho = qL.rho;
            row.u = (btype == BcKind::NoSlipWall) ? 0.0 : qL.u;
            row.v = (btype == BcKind::NoSlipWall) ? 0.0 : qL.v;
            row.mach = (btype == BcKind::NoSlipWall) ? 0.0 : (std::sqrt(qL.u * qL.u + qL.v * qL.v) / qL.a);
            row.tag = face_tag_[face.id];
            rows.push_back(row);
        }
    }

    Real local[5] = {fc.pressure_drag, fc.pressure_lift,
                     fc.viscous_drag, fc.viscous_lift, fc.cmz};
    Real global[5];
    MPI_Allreduce(local, global, 5, MPI_DOUBLE, MPI_SUM, comm_);
    fc.pressure_drag = global[0] / qinf / a_ref;
    fc.pressure_lift = global[1] / qinf / a_ref;
    fc.viscous_drag = global[2] / qinf / a_ref;
    fc.viscous_lift = global[3] / qinf / a_ref;
    fc.cmz = global[4] / qinf / a_ref / c_ref;
    fc.cl = fc.pressure_lift + fc.viscous_lift;
    fc.cd = fc.pressure_drag + fc.viscous_drag;

    if (gather_surface) {
        const int n = (int)rows.size();
        std::vector<int> counts(n_ranks_), displs(n_ranks_);
        MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
        int total = 0;
        for (int r = 0; r < n_ranks_; r++) { displs[r] = total; total += counts[r]; }
        constexpr int NP = 11;
        std::vector<Real> lbuf(n * NP), rbuf(total * NP);
        for (int i = 0; i < n; i++) {
            lbuf[i * NP + 0] = rows[i].x;
            lbuf[i * NP + 1] = rows[i].y;
            lbuf[i * NP + 2] = rows[i].nx;
            lbuf[i * NP + 3] = rows[i].ny;
            lbuf[i * NP + 4] = rows[i].pressure;
            lbuf[i * NP + 5] = rows[i].cp;
            lbuf[i * NP + 6] = rows[i].cf;
            lbuf[i * NP + 7] = rows[i].rho;
            lbuf[i * NP + 8] = rows[i].u;
            lbuf[i * NP + 9] = rows[i].v;
            lbuf[i * NP + 10] = rows[i].mach;
        }
        std::vector<int> recvcounts(n_ranks_), recvdispls(n_ranks_);
        for (int r = 0; r < n_ranks_; r++) {
            recvcounts[r] = counts[r] * NP;
            recvdispls[r] = displs[r] * NP;
        }
        MPI_Gatherv(lbuf.data(), n * NP, MPI_DOUBLE, rbuf.data(),
                    recvcounts.data(), recvdispls.data(), MPI_DOUBLE, 0, comm_);
        if (rank_ == 0) {
            wall_rows_.clear();
            for (int r = 0; r < n_ranks_; r++) {
                for (int k = 0; k < counts[r]; k++) {
                    WallRow row;
                    const int off = (displs[r] + k) * NP;
                    row.x = rbuf[off + 0];
                    row.y = rbuf[off + 1];
                    row.nx = rbuf[off + 2];
                    row.ny = rbuf[off + 3];
                    row.pressure = rbuf[off + 4];
                    row.cp = rbuf[off + 5];
                    row.cf = rbuf[off + 6];
                    row.rho = rbuf[off + 7];
                    row.u = rbuf[off + 8];
                    row.v = rbuf[off + 9];
                    row.mach = rbuf[off + 10];
                    row.tag = "wall";
                    wall_rows_.push_back(row);
                }
            }
        }
    }
    return fc;
}

void Solver::write_surface_csv(const std::string& path) {
    if (rank_ != 0) return;
    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot write surface file: " + path);
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    f << std::setprecision(12);
    for (const auto& r : wall_rows_) {
        f << r.x << "," << r.y << "," << r.nx << "," << r.ny << ","
          << r.pressure << "," << r.cp << "," << r.cf << "," << r.rho << ","
          << r.u << "," << r.v << "," << r.mach << "," << r.tag << "\n";
    }
}
} // namespace cfd
