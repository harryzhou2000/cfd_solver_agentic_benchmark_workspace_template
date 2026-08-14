#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace {
constexpr int RHO = 0, RHU = 1, RHV = 2, RHOE = 3;
}

// Allreduce'd logical OR for loop-termination decisions. MPI reductions can
// return last-ULP-different values across ranks; a borderline break condition
// would otherwise desynchronize ranks and deadlock the halo exchange.
static bool collective_or(MPI_Comm comm, bool local) {
    int l = local ? 1 : 0, g = 0;
    MPI_Allreduce(&l, &g, 1, MPI_INT, MPI_MAX, comm);
    return g != 0;
}

Solver::Solver(MPI_Comm comm, LocalMesh lm, const CaseConfig& cfg, const SolverConfig& scfg)
    : comm_(comm), lm_(std::move(lm)), cfg_(cfg), scfg_(scfg) {
    MPI_Comm_rank(comm_, &rank_);
    MPI_Comm_size(comm_, &nranks_);
    gas_.gamma = cfg_.gas.gamma;
    gas_.R = cfg_.gas.R;
    gas_.prandtl = cfg_.gas.prandtl;
    mu_ = cfg_.viscosity();
    k_ = (mu_ > 0.0) ? mu_ * gas_.cp() / gas_.prandtl : 0.0;
    Uinf_ = cons_from_prim(cfg_.freestream.rho, cfg_.freestream.u, cfg_.freestream.v,
                           cfg_.freestream.pressure, gas_);
    double vm = cfg_.freestream.vel_mag;
    qinf_ = 0.5 * cfg_.freestream.rho * vm * vm;
    rhofloor_ = 1e-8 * cfg_.freestream.rho;
    pfloor_ = 1e-8 * cfg_.freestream.pressure;

    int nc = lm_.n_cell, no = lm_.n_own;
    U_.assign(4 * nc, 0.0);
    R_.assign(4 * no, 0.0);
    grad_.assign(8 * nc, 0.0);
    phi_.assign(4 * nc, 1.0);
    dt_.assign(no, 0.0);
    sface_.assign(lm_.faces.size(), 0.0);
    svface_.assign(lm_.faces.size(), 0.0);
    dU_.assign(4 * nc, 0.0);
    dU_old_.assign(4 * nc, 0.0);
    dU_prev_.assign(4 * nc, 0.0);
    diag_.assign(no, 0.0);
    Un_.assign(4 * no, 0.0);
    Unm1_.assign(4 * no, 0.0);
    build_cell_face_adj();
}

std::string Solver::utc_now() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

void Solver::build_cell_face_adj() {
    cf_off_.assign(lm_.n_own + 1, 0);
    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        if (f.c0 < lm_.n_own) cf_off_[f.c0 + 1]++;
        if (f.c1 >= 0 && f.c1 < lm_.n_own) cf_off_[f.c1 + 1]++;
    }
    for (int i = 0; i < lm_.n_own; i++) cf_off_[i + 1] += cf_off_[i];
    cf_face_.resize(cf_off_[lm_.n_own]);
    cf_side_.resize(cf_off_[lm_.n_own]);
    std::vector<int> pos(cf_off_.begin(), cf_off_.end());
    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        if (f.c0 < lm_.n_own) {
            cf_face_[pos[f.c0]] = (int)fi;
            cf_side_[pos[f.c0]++] = 0;
        }
        if (f.c1 >= 0 && f.c1 < lm_.n_own) {
            cf_face_[pos[f.c1]] = (int)fi;
            cf_side_[pos[f.c1]++] = 1;
        }
    }
}

void Solver::halo_exchange(std::vector<double>& field, int ncomp) {
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<double>> sbuf(lm_.halos.size()), rbuf(lm_.halos.size());
    reqs.reserve(2 * lm_.halos.size());
    for (size_t h = 0; h < lm_.halos.size(); h++) {
        auto& halo = lm_.halos[h];
        rbuf[h].resize(halo.recv_cells.size() * ncomp);
        reqs.push_back(MPI_Request());
        MPI_Irecv(rbuf[h].data(), (int)rbuf[h].size(), MPI_DOUBLE, halo.neighbor, 42, comm_,
                  &reqs.back());
    }
    for (size_t h = 0; h < lm_.halos.size(); h++) {
        auto& halo = lm_.halos[h];
        sbuf[h].resize(halo.send_cells.size() * ncomp);
        for (size_t k = 0; k < halo.send_cells.size(); k++) {
            int c = halo.send_cells[k];
            for (int m = 0; m < ncomp; m++) sbuf[h][k * ncomp + m] = field[c * ncomp + m];
        }
        reqs.push_back(MPI_Request());
        MPI_Isend(sbuf[h].data(), (int)sbuf[h].size(), MPI_DOUBLE, halo.neighbor, 42, comm_,
                  &reqs.back());
    }
    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (size_t h = 0; h < lm_.halos.size(); h++) {
        auto& halo = lm_.halos[h];
        for (size_t k = 0; k < halo.recv_cells.size(); k++) {
            int c = halo.recv_cells[k];
            for (int m = 0; m < ncomp; m++) field[c * ncomp + m] = rbuf[h][k * ncomp + m];
        }
    }
}

void Solver::primitive(int c, double& rho, double& u, double& v, double& p) const {
    prim_from_cons(*reinterpret_cast<const Vec4*>(&U_[4 * c]), gas_, rho, u, v, p);
}

// Reconstruct primitive state (rho,u,v,p) at face center from cell c using
// limited gradients; falls back to first order if positivity is violated.
void Solver::reconstruct_face(const LocalMesh::LFace& f, int c, double* W) const {
    double rho, u, v, p;
    primitive(c, rho, u, v, p);
    static const bool fo_bc = std::getenv("CFDSOLVE_FO_BC") != nullptr;
    static const double fo_vol = []() {
        const char* e = std::getenv("CFDSOLVE_FO_VOL");
        return e ? std::atof(e) : 0.0;
    }();
    static const double fo_x = []() { // first-order downstream of this x (vortex sheet)
        const char* e = std::getenv("CFDSOLVE_FO_X");
        return e ? std::atof(e) : 1e300;
    }();
    if (!scfg_.second_order || (fo_bc && f.c1 < 0) ||
        (fo_vol > 0.0 && lm_.cell_vol[c] < fo_vol) || lm_.cell_cx[c] > fo_x) {
        W[0] = rho; W[1] = u; W[2] = v; W[3] = p;
        return;
    }
    double dx = f.cx - lm_.cell_cx[c], dy = f.cy - lm_.cell_cy[c];
    double vars[4] = {rho, u, v, p};
    double out[4];
    for (int m = 0; m < 4; m++) {
        double g0 = grad_[8 * c + 2 * m], g1 = grad_[8 * c + 2 * m + 1];
        out[m] = vars[m] + phi_[4 * c + m] * (g0 * dx + g1 * dy);
    }
    if (out[0] < rhofloor_ || out[3] < pfloor_) {
        out[0] = rho; out[1] = u; out[2] = v; out[3] = p; // positivity fallback
    }
    W[0] = out[0]; W[1] = out[1]; W[2] = out[2]; W[3] = out[3];
}

void Solver::compute_gradients_limiter() {
    // unweighted least-squares gradients on primitive variables using
    // face-connected neighbors (owned cells only; ghosts filled by exchange)
    std::vector<double> W(4 * lm_.n_cell);
    for (int c = 0; c < lm_.n_cell; c++) {
        double rho, u, v, p;
        primitive(c, rho, u, v, p);
        W[4 * c + 0] = rho; W[4 * c + 1] = u; W[4 * c + 2] = v; W[4 * c + 3] = p;
    }
    std::vector<double> wmin(4 * lm_.n_own), wmax(4 * lm_.n_own);
    for (int i = 0; i < lm_.n_own; i++) {
        double mxx[3] = {0, 0, 0}; // Sxx, Sxy, Syy
        double bx[4] = {0, 0, 0, 0}, by[4] = {0, 0, 0, 0};
        double lo[4] = {W[4 * i], W[4 * i + 1], W[4 * i + 2], W[4 * i + 3]};
        double hi[4] = {lo[0], lo[1], lo[2], lo[3]};
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            const auto& f = lm_.faces[cf_face_[k]];
            int j = (cf_side_[k] == 0) ? f.c1 : f.c0;
            if (j < 0) continue; // boundary face: no neighbor
            double dx = lm_.cell_cx[j] - lm_.cell_cx[i];
            double dy = lm_.cell_cy[j] - lm_.cell_cy[i];
            // inverse-distance-squared weighting (well conditioned on
            // high-aspect-ratio viscous cells)
            double w = 1.0 / (dx * dx + dy * dy);
            mxx[0] += w * dx * dx;
            mxx[1] += w * dx * dy;
            mxx[2] += w * dy * dy;
            for (int m = 0; m < 4; m++) {
                double dw = W[4 * j + m] - W[4 * i + m];
                bx[m] += w * dx * dw;
                by[m] += w * dy * dw;
                lo[m] = std::min(lo[m], W[4 * j + m]);
                hi[m] = std::max(hi[m], W[4 * j + m]);
            }
        }
        double det = mxx[0] * mxx[2] - mxx[1] * mxx[1];
        if (std::abs(det) < 1e-30) det = (det < 0 ? -1e-30 : 1e-30);
        for (int m = 0; m < 4; m++) {
            grad_[8 * i + 2 * m] = (mxx[2] * bx[m] - mxx[1] * by[m]) / det;
            grad_[8 * i + 2 * m + 1] = (-mxx[1] * bx[m] + mxx[0] * by[m]) / det;
            wmin[4 * i + m] = lo[m];
            wmax[4 * i + m] = hi[m];
        }
    }
    // limiter: phi_i = min over reconstruction points
    static const bool no_limiter = std::getenv("CFDSOLVE_NOLIMITER") != nullptr;
    static const bool venkat = std::getenv("CFDSOLVE_BJ") == nullptr; // default Venkatakrishnan
    for (int i = 0; i < lm_.n_own; i++) {
        for (int m = 0; m < 4; m++) phi_[4 * i + m] = 1.0;
        if (no_limiter) continue;
        // Venkatakrishnan smoothing scale from local cell size
        double h = std::sqrt(lm_.cell_vol[i]);
        double eps2 = 0.3 * 0.3 * 0.3 * h * h * h; // (K h)^3, K = 0.3
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            const auto& f = lm_.faces[cf_face_[k]];
            double dx = f.cx - lm_.cell_cx[i], dy = f.cy - lm_.cell_cy[i];
            for (int m = 0; m < 4; m++) {
                double delta = grad_[8 * i + 2 * m] * dx + grad_[8 * i + 2 * m + 1] * dy;
                double phi = 1.0;
                if (venkat) {
                    if (delta > 1e-20) {
                        double dp = wmax[4 * i + m] - W[4 * i + m];
                        phi = (dp * dp + 2.0 * delta * dp + eps2) /
                              (dp * dp + delta * dp + 2.0 * delta * delta + eps2);
                    } else if (delta < -1e-20) {
                        double dm = wmin[4 * i + m] - W[4 * i + m];
                        phi = (dm * dm + 2.0 * delta * dm + eps2) /
                              (dm * dm + delta * dm + 2.0 * delta * delta + eps2);
                    }
                } else {
                    if (delta > 1e-20)
                        phi = std::min(1.0, (wmax[4 * i + m] - W[4 * i + m]) / delta);
                    else if (delta < -1e-20)
                        phi = std::min(1.0, (wmin[4 * i + m] - W[4 * i + m]) / delta);
                }
                phi = std::max(0.0, std::min(1.0, phi));
                phi_[4 * i + m] = std::min(phi_[4 * i + m], phi);
            }
        }
    }
    // make gradients and limiters available on ghost cells
    halo_exchange(grad_, 8);
    halo_exchange(phi_, 4);
}

void Solver::boundary_state(const LocalMesh::LFace& f, const double* WL, Vec4& UR) const {
    switch (f.bc) {
    case BCType::Farfield:
        UR = Uinf_;
        break;
    case BCType::SlipWall: {
        Vec4 ULc = cons_from_prim(WL[0], WL[1], WL[2], WL[3], gas_);
        UR = slip_wall_mirror(ULc, f.nx, f.ny);
        break;
    }
    case BCType::NoSlipAdiabatic: {
        // for the inviscid part: mirror normal velocity (pressure wall flux)
        Vec4 ULc = cons_from_prim(WL[0], WL[1], WL[2], WL[3], gas_);
        UR = slip_wall_mirror(ULc, f.nx, f.ny);
        break;
    }
    default:
        UR = Uinf_;
    }
}

Vec4 Solver::face_inviscid_flux(const LocalMesh::LFace& f, const Vec4& UL, const Vec4& UR,
                                double& lambda) const {
    if (scfg_.inviscid_flux == "hllc")
        return hllc_flux(UL, UR, f.nx, f.ny, gas_, lambda);
    return rusanov_flux(UL, UR, f.nx, f.ny, gas_, scfg_.rusanov_dissipation_scale, lambda,
                        scfg_.low_mach_mfloor);
}

// Viscous normal flux through face. i: left cell, j: right cell (-1 boundary).
void Solver::add_viscous_flux(const LocalMesh::LFace& f, int i, int j, Vec4& Fv) const {
    Fv = {0.0, 0.0, 0.0, 0.0};
    if (mu_ <= 0.0) return;
    double ri, ui, vi, pi;
    primitive(i, ri, ui, vi, pi);
    double Ti = pi / (ri * gas_.R);

    double gux, guy, gvx, gvy, gTx, gTy;
    double uf, vf;
    if (j >= 0) {
        double rj, uj, vj, pj;
        primitive(j, rj, uj, vj, pj);
        double Tj = pj / (rj * gas_.R);
        // average cell gradients with correction along d = xj - xi
        double dx = lm_.cell_cx[j] - lm_.cell_cx[i];
        double dy = lm_.cell_cy[j] - lm_.cell_cy[i];
        double d2 = dx * dx + dy * dy;
        auto face_grad = [&](int varidx, double wi, double wj, double& gx, double& gy) {
            // grad_ layout: 8 per cell, (rho,u,v,p) x (dx,dy)
            double gi_x = 0, gi_y = 0, gj_x = 0, gj_y = 0;
            if (varidx == 4) { // temperature derived from p/(rho R)
                double Ti_ = wi, Tj_ = wj;
                (void)Ti_; (void)Tj_;
            }
            gi_x = grad_[8 * i + 2 * varidx];
            gi_y = grad_[8 * i + 2 * varidx + 1];
            gj_x = grad_[8 * j + 2 * varidx];
            gj_y = grad_[8 * j + 2 * varidx + 1];
            gx = 0.5 * (gi_x + gj_x);
            gy = 0.5 * (gi_y + gj_y);
            double corr = (wj - wi) - (gx * dx + gy * dy);
            gx += corr * dx / d2;
            gy += corr * dy / d2;
        };
        face_grad(1, ui, uj, gux, guy);
        face_grad(2, vi, vj, gvx, gvy);
        // temperature gradient from face-corrected average of p and rho grads
        {
            double gp_x, gp_y, gr_x, gr_y;
            face_grad(3, pi, pj, gp_x, gp_y);
            face_grad(0, ri, rj, gr_x, gr_y);
            double rf = 0.5 * (ri + rj), pf = 0.5 * (pi + pj);
            gTx = (gp_x * rf - pf * gr_x) / (rf * rf * gas_.R);
            gTy = (gp_y * rf - pf * gr_y) / (rf * rf * gas_.R);
        }
        uf = 0.5 * (ui + uj);
        vf = 0.5 * (vi + vj);
    } else if (f.bc == BCType::NoSlipAdiabatic) {
        // one-sided normal gradient with u_wall = 0, dT/dn = 0
        double wx = f.cx - lm_.cell_cx[i], wy = f.cy - lm_.cell_cy[i];
        double dn = std::abs(wx * f.nx + wy * f.ny);
        dn = std::max(dn, 1e-30);
        double dudn = (0.0 - ui) / dn, dvdn = (0.0 - vi) / dn;
        gux = dudn * f.nx; guy = dudn * f.ny;
        gvx = dvdn * f.nx; gvy = dvdn * f.ny;
        gTx = 0.0; gTy = 0.0; // adiabatic
        uf = 0.0; vf = 0.0;
    } else {
        // farfield / slip wall: treat as zero viscous flux
        return;
    }

    double div = gux + gvy;
    double txx = 2.0 * mu_ * gux - (2.0 / 3.0) * mu_ * div;
    double tyy = 2.0 * mu_ * gvy - (2.0 / 3.0) * mu_ * div;
    double txy = mu_ * (guy + gvx);
    double qx = -k_ * gTx, qy = -k_ * gTy;
    Fv[0] = 0.0;
    Fv[1] = txx * f.nx + txy * f.ny;
    Fv[2] = txy * f.nx + tyy * f.ny;
    Fv[3] = (txx * uf + txy * vf - qx) * f.nx + (txy * uf + tyy * vf - qy) * f.ny;
}

void Solver::compute_residual() {
    std::fill(R_.begin(), R_.end(), 0.0);
    halo_exchange(U_, 4);
    if (scfg_.second_order) compute_gradients_limiter();

    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        Vec4 flux;
        double lambda;
        if (f.c1 >= 0) {
            double WL[4], WR[4];
            reconstruct_face(f, f.c0, WL);
            reconstruct_face(f, f.c1, WR);
            Vec4 UL = cons_from_prim(WL[0], WL[1], WL[2], WL[3], gas_);
            Vec4 UR = cons_from_prim(WR[0], WR[1], WR[2], WR[3], gas_);
            flux = face_inviscid_flux(f, UL, UR, lambda);
            if (mu_ > 0.0) {
                Vec4 Fv;
                add_viscous_flux(f, f.c0, f.c1, Fv);
                for (int m = 0; m < 4; m++) flux[m] -= Fv[m];
            }
        } else {
            double WL[4];
            reconstruct_face(f, f.c0, WL);
            Vec4 UL = cons_from_prim(WL[0], WL[1], WL[2], WL[3], gas_);
            if (f.bc == BCType::SlipWall || f.bc == BCType::NoSlipAdiabatic) {
                // wall inviscid flux is exactly the pressure flux
                double a = sound_speed(WL[0], WL[3], gas_);
                double un = WL[1] * f.nx + WL[2] * f.ny;
                lambda = std::abs(un) + a;
                flux = {0.0, WL[3] * f.nx, WL[3] * f.ny, 0.0};
            } else {
                Vec4 UR;
                boundary_state(f, WL, UR);
                flux = face_inviscid_flux(f, UL, UR, lambda);
            }
            if (mu_ > 0.0) {
                Vec4 Fv;
                add_viscous_flux(f, f.c0, -1, Fv);
                for (int m = 0; m < 4; m++) flux[m] -= Fv[m];
            }
        }
        for (int m = 0; m < 4; m++) flux[m] *= f.area;
        if (f.c0 < lm_.n_own)
            for (int m = 0; m < 4; m++) R_[4 * f.c0 + m] += flux[m];
        if (f.c1 >= 0 && f.c1 < lm_.n_own)
            for (int m = 0; m < 4; m++) R_[4 * f.c1 + m] -= flux[m];
    }
}

void Solver::compute_spectral_radii() {
    // Optional low-Mach (Turkel-type) scaling of the implicit operator's
    // spectral radius, consistent with the flux scaling.
    const double m_floor = scfg_.low_mach_mfloor;
    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        double r0, u0, v0, p0;
        primitive(f.c0, r0, u0, v0, p0);
        double un0 = u0 * f.nx + v0 * f.ny;
        double a0 = sound_speed(r0, p0, gas_);
        double lam = std::abs(un0) + a0;
        double vmag0 = std::sqrt(u0 * u0 + v0 * v0);
        double r1 = r0;
        double un = std::abs(un0), a = a0, vmag = vmag0;
        if (f.c1 >= 0) {
            double u1, v1, p1;
            primitive(f.c1, r1, u1, v1, p1);
            double un1 = u1 * f.nx + v1 * f.ny;
            double a1 = sound_speed(r1, p1, gas_);
            lam = std::max(lam, std::abs(un1) + a1);
            un = std::max(un, std::abs(un1));
            a = std::max(a, a1);
            vmag = std::max(vmag, std::sqrt(u1 * u1 + v1 * v1));
            r1 = 0.5 * (r0 + r1);
        }
        if (m_floor > 0.0) {
            double m_eff = std::max(vmag / a, m_floor);
            double alpha = std::min(1.0, m_eff * m_eff);
            lam = 0.5 * ((1.0 + alpha) * un +
                         std::sqrt((1.0 - alpha) * (1.0 - alpha) * un * un +
                                   4.0 * alpha * a * a));
        }
        sface_[fi] = lam * f.area;
        if (mu_ > 0.0) {
            double dx, dy;
            if (f.c1 >= 0) {
                dx = lm_.cell_cx[f.c1] - lm_.cell_cx[f.c0];
                dy = lm_.cell_cy[f.c1] - lm_.cell_cy[f.c0];
            } else {
                dx = 2.0 * (f.cx - lm_.cell_cx[f.c0]);
                dy = 2.0 * (f.cy - lm_.cell_cy[f.c0]);
            }
            double dn = std::abs(dx * f.nx + dy * f.ny);
            dn = std::max(dn, 1e-30);
            double coef = std::max(4.0 / 3.0, gas_.gamma / gas_.prandtl);
            svface_[fi] = 2.0 * coef * mu_ * f.area / (r1 * dn);
        } else {
            svface_[fi] = 0.0;
        }
    }
}

void Solver::local_timesteps(double cfl) {
    // the viscous spectral radius restricts the pseudo time step severely on
    // boundary-layer cells; since LU-SGS is implicit we may under-weight it
    // here (it remains fully included in the relaxation diagonal)
    static const double vis_dt_weight = []() {
        const char* e = std::getenv("CFDSOLVE_VIS_DT_WEIGHT");
        return e ? std::atof(e) : 1.0;
    }();
    for (int i = 0; i < lm_.n_own; i++) {
        double lam = 0.0;
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            int fi = cf_face_[k];
            lam += sface_[fi] + vis_dt_weight * svface_[fi];
        }
        dt_[i] = cfl * lm_.cell_vol[i] / std::max(lam, 1e-30);
    }
}

void Solver::lusgs_sweep(bool final_exchange) {
    // forward sweep (lower part)
    for (int i = 0; i < lm_.n_own; i++) {
        double num[4] = {-R_[4 * i], -R_[4 * i + 1], -R_[4 * i + 2], -R_[4 * i + 3]};
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            const auto& f = lm_.faces[cf_face_[k]];
            int j = (cf_side_[k] == 0) ? f.c1 : f.c0;
            if (j < 0) continue;
            double s = 0.5 * (sface_[cf_face_[k]] + svface_[cf_face_[k]]);
            if (j < i)
                for (int m = 0; m < 4; m++) num[m] += s * dU_[4 * j + m];
            else
                for (int m = 0; m < 4; m++) num[m] += s * dU_old_[4 * j + m];
        }
        for (int m = 0; m < 4; m++) dU_[4 * i + m] = num[m] / diag_[i];
    }
    halo_exchange(dU_, 4);
    // backward sweep (upper part)
    for (int i = lm_.n_own - 1; i >= 0; i--) {
        double num[4] = {-R_[4 * i], -R_[4 * i + 1], -R_[4 * i + 2], -R_[4 * i + 3]};
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            const auto& f = lm_.faces[cf_face_[k]];
            int j = (cf_side_[k] == 0) ? f.c1 : f.c0;
            if (j < 0) continue;
            double s = 0.5 * (sface_[cf_face_[k]] + svface_[cf_face_[k]]);
            for (int m = 0; m < 4; m++) num[m] += s * dU_[4 * j + m];
        }
        for (int m = 0; m < 4; m++) dU_[4 * i + m] = num[m] / diag_[i];
    }
    if (final_exchange) halo_exchange(dU_, 4);
}

// One nonlinear pseudo-time step: assemble residual, then run inner LU-SGS
// relaxation sweeps on the frozen linearization. Returns the inner linear
// residual ratio; used_inner reports the number of relaxation sweeps run.
double Solver::nonlinear_step(double cfl, int min_inner, int max_inner, double inner_target,
                              int& used_inner) {
    compute_residual(); // fills R_ and exchanges ghosts
    compute_spectral_radii();
    local_timesteps(cfl);
    // debug: explicit forward-Euler pseudo step (CFDSOLVE_EXPLICIT=1)
    static const bool explicit_mode = std::getenv("CFDSOLVE_EXPLICIT") != nullptr;
    if (explicit_mode) {
        for (int i = 0; i < lm_.n_own; i++)
            for (int m = 0; m < 4; m++)
                dU_[4 * i + m] = -dt_[i] / lm_.cell_vol[i] * R_[4 * i + m];
        used_inner = 1;
        apply_update(true);
        return 1.0;
    }
    // residual norm before the update (for the backtracking line search)
    double loc = 0.0, glob_old = 0.0;
    for (int i = 0; i < 4 * lm_.n_own; i++) loc += R_[i] * R_[i];
    MPI_Allreduce(&loc, &glob_old, 1, MPI_DOUBLE, MPI_SUM, comm_);
    double rnorm_old = std::sqrt(glob_old);
    for (int i = 0; i < lm_.n_own; i++) {
        double lam = 0.0;
        for (int k = cf_off_[i]; k < cf_off_[i + 1]; k++) {
            int fi = cf_face_[k];
            lam += 0.5 * (sface_[fi] + svface_[fi]);
        }
        // diagonal over-weighting factor (stabilizes high-CFL outer iteration);
        // 1.5 found robust for all supplied cases
        static const double diag_boost = []() {
            const char* e = std::getenv("CFDSOLVE_DIAG_BOOST");
            return e ? std::atof(e) : 1.5;
        }();
        diag_[i] = lm_.cell_vol[i] / dt_[i] + diag_boost * lam;
    }
    std::fill(dU_.begin(), dU_.end(), 0.0);
    std::fill(dU_old_.begin(), dU_old_.end(), 0.0);

    double ratio = 1.0, prev_change = 0.0;
    int sweep = 0;
    for (sweep = 1; sweep <= max_inner; sweep++) {
        dU_prev_ = dU_;
        dU_old_ = dU_;
        lusgs_sweep();
        // measure inner linear residual via update increment
        double loc = 0.0, glob_change, glob_ref = 0.0, loc_ref = 0.0;
        for (int i = 0; i < 4 * lm_.n_own; i++) {
            double dch = dU_[i] - dU_prev_[i];
            loc += dch * dch;
            loc_ref += dU_[i] * dU_[i];
        }
        MPI_Allreduce(&loc, &glob_change, 1, MPI_DOUBLE, MPI_SUM, comm_);
        MPI_Allreduce(&loc_ref, &glob_ref, 1, MPI_DOUBLE, MPI_SUM, comm_);
        double change = std::sqrt(glob_change);
        double ref = std::sqrt(std::max(glob_ref, 1e-300));
        if (sweep == 1) prev_change = std::max(change, 1e-300);
        ratio = change / prev_change;
        if (sweep >= min_inner &&
            collective_or(comm_, ratio < inner_target || change < 1e-14 * ref))
            break;
    }
    used_inner = std::min(sweep, max_inner);
    // Armijo backtracking line search on the global update: save state, try
    // decreasing global scales until the residual does not increase
    static const bool no_linesearch = std::getenv("CFDSOLVE_NO_LINESEARCH") != nullptr;
    if (no_linesearch) {
        apply_update(true);
        return ratio;
    }
    std::vector<double> U_save(U_.begin(), U_.begin() + 4 * lm_.n_own);
    std::vector<double> dU_save(dU_.begin(), dU_.begin() + 4 * lm_.n_own);
    double alpha = 1.0;
    for (int bt = 0; bt < 5; bt++) {
        for (int i = 0; i < 4 * lm_.n_own; i++) dU_[i] = alpha * dU_save[i];
        for (int i = 0; i < 4 * lm_.n_own; i++) U_[i] = U_save[i];
        apply_update(true);
        if (bt == 4) break; // accept the smallest scale unconditionally
        compute_residual();
        double loc2 = 0.0, glob_new = 0.0;
        for (int i = 0; i < 4 * lm_.n_own; i++) loc2 += R_[i] * R_[i];
        MPI_Allreduce(&loc2, &glob_new, 1, MPI_DOUBLE, MPI_SUM, comm_);
        // collective decision: reductions may differ by one ulp across ranks
        if (collective_or(comm_, std::sqrt(glob_new) <= rnorm_old)) break;
        alpha *= 0.5;
        line_search_backtracks_++;
    }
    return ratio;
}

void Solver::apply_update(bool use_relax) {
    for (int i = 0; i < lm_.n_own; i++) {
        double scale = 1.0;
        if (use_relax) {
            // limit relative change of density and pressure per pseudo step
            double rho0 = U_[4 * i], p0 = pressure_of(*reinterpret_cast<Vec4*>(&U_[4 * i]), gas_);
            for (int halve = 0; halve < 20; halve++) {
                double rho = U_[4 * i] + scale * dU_[4 * i];
                double ru = U_[4 * i + 1] + scale * dU_[4 * i + 1];
                double rv = U_[4 * i + 2] + scale * dU_[4 * i + 2];
                double rE = U_[4 * i + 3] + scale * dU_[4 * i + 3];
                double ke = 0.5 * (ru * ru + rv * rv) / std::max(rho, 1e-300);
                double p = (gas_.gamma - 1.0) * (rE - ke);
                bool ok = rho > rhofloor_ && p > pfloor_;
                if (ok) {
                    // clip to at most 20% change in rho and p
                    if (std::abs(rho - rho0) > 0.2 * rho0 || std::abs(p - p0) > 0.2 * p0) ok = false;
                }
                if (ok) break;
                scale *= 0.5;
                positivity_fixes_++;
            }
        }
        for (int m = 0; m < 4; m++) U_[4 * i + m] += scale * dU_[4 * i + m];
    }
}

ResidualNorms Solver::residual_norms(const std::vector<double>& R) const {
    double loc[6] = {0, 0, 0, 0, 0, 0};
    double locmax = 0.0;
    for (int i = 0; i < lm_.n_own; i++) {
        double inv_vol = 1.0 / lm_.cell_vol[i];
        for (int m = 0; m < 4; m++) {
            double r = R[4 * i + m] * inv_vol;
            loc[m] += r * r;
            loc[4] += r * r;
            locmax = std::max(locmax, std::abs(r));
        }
    }
    loc[5] = (double)lm_.n_own;
    double glob[6];
    MPI_Allreduce(loc, glob, 6, MPI_DOUBLE, MPI_SUM, comm_);
    double gmax;
    MPI_Allreduce(&locmax, &gmax, 1, MPI_DOUBLE, MPI_MAX, comm_);
    ResidualNorms rn;
    double n = std::max(glob[5], 1.0);
    for (int m = 0; m < 4; m++) rn.comp[m] = std::sqrt(glob[m] / n);
    rn.l2 = std::sqrt(glob[4] / (4.0 * n));
    rn.linf = gmax;
    return rn;
}

ResidualNorms Solver::current_residual_norms() {
    compute_residual();
    return residual_norms(R_);
}

void Solver::initialize() {
    for (int c = 0; c < lm_.n_cell; c++)
        for (int m = 0; m < 4; m++) U_[4 * c + m] = Uinf_[m];
}

void Solver::load_owned_state(const std::vector<double>& owned_U) {
    // owned_U holds 4 values per owned cell in local owned order
    for (int i = 0; i < 4 * lm_.n_own; i++) U_[i] = owned_U[i];
    // ghosts are refreshed by the first halo exchange in compute_residual
}

void Solver::add_transverse_perturbation(double eps) {
    for (int i = 0; i < lm_.n_cell; i++) {
        double x = lm_.cell_cx[i], y = lm_.cell_cy[i];
        double r2 = x * x + y * y;
        if (r2 > 4.0) continue;
        double rho = U_[4 * i];
        double u = U_[4 * i + 1] / rho;
        double v = U_[4 * i + 2] / rho;
        double dv = eps * y * std::exp(-r2);
        U_[4 * i + 2] += rho * dv;
        U_[4 * i + 3] += 0.5 * ((v + dv) * (v + dv) - v * v);
    }
}

Forces Solver::compute_forces() const {
    double vm = cfg_.freestream.vel_mag;
    double dhat_x = cfg_.freestream.u / vm, dhat_y = cfg_.freestream.v / vm;
    double lhat_x = -dhat_y, lhat_y = dhat_x;
    double refA = cfg_.reference.area, refL = cfg_.reference.length;
    double x0 = cfg_.reference.moment_center[0], y0 = cfg_.reference.moment_center[1];

    double Fx_p = 0, Fy_p = 0, Fx_v = 0, Fy_v = 0, Mz = 0;
    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        if (f.c1 >= 0) continue;
        if (f.bc != BCType::SlipWall && f.bc != BCType::NoSlipAdiabatic) continue;
        double WL[4];
        reconstruct_face(f, f.c0, WL);
        double pw = WL[3];
        double fpx = pw * f.nx * f.area, fpy = pw * f.ny * f.area;
        double fvx = 0.0, fvy = 0.0;
        Fx_p += fpx; Fy_p += fpy;
        if (f.bc == BCType::NoSlipAdiabatic && mu_ > 0.0) {
            // tangential wall shear only
            double wx = f.cx - lm_.cell_cx[f.c0], wy = f.cy - lm_.cell_cy[f.c0];
            double dn = std::abs(wx * f.nx + wy * f.ny);
            dn = std::max(dn, 1e-30);
            double rho, u, v, p;
            primitive(f.c0, rho, u, v, p);
            double dudn = (0.0 - u) / dn, dvdn = (0.0 - v) / dn;
            double gx_u = dudn * f.nx, gy_u = dudn * f.ny;
            double gx_v = dvdn * f.nx, gy_v = dvdn * f.ny;
            double div = gx_u + gy_v;
            double txx = 2 * mu_ * gx_u - (2.0 / 3.0) * mu_ * div;
            double tyy = 2 * mu_ * gy_v - (2.0 / 3.0) * mu_ * div;
            double txy = mu_ * (gy_u + gx_v);
            double tnx = txx * f.nx + txy * f.ny;
            double tny = txy * f.nx + tyy * f.ny;
            // tangential component of traction (skin friction); the force on
            // the body from viscous shear is -(tau.n)|_tangential
            double tn_n = tnx * f.nx + tny * f.ny;
            double tx = tnx - tn_n * f.nx;
            double ty = tny - tn_n * f.ny;
            fvx = -tx * f.area; fvy = -ty * f.area;
            Fx_v += fvx; Fy_v += fvy;
        }
        Mz += (f.cx - x0) * (fpy + fvy) - (f.cy - y0) * (fpx + fvx);
    }
    double glo[5], loc[5] = {Fx_p, Fy_p, Fx_v, Fy_v, Mz};
    MPI_Allreduce(loc, glo, 5, MPI_DOUBLE, MPI_SUM, const_cast<MPI_Comm&>(comm_));
    Forces F;
    double Fx = glo[0] + glo[2], Fy = glo[1] + glo[3];
    F.pressure_drag = (glo[0] * dhat_x + glo[1] * dhat_y) / (qinf_ * refA);
    F.pressure_lift = (glo[0] * lhat_x + glo[1] * lhat_y) / (qinf_ * refA);
    F.viscous_drag = (glo[2] * dhat_x + glo[3] * dhat_y) / (qinf_ * refA);
    F.viscous_lift = (glo[2] * lhat_x + glo[3] * lhat_y) / (qinf_ * refA);
    F.cd = (Fx * dhat_x + Fy * dhat_y) / (qinf_ * refA);
    F.cl = (Fx * lhat_x + Fy * lhat_y) / (qinf_ * refA);
    F.cmz = glo[4] / (qinf_ * refA * refL);
    return F;
}

std::vector<SurfaceRow> Solver::surface_rows() const {
    std::vector<SurfaceRow> rows;
    for (size_t fi = 0; fi < lm_.faces.size(); fi++) {
        const auto& f = lm_.faces[fi];
        if (f.c1 >= 0) continue;
        if (f.bc != BCType::SlipWall && f.bc != BCType::NoSlipAdiabatic) continue;
        double WL[4];
        reconstruct_face(f, f.c0, WL);
        SurfaceRow r;
        r.x = f.cx; r.y = f.cy;
        r.nx = f.nx; r.ny = f.ny;
        r.pressure = WL[3];
        r.cp = (WL[3] - cfg_.freestream.pressure) / qinf_;
        r.tag = f.bc_name;
        double cf = 0.0;
        if (f.bc == BCType::NoSlipAdiabatic) {
            // boundary-condition state: u = v = 0 at the wall
            r.u = 0.0; r.v = 0.0; r.mach = 0.0;
            double Tw = WL[3] / (WL[0] * gas_.R); // adiabatic: T_wall = T_cell-face
            r.rho = WL[3] / (gas_.R * Tw);
            if (mu_ > 0.0) {
                double wx = f.cx - lm_.cell_cx[f.c0], wy = f.cy - lm_.cell_cy[f.c0];
                double dn = std::abs(wx * f.nx + wy * f.ny);
                dn = std::max(dn, 1e-30);
                double rho, u, v, p;
                primitive(f.c0, rho, u, v, p);
                double dudn = (0.0 - u) / dn, dvdn = (0.0 - v) / dn;
                double gx_u = dudn * f.nx, gy_u = dudn * f.ny;
                double gx_v = dvdn * f.nx, gy_v = dvdn * f.ny;
                double div = gx_u + gy_v;
                double txx = 2 * mu_ * gx_u - (2.0 / 3.0) * mu_ * div;
                double tyy = 2 * mu_ * gy_v - (2.0 / 3.0) * mu_ * div;
                double txy = mu_ * (gy_u + gx_v);
                double tnx = txx * f.nx + txy * f.ny;
                double tny = txy * f.nx + tyy * f.ny;
                double tn_n = tnx * f.nx + tny * f.ny;
                double ttx = tnx - tn_n * f.nx, tty = tny - tn_n * f.ny;
                // wall shear on the body along the local surface tangent
                // t = (ny, -nx): cf = -(tau.n).t / q
                cf = -(ttx * f.ny - tty * f.nx) / qinf_;
            }
        } else {
            // slip wall: boundary state has zero normal velocity
            double un = WL[1] * f.nx + WL[2] * f.ny;
            r.u = WL[1] - un * f.nx;
            r.v = WL[2] - un * f.ny;
            double a = sound_speed(WL[0], WL[3], gas_);
            r.mach = std::sqrt(r.u * r.u + r.v * r.v) / a;
            r.rho = WL[0];
        }
        r.cf = cf;
        rows.push_back(r);
    }
    return rows;
}

// ------------------------------ steady solve ------------------------------

std::string Solver::solve_steady(const std::string& out_dir) {
    start_time_utc_ = utc_now();
    auto t_start = std::chrono::steady_clock::now();
    const auto& rc = cfg_.run_control;

    std::ofstream res_file, force_file;
    if (rank_ == 0) {
        res_file.open(out_dir + "/residuals.csv");
        res_file << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
                    "residual_linf\n";
        force_file.open(out_dir + "/forces.csv");
        force_file << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
                      "viscous_lift\n";
    }

    // initial residual for reduction reference
    compute_residual();
    ResidualNorms rn0 = residual_norms(R_);
    double ref = std::max(rn0.l2, 1e-300);

    std::string status = "max_steps";
    double cfl = rc.cfl_initial;
    double min_dt = 0.0;
    for (step_ = 1; step_ <= rc.max_steps; step_++) {
        if (rc.pseudo_cfl_ramp_steps > 0) {
            double t = std::min(1.0, (double)(step_ - 1) / rc.pseudo_cfl_ramp_steps);
            cfl = rc.cfl_initial + (rc.cfl_max - rc.cfl_initial) * t;
        } else {
            cfl = rc.cfl_max;
        }
        int used_inner = 0;
        double inner_ratio = nonlinear_step(cfl, rc.min_inner_iterations,
                                            rc.max_inner_iterations,
                                            rc.inner_residual_reduction_target, used_inner);
        inner_stats_.total_steps++;
        inner_stats_.sum_inner += used_inner;
        inner_stats_.min_inner = std::min(inner_stats_.min_inner, used_inner);
        inner_stats_.max_inner = std::max(inner_stats_.max_inner, used_inner);
        if (inner_ratio > rc.inner_residual_reduction_target) inner_stats_.target_misses++;
        inner_stats_.last_inner_residual_ratio = inner_ratio;

        // norms of the new state residual
        ResidualNorms rn = current_residual_norms();
        double loc_min_dt = 1e300;
        for (int i = 0; i < lm_.n_own; i++) loc_min_dt = std::min(loc_min_dt, dt_[i]);
        MPI_Allreduce(&loc_min_dt, &min_dt, 1, MPI_DOUBLE, MPI_MIN, comm_);

        double reduction = std::log10(ref / std::max(rn.l2, 1e-300));
        res_reduction_orders_ = reduction;

        if (rank_ == 0 && (step_ % cfg_.outputs.write_residuals_every == 0)) {
            res_file << step_ << ",0.0," << used_inner << "," << cfl << "," << min_dt << ","
                     << rn.comp[0] << "," << rn.comp[1] << "," << rn.comp[2] << "," << rn.comp[3]
                     << "," << rn.l2 << "," << rn.linf << "\n";
        }
        if (step_ % cfg_.outputs.write_forces_every == 0 || step_ == rc.max_steps) {
            Forces F = compute_forces();
            if (rank_ == 0) {
                force_file << step_ << ",0.0," << F.cl << "," << F.cd << "," << F.cmz << ","
                           << F.pressure_drag << "," << F.viscous_drag << "," << F.pressure_lift
                           << "," << F.viscous_lift << "\n";
            }
        }
        if (rank_ == 0 && step_ % 50 == 0) {
            res_file.flush();
            force_file.flush();
        }
        if (rank_ == 0 && scfg_.report_level >= 1 && (step_ % 200 == 0 || step_ == 1)) {
            std::printf("[%s] step %ld cfl %.1f res_l2 %.3e (%.2f orders) inner %d\n",
                        cfg_.case_id.c_str(), step_, cfl, rn.l2, reduction, used_inner);
            std::fflush(stdout);
        }
        static const bool debug_res = std::getenv("CFDSOLVE_DEBUG_RES") != nullptr;
        if (debug_res && step_ % 100 == 0) {
            // find global cell with max |R|/Vol
            double loc_max = 0;
            int loc_cell = -1;
            for (int i = 0; i < lm_.n_own; i++) {
                double r = 0;
                for (int m = 0; m < 4; m++) {
                    double v = R_[4 * i + m] / lm_.cell_vol[i];
                    r = std::max(r, std::abs(v));
                }
                if (r > loc_max) { loc_max = r; loc_cell = i; }
            }
            double g_max;
            MPI_Allreduce(&loc_max, &g_max, 1, MPI_DOUBLE, MPI_MAX, comm_);
            if (std::abs(loc_max - g_max) < 1e-30 && loc_cell >= 0 && g_max > 0) {
                double rho, u, v, p;
                primitive(loc_cell, rho, u, v, p);
                std::printf("  [debug] rank %d maxres cell gid %lld at (%.4f,%.4f) vol %.3e "
                            "rho %.4f u %.4f v %.4f p %.4f res %.3e\n",
                            rank_, lm_.cell_global[loc_cell], lm_.cell_cx[loc_cell],
                            lm_.cell_cy[loc_cell], lm_.cell_vol[loc_cell], rho, u, v, p, g_max);
                std::fflush(stdout);
            }
        }
        if (collective_or(comm_, !std::isfinite(rn.l2))) {
            status = "diverged";
            break;
        }
        if (collective_or(comm_, reduction >= rc.residual_reduction_target)) {
            status = "converged";
            break;
        }
    }
    // make sure the last force row matches the final state
    {
        Forces F = compute_forces();
        if (rank_ == 0) {
            // rewrite final row if it was not written this step
            force_file.flush();
        }
        (void)F;
    }
    end_time_utc_ = utc_now();
    wall_time_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    return status;
}

// ---------------------------- transient solve -----------------------------

std::string Solver::solve_transient(const std::string& out_dir) {
    start_time_utc_ = utc_now();
    auto t_start = std::chrono::steady_clock::now();
    const auto& rc = cfg_.run_control;
    const double dt_phys = rc.time_step;
    const long nsteps = rc.max_steps; // caller sets max_steps = final_time/time_step
    static const double cfl = []() { // fixed near 1.0 for Re200 (env override for tuning)
        const char* e = std::getenv("CFDSOLVE_TRANSIENT_CFL");
        return e ? std::atof(e) : 0.0;
    }();
    const double cfl_inner = (cfl > 0.0) ? cfl : rc.cfl_initial;

    std::ofstream res_file, force_file;
    if (rank_ == 0) {
        res_file.open(out_dir + "/residuals.csv");
        res_file << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
                    "residual_linf\n";
        force_file.open(out_dir + "/forces.csv");
        force_file << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
                      "viscous_lift\n";
    }

    // histories: use BDF1 for the first step of this run segment (Unm1 not yet
    // valid at a fresh start or right after a restart)
    for (int i = 0; i < 4 * lm_.n_own; i++) {
        Un_[i] = U_[i];
        Unm1_[i] = U_[i];
    }

    // fixed absolute floor for the inner convergence check: near a steady
    // state the predictor residual is already at the solver noise floor and
    // the relative-ratio check degenerates, so accept steps whose total
    // transient residual is below a small absolute floor. The floor is a tiny
    // fraction of the initial residual scale, far below the residuals that
    // must be reached during genuine transients.
    compute_residual();
    ResidualNorms r0_spatial = residual_norms(R_);
    const double abs_floor = 1e-9 * std::max(r0_spatial.l2, 1.0);

    double last_field_time = phys_time_;
    std::string status = "completed";
    const long start_step = step_ + 1;

    for (step_ = start_step; step_ <= nsteps; step_++) {
        const bool bdf1 = (step_ == start_step);
        const double b0 = bdf1 ? 1.0 : 1.5;
        // src_i = b1*Un + b2*Unm1 scaled: W = Vol*(b0 U - src)/dt + R(U)
        // BDF1: src = Un ; BDF2: src = 2 Un - 0.5 Unm1
        // reference norm of the total transient residual at U = Un
        // W0 = Vol*(b0 Un - src)/dt + R(Un); for BDF1 W0 = R(Un); for BDF2
        // W0 = Vol*(-0.5(Un-Unm1))/dt... compute directly below.

        auto total_residual = [&](std::vector<double>& W) {
            compute_residual(); // R_ from current U_
            for (int i = 0; i < lm_.n_own; i++) {
                double src;
                for (int m = 0; m < 4; m++) {
                    if (bdf1)
                        src = Un_[4 * i + m];
                    else
                        src = 2.0 * Un_[4 * i + m] - 0.5 * Unm1_[4 * i + m];
                    W[4 * i + m] = lm_.cell_vol[i] * (b0 * U_[4 * i + m] - src) / dt_phys +
                                   R_[4 * i + m];
                }
            }
        };

        std::vector<double> W(4 * lm_.n_own);
        // predictor: second-order extrapolation U0 = 2Un - Unm1 (BDF1 step
        // uses U0 = Un)
        if (bdf1) {
            for (int i = 0; i < 4 * lm_.n_own; i++) U_[i] = Un_[i];
        } else {
            for (int i = 0; i < 4 * lm_.n_own; i++)
                U_[i] = 2.0 * Un_[i] - Unm1_[i];
        }
        // compute W0
        total_residual(W);
        ResidualNorms w0 = residual_norms(W);
        double ref = std::max(w0.l2, 1e-300);

        static const int sweeps_per_inner = []() {
            const char* e = std::getenv("CFDSOLVE_INNER_SWEEPS");
            return e ? std::atoi(e) : 3;
        }();
        int inner_used = 0;
        double ratio = 1.0;
        bool hit_target = false;
        double best_wn = 1e300;
        int since_best = 0;
        for (int k = 1; k <= rc.max_inner_iterations; k++) {
            // W holds the total transient residual at the current iterate;
            // solve (b0 Vol/dt + Vol/dtau + J) dU = -W with LU-SGS sweeps
            compute_spectral_radii();
            local_timesteps(cfl_inner);
            std::copy(W.begin(), W.end(), R_.begin());
            for (int i = 0; i < lm_.n_own; i++) {
                double lam = 0.0;
                for (int kk = cf_off_[i]; kk < cf_off_[i + 1]; kk++) {
                    int fi = cf_face_[kk];
                    lam += 0.5 * (sface_[fi] + svface_[fi]);
                }
                diag_[i] = lm_.cell_vol[i] / dt_[i] + b0 * lm_.cell_vol[i] / dt_phys + lam;
            }
            std::fill(dU_.begin(), dU_.end(), 0.0);
            std::fill(dU_old_.begin(), dU_old_.end(), 0.0);
            for (int s = 0; s < sweeps_per_inner; s++) {
                dU_old_ = dU_;
                lusgs_sweep(s + 1 < sweeps_per_inner);
            }
            apply_update(true);
            inner_used = k;
            static const bool dbg_sync = std::getenv("CFDSOLVE_DEBUG_SYNC") != nullptr;
            if (dbg_sync && k % 10 == 0) {
                std::fprintf(stderr, "[rank %d] step %ld k %d\n", rank_, step_, k);
                std::fflush(stderr);
            }
            // re-evaluate the total residual at the new iterate (also
            // refreshes ghost states)
            total_residual(W);
            if (k >= rc.min_inner_iterations || k == rc.max_inner_iterations) {
                ResidualNorms wn = residual_norms(W);
                ratio = wn.l2 / ref;
                bool loc_hit = (ratio <= rc.inner_residual_reduction_target) || (wn.l2 <= abs_floor);
                if (collective_or(comm_, loc_hit)) {
                    hit_target = true;
                    break;
                }
                // conservative stall detection: only give up after a long
                // run without improvement (protects against pathological
                // 1000-iteration grinds without under-converging)
                if (wn.l2 < 0.9 * best_wn) {
                    best_wn = wn.l2;
                    since_best = 0;
                } else {
                    since_best++;
                }
                if (collective_or(comm_, k > 60 && since_best > 25)) break;
            }
        }
        inner_stats_.total_steps++;
        inner_stats_.sum_inner += inner_used;
        inner_stats_.min_inner = std::min(inner_stats_.min_inner, inner_used);
        inner_stats_.max_inner = std::max(inner_stats_.max_inner, inner_used);
        if (!hit_target) inner_stats_.target_misses++;
        inner_stats_.last_inner_residual_ratio = ratio;

        // accept step: update physical histories once
        for (int i = 0; i < 4 * lm_.n_own; i++) {
            Unm1_[i] = Un_[i];
            Un_[i] = U_[i];
        }
        phys_time_ += dt_phys;

        // logging (W holds the accepted-step total residual)
        ResidualNorms rn = residual_norms(W);
        if (collective_or(comm_, !std::isfinite(rn.l2))) {
            status = "diverged";
            break;
        }
        if (rank_ == 0 && (step_ % cfg_.outputs.write_residuals_every == 0)) {
            res_file << step_ << "," << phys_time_ << "," << inner_used << "," << cfl_inner << ","
                     << dt_phys << "," << rn.comp[0] << "," << rn.comp[1] << "," << rn.comp[2]
                     << "," << rn.comp[3] << "," << rn.l2 << "," << rn.linf << "\n";
        }
        if (step_ % cfg_.outputs.write_forces_every == 0) {
            Forces F = compute_forces();
            if (rank_ == 0) {
                force_file << step_ << "," << phys_time_ << "," << F.cl << "," << F.cd << ","
                           << F.cmz << "," << F.pressure_drag << "," << F.viscous_drag << ","
                           << F.pressure_lift << "," << F.viscous_lift << "\n";
            }
        }
        if (rank_ == 0 && step_ % 50 == 0) {
            res_file.flush();
            force_file.flush();
        }
        if (scfg_.report_level >= 1 && (step_ % 500 == 0 || step_ == 1)) {
            Forces F = compute_forces(); // collective: must be called by all ranks
            if (rank_ == 0) {
                std::printf("[%s] t=%.2f step %ld/%ld inner %d ratio %.2e cl %.4f cd %.4f\n",
                            cfg_.case_id.c_str(), phys_time_, step_, nsteps, inner_used, ratio,
                            F.cl, F.cd);
                std::fflush(stdout);
            }
        }
        if (cfg_.outputs.write_field_every_time > 0 && field_callback &&
            phys_time_ - last_field_time >= cfg_.outputs.write_field_every_time - 1e-12) {
            last_field_time = phys_time_;
            field_callback(phys_time_);
        }
        if (restart_callback && step_ % 1000 == 0) restart_callback(step_, phys_time_);
    }
    end_time_utc_ = utc_now();
    wall_time_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
    return status;
}
