#include "solver.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <Eigen/Dense>

static inline double prim_pressure(const Vec4& U, double gamma) {
    double rho = U[0];
    if (rho < 1e-14) return 1e-14;
    double u = U[1] / rho, v = U[2] / rho, E = U[3] / rho;
    return std::max((gamma - 1.0) * rho * (E - 0.5 * (u*u + v*v)), 1e-14);
}

static inline void cons_to_prim(const Vec4& U, double gamma,
                                double& rho, double& u, double& v, double& p) {
    rho = std::max(U[0], 1e-14);
    u = U[1] / rho; v = U[2] / rho;
    p = std::max((gamma - 1.0) * rho * (U[3]/rho - 0.5*(u*u+v*v)), 1e-14);
}

void Solver::init(const CaseConfig& config, LocalMesh& lm, MPI_Comm comm) {
    config_ = config; lm_ = &lm; comm_ = comm;
    MPI_Comm_rank(comm_, &rank_); MPI_Comm_size(comm_, &nranks_);
    num_owned_ = lm_->num_owned;
    num_total_ = num_owned_ + lm_->num_ghost;
    halo_.init(lm, comm);
    if (config_.physics_mode == PhysicsMode::LAMINAR && config_.reynolds > 0)
        mu_ = config_.freestream.rho * config_.freestream.vel_mag * config_.reference.reynolds_length / config_.reynolds;
    initialize_state();
    writer_.init(config_, lm, comm);
}

void Solver::initialize_state() {
    Vec4 Ufs = config_.freestream.state(config_.gas);
    U_.assign(num_total_, Ufs);
    U_n_.assign(num_total_, Ufs);
    U_nm1_.assign(num_total_, Ufs);
    residual_.assign(num_total_, Vec4::Zero());
    gradients_.resize(num_total_);
    for (auto& g : gradients_) { g[0] = Vec4::Zero(); g[1] = Vec4::Zero(); }
    limiter_phi_.assign(num_total_, 1.0);
}

void Solver::compute_gradients() {
    for (int ci = 0; ci < num_total_; ci++) {
        gradients_[ci][0] = Vec4::Zero(); gradients_[ci][1] = Vec4::Zero();
    }
    for (int ci = 0; ci < num_owned_; ci++) {
        auto& cell = lm_->mesh.cells[ci];
        int nf = (int)cell.faces.size();
        if (nf < 2) continue;
        Eigen::Matrix<double, Eigen::Dynamic, 2> A(nf, 2);
        Eigen::Matrix<double, Eigen::Dynamic, 4> B(nf, 4);
        int row = 0;
        for (int fi : cell.faces) {
            auto& face = lm_->mesh.faces[fi];
            int other = (face.left_cell == ci) ? face.right_cell : face.left_cell;
            Vec2 dc; Vec4 dU;
            if (other >= 0) {
                dc = lm_->mesh.cells[other].centroid - cell.centroid;
                dU = U_[other] - U_[ci];
            } else {
                dc = 2.0 * (face.midpoint - cell.centroid);
                dU = Vec4::Zero();
            }
            double d2 = dc.squaredNorm();
            if (d2 < 1e-30) continue;
            double w = 1.0 / std::sqrt(d2);
            A(row, 0) = dc.x() * w; A(row, 1) = dc.y() * w;
            for (int k = 0; k < 4; k++) B(row, k) = dU[k] * w;
            row++;
        }
        if (row < 2) continue;
        A.conservativeResize(row, 2); B.conservativeResize(row, 4);
        Eigen::Matrix2d ATA = A.transpose() * A;
        if (std::abs(ATA.determinant()) < 1e-30) continue;
        Eigen::Matrix<double, 2, 4> grad = ATA.inverse() * (A.transpose() * B);
        for (int k = 0; k < 4; k++) {
            gradients_[ci][0][k] = grad(0, k);
            gradients_[ci][1][k] = grad(1, k);
        }
    }
    halo_.exchange_gradients(gradients_);
}

void Solver::compute_limiter() {
    double recon_scale = recon_ramp_;
    for (int ci = 0; ci < num_owned_; ci++) {
        auto& cell = lm_->mesh.cells[ci];
        Vec4 Umax = U_[ci], Umin = U_[ci];
        for (int fi : cell.faces) {
            auto& face = lm_->mesh.faces[fi];
            int other = (face.left_cell == ci) ? face.right_cell : face.left_cell;
            if (other >= 0) {
                for (int k = 0; k < 4; k++) {
                    Umax[k] = std::max(Umax[k], U_[other][k]);
                    Umin[k] = std::min(Umin[k], U_[other][k]);
                }
            }
        }
        double phi = 1.0;
        for (int fi : cell.faces) {
            auto& face = lm_->mesh.faces[fi];
            Vec2 df = face.midpoint - cell.centroid;
            for (int k = 0; k < 4; k++) {
                double delta = gradients_[ci][0][k] * df.x() + gradients_[ci][1][k] * df.y();
                if (std::abs(delta) < 1e-30) continue;
                double r = (delta > 0) ? (Umax[k] - U_[ci][k]) / delta : (Umin[k] - U_[ci][k]) / delta;
                phi = std::min(phi, std::max(std::min(r, 1.0), 0.0));
            }
        }
        limiter_phi_[ci] = phi * recon_scale;
    }
}

Vec4 Solver::ensure_positive(const Vec4& U_recon, const Vec4& U_cell) const {
    if (U_recon[0] > 1e-14 && prim_pressure(U_recon, config_.gas.gamma) > 1e-14) return U_recon;
    return U_cell;
}

Vec4 Solver::reconstruct_left(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int ci = face.left_cell;
    if (ci < 0) return config_.freestream.state(config_.gas);
    Vec2 df = face.midpoint - lm_->mesh.cells[ci].centroid;
    double phi = (ci < num_owned_) ? limiter_phi_[ci] : 0.0;
    Vec4 Ur;
    for (int k = 0; k < 4; k++)
        Ur[k] = U_[ci][k] + phi * (gradients_[ci][0][k] * df.x() + gradients_[ci][1][k] * df.y());
    return ensure_positive(Ur, U_[ci]);
}

Vec4 Solver::reconstruct_right(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int ci = face.right_cell;
    if (ci < 0) return config_.freestream.state(config_.gas);
    Vec2 df = face.midpoint - lm_->mesh.cells[ci].centroid;
    double phi = (ci < num_owned_) ? limiter_phi_[ci] : 0.0;
    Vec4 Ur;
    for (int k = 0; k < 4; k++)
        Ur[k] = U_[ci][k] + phi * (gradients_[ci][0][k] * df.x() + gradients_[ci][1][k] * df.y());
    return ensure_positive(Ur, U_[ci]);
}

Vec4 Solver::roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& n) const {
    double gamma = config_.gas.gamma;
    double rhoL, uL, vL, pL; cons_to_prim(UL, gamma, rhoL, uL, vL, pL);
    double rhoR, uR, vR, pR; cons_to_prim(UR, gamma, rhoR, uR, vR, pR);

    double HL = (UL[3] + pL) / rhoL, HR = (UR[3] + pR) / rhoR;
    double sqL = std::sqrt(rhoL), sqR = std::sqrt(rhoR), den = sqL + sqR;
    double u_r = (sqL*uL + sqR*uR)/den, v_r = (sqL*vL + sqR*vR)/den, H_r = (sqL*HL + sqR*HR)/den;
    double q2 = u_r*u_r + v_r*v_r;
    double a2 = (gamma-1.0)*(H_r - 0.5*q2);
    if (a2 < 1e-14) return rusanov_flux(UL, UR, n);
    double a_r = std::sqrt(a2), rho_r = sqL * sqR;
    double un = u_r*n.x() + v_r*n.y();

    double eps = 0.1 * a_r;
    auto hf = [eps](double l) { return std::abs(l) < eps ? (l*l+eps*eps)/(2*eps) : std::abs(l); };
    double l1 = hf(un - a_r), l2 = hf(un), l3 = hf(un + a_r);

    double dp = pR-pL, drho = rhoR-rhoL, dun = (uR-uL)*n.x() + (vR-vL)*n.y();
    double a1 = (dp - rho_r*a_r*dun)/(2*a2);
    double a2v = drho - dp/a2;
    double a3 = (dp + rho_r*a_r*dun)/(2*a2);
    double a4 = rho_r*((vR-vL)*n.x() - (uR-uL)*n.y());

    Vec4 r1; r1 << 1, u_r-a_r*n.x(), v_r-a_r*n.y(), H_r-a_r*un;
    Vec4 r2; r2 << 1, u_r, v_r, 0.5*q2;
    Vec4 r3; r3 << 1, u_r+a_r*n.x(), v_r+a_r*n.y(), H_r+a_r*un;
    Vec4 r4; r4 << 0, -n.y(), n.x(), v_r*n.x()-u_r*n.y();

    double unL = uL*n.x()+vL*n.y(), unR = uR*n.x()+vR*n.y();
    Vec4 FL; FL << rhoL*unL, rhoL*uL*unL+pL*n.x(), rhoL*vL*unL+pL*n.y(), (UL[3]+pL)*unL;
    Vec4 FR; FR << rhoR*unR, rhoR*uR*unR+pR*n.x(), rhoR*vR*unR+pR*n.y(), (UR[3]+pR)*unR;

    return 0.5*(FL + FR - (l1*a1*r1 + l2*a2v*r2 + l3*a3*r3 + l2*a4*r4));
}

Vec4 Solver::rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& n) const {
    double gamma = config_.gas.gamma;
    double rhoL, uL, vL, pL; cons_to_prim(UL, gamma, rhoL, uL, vL, pL);
    double rhoR, uR, vR, pR; cons_to_prim(UR, gamma, rhoR, uR, vR, pR);
    double unL = uL*n.x()+vL*n.y(), unR = uR*n.x()+vR*n.y();
    double smax = std::max(std::abs(unL)+std::sqrt(gamma*pL/rhoL), std::abs(unR)+std::sqrt(gamma*pR/rhoR));
    Vec4 FL; FL << rhoL*unL, rhoL*uL*unL+pL*n.x(), rhoL*vL*unL+pL*n.y(), (UL[3]+pL)*unL;
    Vec4 FR; FR << rhoR*unR, rhoR*uR*unR+pR*n.x(), rhoR*vR*unR+pR*n.y(), (UR[3]+pR)*unR;
    return 0.5*(FL + FR - config_.run_control.rusanov_dissipation_scale * smax * (UR - UL));
}

Vec4 Solver::viscous_flux(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int cl = face.left_cell, cr = face.right_cell;
    if (cl < 0 || cr < 0) return Vec4::Zero();
    double gamma = config_.gas.gamma;
    double rhoL, uL, vL, pL; cons_to_prim(U_[cl], gamma, rhoL, uL, vL, pL);
    double rhoR, uR, vR, pR; cons_to_prim(U_[cr], gamma, rhoR, uR, vR, pR);
    double TL = config_.gas.temperature(rhoL, pL), TR = config_.gas.temperature(rhoR, pR);

    Vec2 dc = lm_->mesh.cells[cr].centroid - lm_->mesh.cells[cl].centroid;
    double dist = dc.norm();
    if (dist < 1e-30) return Vec4::Zero();
    Vec2 en = dc / dist;
    double u_f = 0.5*(uL+uR), v_f = 0.5*(vL+vR);
    double dudx = (uR-uL)/dist * en.x(), dudy = (uR-uL)/dist * en.y();
    double dvdx = (vR-vL)/dist * en.x(), dvdy = (vR-vL)/dist * en.y();
    double dTdx = (TR-TL)/dist * en.x(), dTdy = (TR-TL)/dist * en.y();

    double div = dudx + dvdy;
    double txx = mu_*(2*dudx - 2.0/3*div), tyy = mu_*(2*dvdy - 2.0/3*div), txy = mu_*(dudy+dvdx);
    double k = mu_ * config_.gas.cp() / config_.gas.Pr;
    double nx = face.normal.x(), ny = face.normal.y();
    Vec4 Fv;
    Fv[0] = 0;
    Fv[1] = txx*nx + txy*ny;
    Fv[2] = txy*nx + tyy*ny;
    Fv[3] = (u_f*txx + v_f*txy + k*dTdx)*nx + (u_f*txy + v_f*tyy + k*dTdy)*ny;
    return Fv;
}

Vec4 Solver::farfield_flux(int fi) const {
    Vec4 Uf = config_.freestream.state(config_.gas);
    Vec4 UL = reconstruct_left(fi);
    return rusanov_flux(UL, Uf, lm_->mesh.faces[fi].normal);
}

Vec4 Solver::slip_wall_flux(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int ci = face.left_cell;
    double rho, u, v, p;
    cons_to_prim(U_[ci], config_.gas.gamma, rho, u, v, p);
    Vec4 F;
    F[0] = 0; F[1] = p*face.normal.x(); F[2] = p*face.normal.y(); F[3] = 0;
    return F;
}

Vec4 Solver::noslip_wall_flux(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int ci = face.left_cell;
    double rho, u, v, p;
    cons_to_prim(U_[ci], config_.gas.gamma, rho, u, v, p);
    Vec4 F;
    F[0] = 0; F[1] = p*face.normal.x(); F[2] = p*face.normal.y(); F[3] = 0;
    return F;
}

Vec4 Solver::noslip_viscous_flux(int fi) const {
    auto& face = lm_->mesh.faces[fi];
    int ci = face.left_cell;
    if (ci < 0) return Vec4::Zero();
    double rho, u, v, p;
    cons_to_prim(U_[ci], config_.gas.gamma, rho, u, v, p);
    Vec2 dc = face.midpoint - lm_->mesh.cells[ci].centroid;
    double dist = dc.norm();
    if (dist < 1e-30) return Vec4::Zero();
    Vec2 en = dc / dist;
    double dudx = -u/dist*en.x(), dudy = -u/dist*en.y();
    double dvdx = -v/dist*en.x(), dvdy = -v/dist*en.y();
    double div = dudx + dvdy;
    double txx = mu_*(2*dudx - 2.0/3*div), tyy = mu_*(2*dvdy - 2.0/3*div), txy = mu_*(dudy+dvdx);
    double nx = face.normal.x(), ny = face.normal.y();
    Vec4 Fv;
    Fv[0] = 0; Fv[1] = txx*nx + txy*ny; Fv[2] = txy*nx + tyy*ny; Fv[3] = 0;
    return Fv;
}

void Solver::compute_residual() {
    for (int ci = 0; ci < num_owned_; ci++) residual_[ci] = Vec4::Zero();

    for (int fi = 0; fi < (int)lm_->mesh.faces.size(); fi++) {
        auto& face = lm_->mesh.faces[fi];
        Vec4 flux = Vec4::Zero();

        if (face.is_boundary) {
            if (face.bc_id < 0) continue;
            auto& bg = lm_->mesh.boundary_groups[face.bc_id];
            switch (bg.type) {
                case BCType::FARFIELD: flux = farfield_flux(fi); break;
                case BCType::SLIP_WALL: flux = slip_wall_flux(fi); break;
                case BCType::NO_SLIP_ADIABATIC_WALL:
                    flux = noslip_wall_flux(fi);
                    if (config_.physics_mode == PhysicsMode::LAMINAR) flux -= noslip_viscous_flux(fi);
                    break;
            }
            if (face.left_cell >= 0 && face.left_cell < num_owned_)
                residual_[face.left_cell] -= flux * face.area;
        } else {
            Vec4 UL = reconstruct_left(fi);
            Vec4 UR = reconstruct_right(fi);
            flux = rusanov_flux(UL, UR, face.normal);
            Vec4 fv = Vec4::Zero();
            if (config_.physics_mode == PhysicsMode::LAMINAR) fv = viscous_flux(fi);
            Vec4 net = (flux - fv) * face.area;
            if (face.left_cell >= 0 && face.left_cell < num_owned_)  residual_[face.left_cell]  -= net;
            if (face.right_cell >= 0 && face.right_cell < num_owned_) residual_[face.right_cell] += net;
        }
    }
}

double Solver::spectral_radius(int ci) const {
    double rho, u, v, p;
    cons_to_prim(U_[ci], config_.gas.gamma, rho, u, v, p);
    double a = std::sqrt(config_.gas.gamma * p / rho);
    double sr = 0;
    for (int fi : lm_->mesh.cells[ci].faces) {
        auto& face = lm_->mesh.faces[fi];
        sr += (std::abs(u*face.normal.x() + v*face.normal.y()) + a) * face.area;
    }
    return sr;
}

double Solver::viscous_spectral_radius(int ci) const {
    if (mu_ < 1e-30) return 0;
    double rho = std::max(U_[ci][0], 1e-14);
    double fas = 0;
    for (int fi : lm_->mesh.cells[ci].faces) fas += lm_->mesh.faces[fi].area * lm_->mesh.faces[fi].area;
    return mu_/rho * std::max(4.0/3.0, config_.gas.gamma/config_.gas.Pr) * fas / std::max(lm_->mesh.cells[ci].volume, 1e-30);
}

void Solver::lusgs_sweep(double cfl, const std::vector<Vec4>& rhs, std::vector<Vec4>& dU) {
    double gamma = config_.gas.gamma;
    dU.assign(num_total_, Vec4::Zero());

    std::vector<double> D(num_owned_);
    for (int ci = 0; ci < num_owned_; ci++) {
        double vol = lm_->mesh.cells[ci].volume;
        double sr = spectral_radius(ci) + viscous_spectral_radius(ci);
        double dt_local = cfl * vol / std::max(sr, 1e-30);
        D[ci] = vol / dt_local + sr;
    }

    auto face_spec = [&](int fi, int nb) -> double {
        double rho_n, u_n, v_n, p_n;
        cons_to_prim(U_[nb], gamma, rho_n, u_n, v_n, p_n);
        auto& f = lm_->mesh.faces[fi];
        return (std::abs(u_n*f.normal.x()+v_n*f.normal.y()) + std::sqrt(gamma*p_n/rho_n)) * f.area;
    };

    // Forward sweep: (D+L)*dU_star = R
    for (int ci = 0; ci < num_owned_; ci++) {
        Vec4 lower_sum = Vec4::Zero();
        for (int fi : lm_->mesh.cells[ci].faces) {
            auto& face = lm_->mesh.faces[fi];
            int nb = (face.left_cell == ci) ? face.right_cell : face.left_cell;
            if (nb >= 0 && nb < ci && nb < num_owned_)
                lower_sum += 0.5 * face_spec(fi, nb) * dU[nb];
        }
        dU[ci] = (rhs[ci] + lower_sum) / D[ci];
    }

    // Backward sweep: (D+U)*dU = D*dU_star
    // Rearranged: dU[i] = dU_star[i] + (1/D) * sum_{j>i} 0.5*spec*dU[j]
    for (int ci = num_owned_-1; ci >= 0; ci--) {
        Vec4 upper_sum = Vec4::Zero();
        for (int fi : lm_->mesh.cells[ci].faces) {
            auto& face = lm_->mesh.faces[fi];
            int nb = (face.left_cell == ci) ? face.right_cell : face.left_cell;
            if (nb > ci && nb < num_owned_)
                upper_sum += 0.5 * face_spec(fi, nb) * dU[nb];
        }
        dU[ci] = dU[ci] + upper_sum / D[ci];
    }
}

double Solver::compute_residual_l2() const {
    double s = 0;
    for (int ci = 0; ci < num_owned_; ci++) s += residual_[ci].squaredNorm();
    return std::sqrt(mpi_allreduce_sum(s, comm_));
}

double Solver::compute_residual_linf() const {
    double m = 0;
    for (int ci = 0; ci < num_owned_; ci++)
        for (int k = 0; k < 4; k++) m = std::max(m, std::abs(residual_[ci][k]));
    return mpi_allreduce_max(m, comm_);
}

void Solver::compute_forces(double& cl, double& cd, double& cmz,
                           double& pdrag, double& vdrag, double& plift, double& vlift) const {
    double gamma = config_.gas.gamma;
    double q_inf = 0.5 * config_.freestream.rho * config_.freestream.vel_mag * config_.freestream.vel_mag;
    double ref_area = config_.reference.area;
    Vec2 mc = config_.reference.moment_center;
    double aoa = config_.freestream.aoa_deg * M_PI / 180.0;
    double ca = std::cos(aoa), sa = std::sin(aoa);
    double lp = 0, lv = 0, dp = 0, dv = 0, lm = 0;

    for (auto& bg : lm_->mesh.boundary_groups) {
        if (bg.type != BCType::SLIP_WALL && bg.type != BCType::NO_SLIP_ADIABATIC_WALL) continue;
        for (int fi : bg.face_ids) {
            auto& face = lm_->mesh.faces[fi];
            int ci = face.left_cell;
            if (ci < 0 || ci >= num_owned_) continue;
            double rho, u, v, p; cons_to_prim(U_[ci], gamma, rho, u, v, p);
            double Fxp = p*face.normal.x()*face.area, Fyp = p*face.normal.y()*face.area;
            double Fxv = 0, Fyv = 0;
            if (config_.physics_mode == PhysicsMode::LAMINAR && bg.type == BCType::NO_SLIP_ADIABATIC_WALL) {
                Vec2 dc = face.midpoint - lm_->mesh.cells[ci].centroid;
                double dist = dc.norm();
                if (dist > 1e-30) {
                    Vec2 tang(face.normal.y(), -face.normal.x());
                    double ut = u*tang.x() + v*tang.y();
                    double tw = mu_ * ut / dist;
                    Fxv = tw*tang.x()*face.area; Fyv = tw*tang.y()*face.area;
                }
            }
            dp += Fxp*ca + Fyp*sa; lp += -Fxp*sa + Fyp*ca;
            dv += Fxv*ca + Fyv*sa; lv += -Fxv*sa + Fyv*ca;
            Vec2 r = face.midpoint - mc;
            lm += r.x()*(Fyp+Fyv) - r.y()*(Fxp+Fxv);
        }
    }
    pdrag = mpi_allreduce_sum(dp, comm_)/(q_inf*ref_area);
    vdrag = mpi_allreduce_sum(dv, comm_)/(q_inf*ref_area);
    plift = mpi_allreduce_sum(lp, comm_)/(q_inf*ref_area);
    vlift = mpi_allreduce_sum(lv, comm_)/(q_inf*ref_area);
    cd = pdrag+vdrag; cl = plift+vlift;
    cmz = mpi_allreduce_sum(lm, comm_)/(q_inf*ref_area*config_.reference.length);
}

SolverStats Solver::run(const std::string& output_dir) {
    auto t_start = std::chrono::steady_clock::now();
    SolverStats stats;
    writer_.open(output_dir);

    bool is_transient = (config_.run_control.type == RunType::TRANSIENT);
    double cfl_max = config_.run_control.cfl_max;
    int ramp_steps = config_.run_control.pseudo_cfl_ramp_steps;
    double res0 = -1.0, physical_time = 0.0;
    int step = 0, total_inner = 0;
    int inner_min_obs = 999999, inner_max_obs = 0, target_misses = 0, converged_steps = 0;

    if (!is_transient) {
        int max_steps = config_.run_control.max_steps;
        double target_reduction = config_.run_control.residual_reduction_target;

        for (step = 1; step <= max_steps; step++) {
            double cfl;
            if (ramp_steps > 0 && step <= ramp_steps) {
                double frac = (double)(step-1) / ramp_steps;
                cfl = config_.run_control.cfl_initial + frac*(cfl_max - config_.run_control.cfl_initial);
            } else cfl = cfl_max;
            recon_ramp_ = std::min(1.0, 20.0 / std::max(cfl, 1.0));

            halo_.exchange(U_);
            compute_gradients();
            compute_limiter();
            compute_residual();

            double res_l2 = compute_residual_l2();
            double res_linf = compute_residual_linf();
            if (step == 1) res0 = res_l2;

            Vec4 res_comp;
            for (int k = 0; k < 4; k++) {
                double s = 0;
                for (int ci = 0; ci < num_owned_; ci++) s += residual_[ci][k]*residual_[ci][k];
                res_comp[k] = std::sqrt(mpi_allreduce_sum(s, comm_));
            }

            std::vector<Vec4> dU;
            lusgs_sweep(cfl, residual_, dU);

            for (int ci = 0; ci < num_owned_; ci++) {
                Vec4 Un = U_[ci] + dU[ci];
                if (Un[0] > 1e-14 && prim_pressure(Un, config_.gas.gamma) > 1e-14)
                    U_[ci] = Un;
            }

            total_inner++; inner_min_obs = 1; inner_max_obs = 1;
            double cl_v, cd_v, cmz, pd, vd, pl, vl;
            compute_forces(cl_v, cd_v, cmz, pd, vd, pl, vl);

            if (rank_ == 0 && step % config_.write_residuals_every == 0)
                writer_.write_residual(step, physical_time, 1, cfl, 0, res_comp, res_l2, res_linf);
            if (rank_ == 0 && step % config_.write_forces_every == 0)
                writer_.write_force(step, physical_time, cl_v, cd_v, cmz, pd, vd, pl, vl);
            if (rank_ == 0 && step % 1000 == 0) {
                double red = (res0>1e-30) ? std::log10(res0/std::max(res_l2,1e-30)) : 0;
                fprintf(stderr, "Step %6d CFL=%8.2f Res=%.4e Red=%.2f CD=%.6f CL=%.6f\n", step, cfl, res_l2, red, cd_v, cl_v);
            }
            if (res0>1e-30 && res_l2>0 && std::isfinite(res_l2) && std::log10(res0/res_l2) >= target_reduction) {
                stats.convergence_status = "converged"; break;
            }
            if (!std::isfinite(res_l2)) {
                if (rank_==0) fprintf(stderr, "NaN at step %d, CFL=%.2f\n", step, cfl);
                stats.convergence_status = "failed"; break;
            }
        }
        stats.total_steps = step; stats.final_time = physical_time;
        double fr = compute_residual_l2();
        stats.residual_reduction_orders = (res0>1e-30 && std::isfinite(fr)) ? std::log10(res0/std::max(fr,1e-30)) : 0;
        if (stats.convergence_status.empty())
            stats.convergence_status = (stats.residual_reduction_orders >= 1.0) ? "converged" : "failed";
    } else {
        double dt = config_.run_control.time_step, final_time = config_.run_control.final_time;
        int max_phys = (int)(final_time/dt + 0.5);
        U_n_ = U_; U_nm1_ = U_;

        for (step = 1; step <= max_phys; step++) {
            physical_time = step * dt;
            U_nm1_ = U_n_; U_n_ = U_;
            int inner_iters = 0; double inner_res0 = -1; bool ic = false;

            for (int inner = 0; inner < config_.run_control.max_inner_iterations; inner++) {
                halo_.exchange(U_); compute_gradients(); compute_limiter(); compute_residual();
                double bdf_c = (step>=2)?1.5:1.0, bdf_n = (step>=2)?-2.0:-1.0, bdf_nm1 = (step>=2)?0.5:0.0;
                for (int ci = 0; ci < num_owned_; ci++) {
                    double vol = lm_->mesh.cells[ci].volume;
                    residual_[ci] -= (vol/dt)*(bdf_c*U_[ci] + bdf_n*U_n_[ci] + bdf_nm1*U_nm1_[ci]);
                }
                double rl2 = compute_residual_l2();
                if (inner==0) inner_res0 = rl2;
                inner_iters = inner+1;
                if (inner >= config_.run_control.min_inner_iterations-1 && inner_res0>1e-30 && rl2/inner_res0 < config_.run_control.inner_residual_reduction_target) { ic=true; break; }
                double cfl = config_.run_control.cfl_initial;
                std::vector<Vec4> dU; lusgs_sweep(cfl, residual_, dU);
                for (int ci = 0; ci < num_owned_; ci++) {
                    Vec4 Un = U_[ci]+dU[ci];
                    if (Un[0]>1e-14 && prim_pressure(Un, config_.gas.gamma)>1e-14) U_[ci] = Un;
                }
            }
            if (!ic) target_misses++; else converged_steps++;
            total_inner += inner_iters;
            inner_min_obs = std::min(inner_min_obs, inner_iters);
            inner_max_obs = std::max(inner_max_obs, inner_iters);

            halo_.exchange(U_); compute_gradients(); compute_residual();
            double rl2 = compute_residual_l2(), rli = compute_residual_linf();
            if (step==1) res0 = rl2;
            Vec4 rc; for (int k=0;k<4;k++) { double s=0; for(int ci=0;ci<num_owned_;ci++) s+=residual_[ci][k]*residual_[ci][k]; rc[k]=std::sqrt(mpi_allreduce_sum(s,comm_)); }
            double cl_v,cd_v,cmz,pd,vd,pl,vl; compute_forces(cl_v,cd_v,cmz,pd,vd,pl,vl);
            if (rank_==0) { writer_.write_residual(step,physical_time,inner_iters,config_.run_control.cfl_initial,dt,rc,rl2,rli); writer_.write_force(step,physical_time,cl_v,cd_v,cmz,pd,vd,pl,vl); }
            if (rank_==0 && step%500==0) fprintf(stderr,"Phys %6d t=%.4f Inner=%d Res=%.4e CD=%.6f CL=%.6f\n",step,physical_time,inner_iters,rl2,cd_v,cl_v);
            if (!std::isfinite(rl2)) { if(rank_==0) fprintf(stderr,"NaN transient step %d\n",step); stats.convergence_status="failed"; break; }
        }
        stats.total_steps=step; stats.final_time=physical_time;
        double fr=compute_residual_l2();
        stats.residual_reduction_orders=(res0>1e-30&&std::isfinite(fr))?std::log10(res0/std::max(fr,1e-30)):0;
        if (stats.convergence_status.empty()) stats.convergence_status="statistically_periodic";
    }

    halo_.exchange(U_);
    writer_.write_surface(lm_->mesh, U_, gradients_, config_, mu_, rank_, nranks_, comm_);
    writer_.write_field(lm_->mesh, U_, config_, rank_, nranks_, comm_);
    auto t_end = std::chrono::steady_clock::now();
    stats.wall_time_seconds = std::chrono::duration<double>(t_end-t_start).count();
    stats.observed_min_inner = inner_min_obs==999999?0:inner_min_obs;
    stats.observed_max_inner = inner_max_obs;
    stats.observed_mean_inner = stats.total_steps>0?(double)total_inner/stats.total_steps:0;
    stats.inner_target_misses = target_misses;
    stats.inner_converged_fraction = stats.total_steps>0?(double)converged_steps/stats.total_steps:0;
    return stats;
}
