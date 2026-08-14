#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace cfd {

using Mat4 = Eigen::Matrix<real_t, 4, 4>;

// Exact 4x4 Jacobian of the inviscid flux F(U)·n with respect to the
// conservative state U = [rho, rhou, rhov, rhoE].  This is the same
// analytic matrix used by the block-Jacobi reference solver; the scalar
// LU-SGS spectral-radius diagonal decouples the equations and cannot
// represent the density/momentum/energy coupling that keeps the update
// well-posed at the degenerate LE/TE sliver cells (V ~ 1e-9).
static Mat4 euler_flux_jacobian(const StateVec& U, real_t nx, real_t ny,
                                real_t gamma, real_t Rgas) {
    Prims q = prims_from_conservative(U, gamma, Rgas);
    real_t u = q.u, v = q.v;
    real_t V2 = u * u + v * v;
    real_t gm1 = gamma - 1.0;
    real_t H = q.e + q.p / q.rho + 0.5 * V2;  // total enthalpy
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

// Jacobian of the impermeable-wall pressure flux (0, p nx, p ny, 0) with
// respect to the interior conservative state.
static Mat4 wall_flux_jacobian(const StateVec& U, real_t nx, real_t ny,
                               real_t gamma, real_t Rgas) {
    Prims q = prims_from_conservative(U, gamma, Rgas);
    real_t V2 = q.u * q.u + q.v * q.v;
    real_t gm1 = gamma - 1.0;
    StateVec dpdU;
    dpdU << 0.5 * gm1 * V2, -gm1 * q.u, -gm1 * q.v, gm1;
    Mat4 J = Mat4::Zero();
    J.row(1) = nx * dpdU.transpose();
    J.row(2) = ny * dpdU.transpose();
    return J;
}

// Exact 4x4 Jacobian of the no-slip adiabatic wall viscous traction
// contribution to the boundary flux, F_b = (0, -txx_n S, -tyy_n S, 0), with
// respect to the interior conservative state.  The traction is built from the
// mirrored-ghost wall stress (see wall_viscous_stress) at the SAME effective
// wall distance d_eff used by compute_residual, so the implicit operator is
// exactly consistent with the explicit residual at the wall (the old scalar
// viscous_diag * I approximation both over-stiffened the wall diagonal by
// using max(|dx.n|, 0.1|dr|) instead of max(d, 0.5*sqrt(V)) and put spurious
// mass/energy diagonals in the momentum rows' place).
static Mat4 wall_traction_jacobian(const StateVec& U, real_t nx, real_t ny,
                                   real_t S, real_t d_eff, real_t mu,
                                   real_t gamma, real_t Rgas) {
    Prims q = prims_from_conservative(U, gamma, Rgas);
    real_t u = q.u, v = q.v;
    real_t rho = std::max(q.rho, 1e-30);
    real_t c = -mu * S / std::max(d_eff, 1e-14);
    // txx_n = tau_xx nx + tau_xy ny
    //       = (mu/d)[ u(-(4/3)nx^2 - ny^2) - (1/3) v nx ny ]
    // tyy_n = tau_xy nx + tau_yy ny
    //       = (mu/d)[ -(1/3) u nx ny + v(-nx^2 - (4/3)ny^2) ]
    real_t a11 = -((4.0 / 3.0) * nx * nx + ny * ny);
    real_t a12 = -(1.0 / 3.0) * nx * ny;
    real_t a22 = -(nx * nx + (4.0 / 3.0) * ny * ny);
    Mat4 J = Mat4::Zero();
    J(1, 0) = c * (a11 * (-u / rho) + a12 * (-v / rho));
    J(1, 1) = c * a11 / rho;
    J(1, 2) = c * a12 / rho;
    J(2, 0) = c * (a12 * (-u / rho) + a22 * (-v / rho));
    J(2, 1) = c * a12 / rho;
    J(2, 2) = c * a22 / rho;
    return J;
}

// Dense 4x4 Gaussian elimination with partial pivoting.
static StateVec solve_block4(Mat4 A, StateVec b) {
    for (int pivot = 0; pivot < 4; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 4; ++row) {
            if (std::abs(A(row, pivot)) > std::abs(A(best, pivot))) best = row;
        }
        if (best != pivot) {
            A.row(pivot).swap(A.row(best));
            std::swap(b[pivot], b[best]);
        }
        const real_t diagonal = A(pivot, pivot);
        if (!std::isfinite(diagonal) || std::abs(diagonal) < 1.0e-30) {
            throw std::runtime_error("singular/non-finite implicit 4x4 block");
        }
        for (int row = pivot + 1; row < 4; ++row) {
            const real_t multiplier = A(row, pivot) / diagonal;
            A(row, pivot) = 0.0;
            for (int col = pivot + 1; col < 4; ++col) {
                A(row, col) -= multiplier * A(pivot, col);
            }
            b[row] -= multiplier * b[pivot];
        }
    }
    StateVec x;
    for (int rev = 3; rev >= 0; --rev) {
        real_t value = b[rev];
        for (int col = rev + 1; col < 4; ++col) {
            value -= A(rev, col) * x[col];
        }
        x[rev] = value / A(rev, rev);
    }
    return x;
}

// Viscous stress tensor at a no-slip adiabatic wall face, built from the
// mirrored-ghost construction instead of the cell-centered Green-Gauss
// gradient.  The ghost cell (mirrored across the wall at distance d from the
// face) has u_g = -u_cell, v_g = -v_cell, T_g = T_cell, so the wall-normal
// velocity derivative is du/dn = -u_cell/d (the face normal points from the
// fluid cell into the body) and grad u = (du/dn) n^T.  This is O(1/d) and
// stays well-conditioned at the degenerate leading/trailing-edge cells
// (V ~ 1e-9), where the Green-Gauss gradient is O(1/V) and made the wall
// stress (and therefore the residual) enormous.
static void wall_viscous_stress(real_t u, real_t v, real_t nx, real_t ny,
                                real_t d, real_t mu,
                                real_t& tau_xx, real_t& tau_yy,
                                real_t& tau_xy) {
    real_t ux = -u * nx / d;
    real_t uy = -u * ny / d;
    real_t vx = -v * nx / d;
    real_t vy = -v * ny / d;
    real_t div = ux + vy;
    tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
    tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
    tau_xy = mu * (uy + vx);
}

Solver::Solver(const CaseInput& ci, const LocalMesh& lm, const Mesh& gm,
               const PartitionInfo& part, MPI_Comm comm, int rank, int n_ranks)
    : case_input_(ci), local_mesh_(lm), global_mesh_(gm), part_(part),
      comm_(comm), rank_(rank), n_ranks_(n_ranks) {

    const auto& faces = local_mesh_.faces;
    bc_of_face_.assign(faces.size(), 0);
    wall_bc_.assign(faces.size(), BCType::Interior);
    face_length_.assign(faces.size(), 0.0);
    face_tag_name_.assign(faces.size(), "");
    cell_dt.assign(local_mesh_.n_total, 1.0);

    // Map family tag id -> BC type from the case file.
    std::map<idx_t, BCType> tag_bc;
    for (const auto& [fam, type] : case_input_.boundary_conditions) {
        auto it = global_mesh_.bc_tag_map.find(fam);
        if (it == global_mesh_.bc_tag_map.end()) {
            throw std::runtime_error("case boundary family '" + fam +
                                     "' not found in mesh boundary families");
        }
        tag_bc[it->second] = bc_from_string(type);
    }

    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        face_length_[f] = std::sqrt(face.normal[0] * face.normal[0] +
                                    face.normal[1] * face.normal[1]);
        if (face.bc_tag != 0) {
            bc_of_face_[f] = face.bc_tag;
            auto it = tag_bc.find(face.bc_tag);
            wall_bc_[f] = (it != tag_bc.end()) ? it->second : BCType::Farfield;
            face_tag_name_[f] = family_tag_name(face.bc_tag);
        }
    }

    // Initialize state: freestream everywhere.
    U_.assign(local_mesh_.n_total, case_input_.freestream_state);

    // Local geometry arrays covering owned + ghost cells.
    local_centroids_.resize(local_mesh_.n_total);
    local_volumes_.resize(local_mesh_.n_total, 1.0);
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        local_centroids_[i] = local_mesh_.owned_cells[i].centroid;
        local_volumes_[i] = local_mesh_.owned_cells[i].volume;
    }
    for (idx_t i = 0; i < local_mesh_.n_ghost; i++) {
        idx_t gi = local_mesh_.n_owned + i;
        local_centroids_[gi] = local_mesh_.ghost_cells[i].centroid;
        local_volumes_[gi] = local_mesh_.ghost_cells[i].volume;
    }
}

// Forward declaration needed by newton_gmres_correction.
static void apply_limited_correction(std::vector<StateVec>& U,
                                     const std::vector<StateVec>& dU,
                                     const LocalMesh& mesh,
                                     real_t gamma, real_t Rgas,
                                     real_t limiter_frac);

real_t Solver::newton_gmres_correction(const std::vector<StateVec>& R,
                                       const real_t* dt_local,
                                       real_t dt_phys) {
    const idx_t n_owned = local_mesh_.n_owned;
    const idx_t n_total = local_mesh_.n_total;
    const auto& faces = local_mesh_.faces;
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;

    // RHS: b = R*V (volume-weighted residual; the Newton system is
    // (block_diag + off-diagonal) x = b, i.e. J x = R*V, the sign
    // convention of block_jacobi_correction)
    std::vector<StateVec> b(n_owned);
    real_t pn_l2 = 0.0;
    for (idx_t i = 0; i < n_owned; i++) {
        b[i] = R[i] * local_volumes_[i];
        pn_l2 += b[i].squaredNorm();
    }
    MPI_Allreduce(MPI_IN_PLACE, &pn_l2, 1, MPI_DOUBLE, MPI_SUM, comm_);
    const real_t pn = std::sqrt(pn_l2);
    if (pn < 1e-300) return pn;

    // Assemble the block Jacobian
    std::vector<FaceImplicit> fimp(faces.size());
    std::vector<Mat4> block_diag(n_owned, Mat4::Zero());
    assemble_inner_jacobian(block_diag, fimp, dt_local, dt_phys);

    // --- Right-preconditioned GMRES for A x = b ---
    // A v = block_diag[i]·v_i + Σ_f fimp·v_nb
    // M = block_diag (block-Jacobi preconditioner)
    // Right preconditioning: solve A M^{-1} z = b, x = M^{-1} z
    const int m = 30;  // restart length
    int max_iter = 200;
    real_t tol_rel = 1e-10;
    if (const char* e = getenv("CFD_GMRES_MAX_ITER")) max_iter = std::atoi(e);
    if (const char* e = getenv("CFD_GMRES_TOL")) tol_rel = std::atof(e);

    // Helper: dot product over owned cells with MPI all-reduce
    auto global_dot = [&](const std::vector<StateVec>& a,
                           const std::vector<StateVec>& bb) -> real_t {
        real_t s = 0.0;
        for (idx_t i = 0; i < n_owned; i++) s += a[i].dot(bb[i]);
        MPI_Allreduce(MPI_IN_PLACE, &s, 1, MPI_DOUBLE, MPI_SUM, comm_);
        return s;
    };

    // Helper: apply A to a vector (with ghost exchange)
    auto apply_A = [&](const std::vector<StateVec>& v_in,
                        std::vector<StateVec>& out) {
        std::vector<StateVec> v_full(n_total, StateVec::Zero());
        for (idx_t i = 0; i < n_owned; i++) v_full[i] = v_in[i];
        exchange_correction(v_full);
        for (idx_t i = 0; i < n_owned; i++) {
            out[i] = block_diag[i] * v_full[i];
        }
        for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
            idx_t L = faces[f].left_cell;
            if (L < 0 || L >= n_owned) continue;
            idx_t Rc = faces[f].right_cell;
            if (Rc < 0 || !fimp[f].used) continue;
            out[L] += fimp[f].off_owner * v_full[Rc];
            if (Rc < n_owned) out[Rc] += fimp[f].off_neighbor * v_full[L];
        }
    };

    // Preconditioned initial residual: z = M^{-1} b
    std::vector<StateVec> z(n_owned);
    for (idx_t i = 0; i < n_owned; i++) z[i] = solve_block4(block_diag[i], b[i]);
    real_t beta = std::sqrt(std::max(global_dot(z, z), 1e-300));
    const real_t beta0 = beta;

    // Solution accumulated across restarts
    std::vector<StateVec> x(n_owned, StateVec::Zero());
    if (beta < 1e-30) return pn;

    // Arnoldi basis V[0..m] (each vector is n_owned StateVecs)
    std::vector<std::vector<StateVec>> V(m + 1, std::vector<StateVec>(n_owned));
    // Hessenberg matrix (m+1) x m, flat column-major H[i + j*(m+1)]
    std::vector<real_t> H((m + 1) * m, 0.0);
    // Givens rotation cos/sin per column
    std::vector<real_t> cs(m, 0.0), sn(m, 0.0);
    // LSQ right-hand side g (β e_1)
    std::vector<real_t> g(m + 1, 0.0);
    std::vector<StateVec> w(n_owned), tmp(n_owned);

    int total_steps = 0;
    real_t resid = beta;

    // Outer restart loop
    while (total_steps < max_iter && resid > tol_rel * beta) {
        // v_1 = z / β (z is the preconditioned residual of the current x)
        for (idx_t i = 0; i < n_owned; i++) V[0][i] = z[i] / beta;
        g[0] = beta;
        for (int j = 0; j <= m; j++) if (j > 0) g[j] = 0.0;

        int k = 0;
        for (k = 0; k < m && total_steps + k < max_iter; k++) {
            // w = A M^{-1} v_k
            for (idx_t i = 0; i < n_owned; i++) tmp[i] = solve_block4(block_diag[i], V[k][i]);
            apply_A(tmp, w);
            // Modified Gram-Schmidt
            for (int j = 0; j <= k; j++) {
                real_t h = global_dot(w, V[j]);
                H[j + k * (m + 1)] = h;
                for (idx_t i = 0; i < n_owned; i++) w[i] -= h * V[j][i];
            }
            real_t hnn = std::sqrt(std::max(global_dot(w, w), 1e-300));
            H[(k + 1) + k * (m + 1)] = hnn;
            if (hnn > 1e-30) {
                for (idx_t i = 0; i < n_owned; i++) V[k + 1][i] = w[i] / hnn;
            }

            // Apply previous Givens rotations to the new column
            for (int j = 0; j < k; j++) {
                real_t c = cs[j], s = sn[j];
                real_t hj = H[j + k * (m + 1)];
                real_t hj1 = H[(j + 1) + k * (m + 1)];
                H[j + k * (m + 1)] = c * hj + s * hj1;
                H[(j + 1) + k * (m + 1)] = -s * hj + c * hj1;
            }
            // New rotation to zero the subdiagonal
            real_t f = H[k + k * (m + 1)];
            real_t gg = H[(k + 1) + k * (m + 1)];
            real_t r = std::sqrt(f * f + gg * gg);
            cs[k] = (r > 1e-300) ? f / r : 1.0;
            sn[k] = (r > 1e-300) ? gg / r : 0.0;
            H[k + k * (m + 1)] = r;
            H[(k + 1) + k * (m + 1)] = 0.0;
            // Apply rotation to the RHS
            real_t gk = g[k], gk1 = g[k + 1];
            g[k] = cs[k] * gk + sn[k] * gk1;
            g[k + 1] = -sn[k] * gk + cs[k] * gk1;

            resid = std::abs(g[k + 1]);
            if (resid <= tol_rel * beta || hnn <= 1e-30) {
                k++;
                break;
            }
        }
        total_steps += k;

        // Solve the (k x k) upper-triangular LSQ system for y
        std::vector<real_t> y(k, 0.0);
        for (int i = k - 1; i >= 0; i--) {
            real_t acc = 0.0;
            for (int j = i + 1; j < k; j++) acc += H[i + j * (m + 1)] * y[j];
            real_t diag = H[i + i * (m + 1)];
            y[i] = (std::abs(diag) > 1e-300) ? (g[i] - acc) / diag : 0.0;
        }
        // Accumulate x += M^{-1} (V_k y)
        for (idx_t i = 0; i < n_owned; i++) tmp[i] = StateVec::Zero();
        for (int j = 0; j < k; j++) {
            for (idx_t i = 0; i < n_owned; i++) tmp[i] += y[j] * V[j][i];
        }
        for (idx_t i = 0; i < n_owned; i++) {
            x[i] += solve_block4(block_diag[i], tmp[i]);
        }

        // Preconditioned residual of the updated x: z = M^{-1}(b - A x)
        apply_A(x, w);
        for (idx_t i = 0; i < n_owned; i++) z[i] = b[i] - w[i];
        for (idx_t i = 0; i < n_owned; i++) z[i] = solve_block4(block_diag[i], z[i]);
        beta = std::sqrt(std::max(global_dot(z, z), 1e-300));
        resid = beta;
    }

    // Apply the Newton correction.  The block-Jacobi/LU-SGS sweeps need the
    // aggressive 0.25 state-scale cap because their step direction is only a
    // rough quasi-Newton direction; the Krylov Newton step is a much better
    // direction, so the cap would shrink it to ~0 and stall the solve.  Only
    // positivity backtracking protects the state here.
    apply_limited_correction(U_, x, local_mesh_, gamma, Rgas,
                             getenv("CFD_GMRES_LIMIT")
                                 ? std::atof(getenv("CFD_GMRES_LIMIT")) : 10.0);

    if (getenv("CFD_DEBUG_GMRES") && rank_ == 0) {
        // True preconditioned residual of the final x (before limiter)
        apply_A(x, w);
        for (idx_t i = 0; i < n_owned; i++) z[i] = b[i] - w[i];
        for (idx_t i = 0; i < n_owned; i++) z[i] = solve_block4(block_diag[i], z[i]);
        const real_t true_resid = std::sqrt(std::max(global_dot(z, z), 1e-300));
        std::cerr << "GMRES iters=" << total_steps
                  << " lsq_resid=" << resid
                  << " true_resid=" << true_resid
                  << " beta0=" << beta0
                  << " ||x||=" << std::sqrt(global_dot(x, x))
                  << " pn=" << pn << "\n";
    }
    return pn;
}
void Solver::assemble_inner_jacobian(std::vector<Mat4>& block_diag,
                                     std::vector<FaceImplicit>& fimp,
                                     const real_t* dt_local,
                                     real_t dt_phys) const {
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;
    const real_t diss = case_input_.rusanov_dissipation_scale;
    const real_t mu = case_input_.mu_ref;
    const real_t prandtl = case_input_.prandtl;
    const bool viscous = case_input_.physics_mode == "laminar";
    const auto& faces = local_mesh_.faces;
    const idx_t n_owned = local_mesh_.n_owned;
    const idx_t n_total = local_mesh_.n_total;
    const real_t visc_coeff = (4.0 / 3.0 + gamma / prandtl);

    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        idx_t L = face.left_cell;
        if (L < 0 || L >= n_owned) continue;
        idx_t Rc = face.right_cell;
        real_t nx = face.normal[0], ny = face.normal[1];
        real_t S = std::sqrt(nx * nx + ny * ny);
        if (S <= 0) continue;
        nx /= S; ny /= S;
        auto qL = prims_from_conservative(U_[L], gamma, Rgas);
        if (Rc >= 0 && Rc < n_total) {
            auto qR = prims_from_conservative(U_[Rc], gamma, Rgas);
            real_t unL = qL.u * nx + qL.v * ny;
            real_t unR = qR.u * nx + qR.v * ny;
            real_t kappa = diss * std::max(std::abs(unL) + qL.a,
                                           std::abs(unR) + qR.a);
            Mat4 A_L = euler_flux_jacobian(U_[L], nx, ny, gamma, Rgas);
            Mat4 A_R = euler_flux_jacobian(U_[Rc], nx, ny, gamma, Rgas);
            real_t viscous_diag = 0.0;
            if (viscous && mu > 0) {
                real_t dx = local_centroids_[Rc][0] - local_centroids_[L][0];
                real_t dy = local_centroids_[Rc][1] - local_centroids_[L][1];
                real_t dist = std::sqrt(dx * dx + dy * dy);
                real_t d = std::max(std::fabs(dx * nx + dy * ny), 0.1 * dist);
                real_t rho_face = 0.5 * (qL.rho + qR.rho);
                viscous_diag = (mu / std::max(rho_face, 1e-30)) * visc_coeff *
                               S / std::max(d, 1e-14);
            }
            fimp[f].off_owner = (0.5 * A_R - 0.5 * kappa * Mat4::Identity()) * S;
            fimp[f].off_neighbor = (-0.5 * A_L - 0.5 * kappa * Mat4::Identity()) * S;
            if (viscous_diag > 0) {
                fimp[f].off_owner -= viscous_diag * Mat4::Identity();
                fimp[f].off_neighbor -= viscous_diag * Mat4::Identity();
            }
            fimp[f].used = true;
            block_diag[L] += (0.5 * A_L + 0.5 * kappa * Mat4::Identity()) * S;
            if (viscous_diag > 0) block_diag[L] += viscous_diag * Mat4::Identity();
            if (Rc < n_owned) {
                block_diag[Rc] += (-0.5 * A_R + 0.5 * kappa * Mat4::Identity()) * S;
                if (viscous_diag > 0) block_diag[Rc] += viscous_diag * Mat4::Identity();
            }
        } else {
            BCType btype = bc_of_face_[f] ? wall_bc_[f] : BCType::Farfield;
            real_t unL = qL.u * nx + qL.v * ny;
            real_t kappa = diss * (std::abs(unL) + qL.a);
            real_t viscous_diag = 0.0;
            if (viscous && mu > 0) {
                real_t dx = face.centroid[0] - local_centroids_[L][0];
                real_t dy = face.centroid[1] - local_centroids_[L][1];
                real_t dist = std::sqrt(dx * dx + dy * dy);
                real_t d = std::max(std::fabs(dx * nx + dy * ny), 0.1 * dist);
                viscous_diag = (mu / std::max(qL.rho, 1e-30)) * visc_coeff *
                               S / std::max(d, 1e-14);
            }
            if (btype == BCType::Farfield) {
                block_diag[L] += (kappa * S + viscous_diag) * Mat4::Identity();
            } else {
                block_diag[L] += wall_flux_jacobian(U_[L], nx, ny, gamma, Rgas) * S;
                if (viscous && btype == BCType::NoSlipAdiabaticWall) {
                    real_t dx = face.centroid[0] - local_centroids_[L][0];
                    real_t dy = face.centroid[1] - local_centroids_[L][1];
                    real_t d = std::fabs(dx * nx + dy * ny);
                    real_t d_eff = std::max(
                        d, 0.5 * std::sqrt(std::max(local_volumes_[L], 1e-30)));
                    if (d_eff > 0) {
                        block_diag[L] += wall_traction_jacobian(
                            U_[L], nx, ny, S, d_eff, mu, gamma, Rgas);
                    }
                } else if (viscous_diag > 0) {
                    block_diag[L] += viscous_diag * Mat4::Identity();
                }
            }
        }
    }

    // Pseudo-time diagonal (the mass term of the dual-time system).
    for (idx_t i = 0; i < n_owned; i++) {
        block_diag[i] += (local_volumes_[i] / std::max(dt_local[i], 1e-30)) * Mat4::Identity();
    }
    if (dt_phys > 0) {
        // BDF2 mass term dR_time/dU = 3V/(2 dt_phys) on the diagonal.
        const real_t bdf = 1.5 / dt_phys;
        for (idx_t i = 0; i < n_owned; i++) {
            block_diag[i] += (bdf * local_volumes_[i]) * Mat4::Identity();
        }
    }
}

// Apply the per-cell state-scale limiter and positivity backtracking to a
// correction vector dU (owned cells), updating U_ in place.  Identical logic
// to the limiter in block_jacobi_correction and lusgs_sweep.
static void apply_limited_correction(std::vector<StateVec>& U,
                                     const std::vector<StateVec>& dU,
                                     const LocalMesh& mesh,
                                     real_t gamma, real_t Rgas,
                                     real_t limiter_frac) {
    const idx_t n_owned = mesh.n_owned;
    real_t alpha_min = 1.0;
    idx_t n_limited = 0, n_backtrack = 0;
    for (idx_t i = 0; i < n_owned; i++) {
        const StateVec& Ui = U[i];
        real_t rho_i = std::max(Ui[0], 1e-30);
        Prims qi = prims_from_conservative(Ui, gamma, Rgas);
        real_t scale[4] = {rho_i,
                           rho_i * (std::fabs(qi.u) + qi.a),
                           rho_i * (std::fabs(qi.v) + qi.a),
                           std::max(std::fabs(Ui[3]), qi.p / (gamma - 1.0))};
        real_t ratio = 0.0;
        for (int c = 0; c < 4; c++) {
            ratio = std::max(ratio, std::fabs(dU[i][c]) / std::max(scale[c], 1e-30));
        }
        real_t alpha = (ratio > limiter_frac) ? limiter_frac / ratio : 1.0;
        for (int bt = 0; bt < 45; bt++) {
            StateVec trial = Ui + alpha * dU[i];
            if (trial[0] > 0) {
                real_t ke = 0.5 * (trial[1] * trial[1] + trial[2] * trial[2]) / trial[0];
                if ((gamma - 1.0) * (trial[3] - ke) > 0) break;
            }
            alpha *= 0.5;
            n_backtrack++;
        }
        if (alpha < 1.0) n_limited++;
        alpha_min = std::min(alpha_min, alpha);
        if (alpha > 0) U[i] += alpha * dU[i];
    }
    if (getenv("CFD_DEBUG_LIMIT") && mesh.n_owned > 0) {
        std::cerr << "LIMIT alpha_min=" << alpha_min
                  << " n_limited=" << n_limited << "/" << n_owned
                  << " n_backtrack=" << n_backtrack << "\n";
    }
}
std::string Solver::family_tag_name(idx_t tag) const {
    for (const auto& [fam, tid] : global_mesh_.bc_tag_map) {
        if (tid == tag) return fam;
    }
    return "wall";
}

void Solver::exchange_state() {
    const int n_nbr = (int)local_mesh_.neighbors.size();
    if (n_nbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<real_t>> sendbufs(n_nbr);
    std::vector<std::vector<real_t>> recvbufs(n_nbr);

    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        sendbufs[i].resize(nb.send_cells.size() * 4);
        for (size_t k = 0; k < nb.send_cells.size(); k++) {
            idx_t lc = nb.send_cells[k];
            for (int c = 0; c < 4; c++) sendbufs[i][k * 4 + c] = U_[lc][c];
        }
        reqs.push_back(MPI_Request());
        MPI_Isend(sendbufs[i].data(), (int)sendbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 77, comm_, &reqs.back());
    }
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        recvbufs[i].resize(nb.recv_cells.size() * 4);
        reqs.push_back(MPI_Request());
        MPI_Irecv(recvbufs[i].data(), (int)recvbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 77, comm_, &reqs.back());
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        for (size_t k = 0; k < nb.recv_cells.size(); k++) {
            idx_t lc = nb.recv_cells[k];
            for (int c = 0; c < 4; c++) U_[lc][c] = recvbufs[i][k * 4 + c];
        }
    }
}

void Solver::exchange_correction(std::vector<StateVec>& corr) {
    const int n_nbr = (int)local_mesh_.neighbors.size();
    if (n_nbr == 0) return;
    std::vector<MPI_Request> reqs;
    std::vector<std::vector<real_t>> sendbufs(n_nbr);
    std::vector<std::vector<real_t>> recvbufs(n_nbr);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        sendbufs[i].resize(nb.send_cells.size() * 4);
        for (size_t k = 0; k < nb.send_cells.size(); k++) {
            idx_t lc = nb.send_cells[k];
            for (int c = 0; c < 4; c++) sendbufs[i][k * 4 + c] = corr[lc][c];
        }
        reqs.push_back(MPI_Request());
        MPI_Isend(sendbufs[i].data(), (int)sendbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 78, comm_, &reqs.back());
    }
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        recvbufs[i].resize(nb.recv_cells.size() * 4);
        reqs.push_back(MPI_Request());
        MPI_Irecv(recvbufs[i].data(), (int)recvbufs[i].size(), MPI_DOUBLE,
                  (int)nb.rank, 78, comm_, &reqs.back());
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int i = 0; i < n_nbr; i++) {
        const auto& nb = local_mesh_.neighbors[i];
        for (size_t k = 0; k < nb.recv_cells.size(); k++) {
            idx_t lc = nb.recv_cells[k];
            for (int c = 0; c < 4; c++) corr[lc][c] = recvbufs[i][k * 4 + c];
        }
    }
}

void Solver::compute_local_dt(real_t* dt_local, real_t cfl) {
    const auto& faces = local_mesh_.faces;
    const real_t gamma = case_input_.gamma;
    const real_t R = case_input_.R;
    const real_t mu = case_input_.mu_ref;
    const real_t prandtl = case_input_.prandtl;
    const bool viscous = case_input_.physics_mode == "laminar";

    std::vector<real_t> spec(local_mesh_.n_total, 0.0);

    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        idx_t L = face.left_cell;
        if (L < 0 || L >= local_mesh_.n_total) continue;
        auto qL = prims_from_conservative(U_[L], gamma, R);
        real_t len = face_length_[f];
        real_t un = qL.u * face.normal[0] + qL.v * face.normal[1];
        real_t lam = (std::abs(un) + qL.a) * len;
        spec[L] += lam;
        idx_t Rc = face.right_cell;
        if (Rc >= 0 && Rc < local_mesh_.n_total) {
            auto qR = prims_from_conservative(U_[Rc], gamma, R);
            real_t unR = qR.u * face.normal[0] + qR.v * face.normal[1];
            spec[Rc] += (std::abs(unR) + qR.a) * len;
            if (viscous && mu > 0) {
                real_t cv = R / (gamma - 1.0);
                real_t kk = mu * gamma * R / ((gamma - 1.0) * prandtl);
                real_t dvL = std::max(mu / qL.rho, kk / (qL.rho * cv));
                spec[L] += dvL * len * len / std::max(local_volumes_[L], 1e-30);
                real_t dvR = std::max(mu / qR.rho, kk / (qR.rho * cv));
                spec[Rc] += dvR * len * len / std::max(local_volumes_[Rc], 1e-30);
            }
        } else if (viscous && mu > 0) {
            real_t cv = R / (gamma - 1.0);
            real_t kk = mu * gamma * R / ((gamma - 1.0) * prandtl);
            real_t dv = std::max(mu / qL.rho, kk / (qL.rho * cv));
            spec[L] += dv * len * len / std::max(local_volumes_[L], 1e-30);
        }
    }

    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        real_t vol = local_mesh_.owned_cells[i].volume;
        dt_local[i] = cfl * vol / std::max(spec[i], 1e-30);
    }
}

void Solver::compute_residual(std::vector<StateVec>& R, real_t& res_l2, real_t& res_linf,
                              bool is_transient, real_t dt,
                              const std::vector<StateVec>& U_n,
                              const std::vector<StateVec>& U_nm1) {
    ResNorm norm;
    compute_residual(R, norm, is_transient, dt, U_n, U_nm1);
    res_l2 = norm.l2;
    res_linf = norm.linf;
}

void Solver::compute_residual(std::vector<StateVec>& R, ResNorm& norm,
                              bool is_transient, real_t dt,
                              const std::vector<StateVec>& U_n,
                              const std::vector<StateVec>& U_nm1) {
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;
    const real_t prandtl = case_input_.prandtl;
    const real_t mu = case_input_.mu_ref;
    const bool viscous = case_input_.physics_mode == "laminar";
    const real_t diss = case_input_.rusanov_dissipation_scale;
    const auto& faces = local_mesh_.faces;

    std::vector<StateVec> spatial(local_mesh_.n_total, StateVec::Zero());

    GradWorkspace ws;
    compute_gradients(*this, U_, ws);
    apply_limiter(*this, U_, ws);

    idx_t dbg_cell = -1;
    if (const char* e = getenv("CFD_DEBUG_CELL")) dbg_cell = (idx_t)std::atoll(e);

    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        idx_t L = face.left_cell, Rc = face.right_cell;
        real_t nx = face.normal[0], ny = face.normal[1];
        real_t S = std::sqrt(nx * nx + ny * ny);
        if (S <= 0) continue;
        nx /= S; ny /= S;
        const bool dbg_this_face = (dbg_cell >= 0) && (L == dbg_cell || Rc == dbg_cell);

        if (Rc >= 0) {
            // interior face: second-order reconstruction everywhere
            Prims qL, qR;
            reconstruct_face(*this, U_, ws, f, qL, qR);

            // conservative states at the face for the numerical flux
            StateVec UL = conservative_from_prims(qL, gamma);
            StateVec UR = conservative_from_prims(qR, gamma);
            StateVec flux = rusanov_flux(UL, UR, nx, ny, S, gamma, Rgas, diss);
            spatial[L] += flux;
            spatial[Rc] -= flux;
            if (dbg_this_face && rank_ == 0) {
                std::cerr << "CFACE f=" << f << " L=" << L << " R=" << Rc
                          << " S=" << S << " flux=(" << flux[0] << "," << flux[1]
                          << "," << flux[2] << "," << flux[3] << ") qL.E="
                          << (qL.p/(gamma-1.0) + 0.5*qL.rho*(qL.u*qL.u+qL.v*qL.v))
                          << " qR.E="
                          << (qR.p/(gamma-1.0) + 0.5*qR.rho*(qR.u*qR.u+qR.v*qR.v))
                          << "\n";
                if (L == dbg_cell || Rc == dbg_cell) {
                    std::cerr << "CFACE2 f=" << f
                              << " L=" << L << " R=" << Rc
                              << " n=(" << nx << "," << ny << ")"
                              << " qL=(rho=" << qL.rho << ",u=" << qL.u
                              << ",v=" << qL.v << ",p=" << qL.p << ")"
                              << " qR=(rho=" << qR.rho << ",u=" << qR.u
                              << ",v=" << qR.v << ",p=" << qR.p << ")"
                              << " limL=" << ws.limiter[L] << " limR=" << ws.limiter[Rc]
                              << "\n";
                }
            }

            if (viscous) {
                // Corrected central face gradient.  The raw face average of
                // the Green-Gauss cell gradients contains a spurious
                // O(1/V_cell) component at wall-adjacent faces: the no-slip
                // wall face contributes u=0 to the cell's Green-Gauss sum,
                // so a wall cell's gradient is nonzero even for a uniform
                // state (grad u = -n_wall*S_wall/V).  Averaging that into
                // the viscous flux makes the step-0 energy residual
                // enormous at the degenerate wall slivers (V ~ 1e-13) and
                // blows the run up.  The standard fix is the corrected
                // central gradient: take the cell-gradient average, strip
                // its component along the cell-to-cell connector, and add
                // the exact one-sided difference (phi_R - phi_L)/|dr| along
                // dr.  For a uniform state the correction is identically
                // zero, and near the wall it replaces the O(1/V) Green-Gauss
                // artifact with the well-conditioned cell-to-cell slope.
                Vec2 dr(local_centroids_[Rc][0] - local_centroids_[L][0],
                        local_centroids_[Rc][1] - local_centroids_[L][1]);
                real_t dr2 = dr[0] * dr[0] + dr[1] * dr[1];
                auto corrected = [&](const Vec2& gL, const Vec2& gR,
                                     real_t phiL, real_t phiR) {
                    Vec2 gavg = 0.5 * (gL + gR);
                    if (dr2 > 1e-30) {
                        real_t proj = (gavg[0] * dr[0] + gavg[1] * dr[1]) / dr2;
                        real_t slope = (phiR - phiL) / dr2;
                        gavg[0] += (slope - proj) * dr[0];
                        gavg[1] += (slope - proj) * dr[1];
                    }
                    return gavg;
                };
                Vec2 gu = corrected(ws.grads[L].gu, ws.grads[Rc].gu,
                                    ws.prims[L].u, ws.prims[Rc].u);
                Vec2 gv = corrected(ws.grads[L].gv, ws.grads[Rc].gv,
                                    ws.prims[L].v, ws.prims[Rc].v);
                Vec2 gT = corrected(ws.grads[L].gT, ws.grads[Rc].gT,
                                    ws.prims[L].T, ws.prims[Rc].T);
                StateVec Fv = viscous_flux_face(qL, qR, gu, gv, gT, nx, ny, S,
                                                 mu, gamma, Rgas, prandtl);
                spatial[L] -= Fv;
                spatial[Rc] += Fv;
                if (dbg_this_face && rank_ == 0) {
                    std::cerr << "CVISC f=" << f << " L=" << L << " R=" << Rc
                              << " Fv=(" << Fv[0] << "," << Fv[1] << "," << Fv[2]
                              << "," << Fv[3] << ")\n";
                    if (L == dbg_cell || Rc == dbg_cell) {
                        std::cerr << "CVISC2 f=" << f << " L=" << L << " R=" << Rc
                                  << " gu=(" << gu[0] << "," << gu[1] << ")"
                                  << " gv=(" << gv[0] << "," << gv[1] << ")"
                                  << " gT=(" << gT[0] << "," << gT[1] << ")"
                                  << " guL=(" << ws.grads[L].gu[0] << ","
                                  << ws.grads[L].gu[1] << ")"
                                  << " guR=(" << ws.grads[Rc].gu[0] << ","
                                  << ws.grads[Rc].gu[1] << ")"
                                  << " dr=(" << dr[0] << "," << dr[1] << ")"
                                  << "\n";
                    }
                }
            }
        } else {
            Prims qL = prims_from_conservative(U_[L], gamma, Rgas);
            BCType btype = bc_of_face_[f] ? wall_bc_[f] : BCType::Farfield;
            StateVec Fb(StateVec::Zero());

            switch (btype) {
            case BCType::Farfield: {
                Fb = rusanov_flux(U_[L], case_input_.freestream_state, nx, ny, S,
                                  gamma, Rgas, diss);
                break;
            }
            case BCType::SlipWall: {
                // Reflect the wall-normal velocity through the wall and use the
                // full numerical flux: at a tangent state this degenerates to
                // the pressure-only flux, but during the transient it adds the
                // dissipation that damps the normal-velocity component (the
                // bare pressure-only flux is undamped and destabilizes the
                // wall layer).
                real_t un = qL.u * nx + qL.v * ny;
                Prims qg = qL;
                qg.u -= 2.0 * un * nx;
                qg.v -= 2.0 * un * ny;
                Fb = rusanov_flux(U_[L], conservative_from_prims(qg, gamma),
                                  nx, ny, S, gamma, Rgas, diss);
                break;
            }
            case BCType::NoSlipAdiabaticWall: {
                Fb = pressure_only_flux(qL.p, nx, ny, S);
                if (viscous) {
                    // Wall distance: normal-projected distance from the cell
                    // centroid to the wall plane (not the Euclidean distance,
                    // which is polluted by the tangential offset on
                    // high-aspect-ratio wall cells), with a floor at half the
                    // cell length scale so the one-sided O(1/d) stress stays
                    // bounded at the degenerate TE/LE sliver cells.
                    // The mirrored-ghost normal derivative is -u_cell/d, O(1/d)
                    // even at the degenerate TE/LE sliver cells.
                    real_t dx = face.centroid[0] - local_centroids_[L][0];
                    real_t dy = face.centroid[1] - local_centroids_[L][1];
                    real_t d = std::fabs(dx * nx + dy * ny);
                    real_t d_eff = std::max(
                        d, 0.5 * std::sqrt(std::max(local_volumes_[L], 1e-30)));
                    if (d_eff > 0) {
                        real_t tau_xx, tau_yy, tau_xy;
                        wall_viscous_stress(qL.u, qL.v, nx, ny, d_eff, mu,
                                            tau_xx, tau_yy, tau_xy);
                        real_t txx_n = tau_xx * nx + tau_xy * ny;
                        real_t tyy_n = tau_xy * nx + tau_yy * ny;
                        // Conservative form: R = -(1/V)∮(F_i - F_v)·n dS.  At
                        // the wall, F_i·n = p n and F_v·n = τ·n, so the wall
                        // face contributes +(p n - τ n) S.  The viscous
                        // traction must be SUBTRACTED: with n pointing
                        // fluid->body, τ·n decelerates the boundary layer
                        // (anti-dissipative if added).
                        Fb[1] -= txx_n * S;
                        Fb[2] -= tyy_n * S;
                    }
                }
                break;
            }
            default:
                throw std::runtime_error("unhandled boundary condition");
            }
            spatial[L] += Fb;
            if (dbg_this_face && rank_ == 0 && L == dbg_cell) {
                std::cerr << "CBND f=" << f << " L=" << L
                          << " S=" << S << " n=(" << nx << "," << ny << ")"
                          << " bt=" << (int)btype
                          << " Fb=(" << Fb[0] << "," << Fb[1] << ","
                          << Fb[2] << "," << Fb[3] << ")"
                          << " qL=(rho=" << qL.rho << ",u=" << qL.u
                          << ",v=" << qL.v << ",p=" << qL.p << ")\n";
            }
        }
    }

    if (dbg_cell >= 0 && dbg_cell < local_mesh_.n_owned && rank_ == 0) {
        std::cerr << "CSUM cell=" << dbg_cell
                  << " spatial=(" << spatial[dbg_cell][0] << ","
                  << spatial[dbg_cell][1] << "," << spatial[dbg_cell][2] << ","
                  << spatial[dbg_cell][3] << ")"
                  << " R=(" << (-spatial[dbg_cell][0]/local_volumes_[dbg_cell])
                  << "," << (-spatial[dbg_cell][1]/local_volumes_[dbg_cell])
                  << "," << (-spatial[dbg_cell][2]/local_volumes_[dbg_cell])
                  << "," << (-spatial[dbg_cell][3]/local_volumes_[dbg_cell])
                  << ")\n";
    }

    // R = -spatial/V + physical-time term
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        real_t vol = local_mesh_.owned_cells[i].volume;
        StateVec Rt = -spatial[i] / vol;
        if (is_transient) {
            // Dual-time residual: R* = -(R_space + R_time), where
            // R_time = (3U - 4U^n + U^{n-1}) / (2 dt).  Driving R* -> 0 gives
            // the BDF2 update (3U^{n+1} - 4U^n + U^{n-1})/(2dt) + R_space = 0.
            Rt -= (3.0 * U_[i] - 4.0 * U_n[i] + U_nm1[i]) / (2.0 * dt);
        }
        R[i] = Rt;
    }

    // Residual norm: L2 over cells of the *volume-weighted* residual
    // |R_i * V_i| = |flux imbalance|.  Weighting the raw per-unit-volume
    // residual by V would let the tiny degenerate cells at the airfoil
    // leading/trailing edges (V ~ 1e-9) dominate the norm; the flux-imbalance
    // form gives every cell a physically meaningful weight.
    real_t l2 = 0, linf = 0;
    real_t rho_n = 0, rhou_n = 0, rhov_n = 0, rhoE_n = 0;
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        real_t vol = local_mesh_.owned_cells[i].volume;
        real_t v0 = R[i][0], v1 = R[i][1], v2 = R[i][2], v3 = R[i][3];
        real_t w0 = v0 * vol, w1 = v1 * vol, w2 = v2 * vol, w3 = v3 * vol;
        l2 += (w0*w0 + w1*w1 + w2*w2 + w3*w3);
        linf = std::max({linf, std::abs(v0), std::abs(v1), std::abs(v2), std::abs(v3)});
        rho_n += w0*w0; rhou_n += w1*w1;
        rhov_n += w2*w2; rhoE_n += w3*w3;
    }
    real_t red[5] = {l2, rho_n, rhou_n, rhov_n, rhoE_n};
    MPI_Allreduce(MPI_IN_PLACE, red, 5, MPI_DOUBLE, MPI_SUM, comm_);
    MPI_Allreduce(MPI_IN_PLACE, &linf, 1, MPI_DOUBLE, MPI_MAX, comm_);
    norm.l2 = std::sqrt(red[0]);
    norm.linf = linf;
    norm.rho = std::sqrt(red[1]);
    norm.rhou = std::sqrt(red[2]);
    norm.rhov = std::sqrt(red[3]);
    norm.rhoE = std::sqrt(red[4]);

    if (getenv("CFD_DEBUG_TOPRES") && rank_ == 0) {
        std::vector<std::pair<real_t, idx_t>> wlist;
        wlist.reserve(local_mesh_.n_owned);
        for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
            real_t w = (R[i] * local_volumes_[i]).norm();
            wlist.emplace_back(w, i);
        }
        std::partial_sort(wlist.begin(), wlist.begin() + 8, wlist.end(),
                          std::greater<std::pair<real_t, idx_t>>());
        for (int k = 0; k < 8; k++) {
            idx_t cid = wlist[k].second;
            std::cerr << "TOPRES cell=" << cid
                      << " w=" << wlist[k].first
                      << " at (" << local_centroids_[cid][0] << ","
                      << local_centroids_[cid][1] << ")"
                      << " vol=" << local_volumes_[cid]
                      << " R=(" << R[cid][0] << "," << R[cid][1] << ","
                      << R[cid][2] << "," << R[cid][3] << ")\n";
        }
    }

}

void Solver::lusgs_sweep(const std::vector<StateVec>& R, std::vector<StateVec>& U,
                         const real_t* dt_local, bool is_transient, real_t dt_phys) {
    if (getenv("CFD_EXPLICIT_EULER")) {
        for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
            U[i] += R[i] * dt_local[i];
        }
        if (getenv("CFD_DEBUG_UPD") && rank_ == 0) {
            idx_t imax = 0;
            real_t wmax = -1;
            for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
                real_t w = R[i].cwiseAbs().maxCoeff() * dt_local[i];
                if (w > wmax) { wmax = w; imax = i; }
            }
            std::cerr << "UPD maxdU=" << wmax << " cell=" << imax
                      << " at (" << local_centroids_[imax][0] << "," << local_centroids_[imax][1] << ")"
                      << " vol=" << local_volumes_[imax]
                      << " R=(" << R[imax][0] << "," << R[imax][1] << ","
                      << R[imax][2] << "," << R[imax][3] << ")"
                      << " dt=" << dt_local[imax] << "\n";
            if (getenv("CFD_DEBUG_FACES")) {
                for (idx_t fid : local_mesh_.owned_cells[imax].face_ids) {
                    const auto& face = local_mesh_.faces[fid];
                    std::cerr << "  face " << fid << " L=" << face.left_cell
                              << " R=" << face.right_cell
                              << " n=(" << face.normal[0] << "," << face.normal[1]
                              << ") S=" << std::sqrt(face.normal[0]*face.normal[0]+face.normal[1]*face.normal[1])
                              << "\n";
                }
            }
        }
        return;
    }
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;
    const real_t diss = case_input_.rusanov_dissipation_scale;
    const real_t mu = case_input_.mu_ref;
    const real_t prandtl = case_input_.prandtl;
    const bool viscous = case_input_.physics_mode == "laminar";
    const auto& faces = local_mesh_.faces;
    // Under-relaxation of the LU-SGS update.  On the extreme-aspect-ratio
    // wall cells of these meshes the unrelaxed scalar update overshoots and
    // the inner iteration enters a limit cycle; omega < 1 damps it.
    real_t omega = 0.7;
    if (const char* e = getenv("CFD_LUSGS_RELAX")) omega = std::atof(e);
    // Diagonal scale: the standard scalar LU-SGS uses 0.5*(lambda+ + lambda-)
    // = 0.5*kappa*S per face.  On the extreme-aspect-ratio cells of these
    // meshes that diagonal is not dominant enough for the Gauss-Seidel inner
    // iteration to converge (the inner residual stalls), so the diagonal can
    // be strengthened by this factor (2.0 gives the full spectral radius
    // kappa*S per face).
    real_t diag_factor = 1.0;
    if (const char* e = getenv("CFD_LUSGS_DIAG_FACTOR")) diag_factor = std::atof(e);

    // Face coupling coefficients (direction-split scalar LU-SGS).
    // For a face with left cell L and right cell R (normal L->R), the
    // linearized flux change is  dF ≈ A^+ dU_L + A^- dU_R  with the scalar
    // approximations  A^+ ≈ 0.5*(kappa + un),  A^- ≈ 0.5*(un - kappa)
    // (un = contravariant velocity, kappa = |un| + a spectral radius).
    // The implicit-matrix entries are therefore
    //   J_{L,R} = +A^- S = -0.5*(kappa - un) S      (L row, R column)
    //   J_{R,L} = -A^+ S = -0.5*(kappa + un) S      (R row, L column)
    // and the sweep RHS accumulates -J_{i,j} dU_j:
    //   cell L receives +0.5*(kappa - un) S dU_R
    //   cell R receives +0.5*(kappa + un) S dU_L
    // This upwind-weighted coupling is the standard LU-SGS; a symmetric
    // 0.5*kappa*S coupling to both neighbors under-couples the upwind side,
    // spurious-couples the downwind side, and stalls the inner iteration.
    const real_t cv = Rgas / (gamma - 1.0);
    const real_t kk = (viscous && mu > 0) ? mu * gamma * Rgas / ((gamma - 1.0) * prandtl) : 0.0;
    std::vector<real_t> lam_plus(faces.size(), 0.0);  // R-row coupling: 0.5*(kappa + un) S
    std::vector<real_t> lam_minus(faces.size(), 0.0); // L-row coupling: 0.5*(kappa - un) S
    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        idx_t L = face.left_cell, Rc = face.right_cell;
        real_t nx = face.normal[0], ny = face.normal[1];
        real_t S = std::sqrt(nx * nx + ny * ny);
        if (S <= 0) continue;
        nx /= S; ny /= S;
        real_t un = 0, kappa = 0;
        if (L >= 0) {
            auto qL = prims_from_conservative(U[L], gamma, Rgas);
            real_t unL = qL.u * nx + qL.v * ny;
            un = unL;
            kappa = std::abs(unL) + qL.a;
        }
        if (Rc >= 0) {
            auto qR = prims_from_conservative(U[Rc], gamma, Rgas);
            real_t unR = qR.u * nx + qR.v * ny;
            un = 0.5 * (un + unR);
            kappa = std::max(kappa, std::abs(unR) + qR.a);
        }
        kappa *= diss;
        lam_plus[f] = 0.5 * (kappa + un) * S;
        lam_minus[f] = 0.5 * (kappa - un) * S;
    }

    // Cell diagonal: V/dt_p + 3V/(2dt_phys) + sum_f lam_f
    // (the physical-time contribution is included in the diagonal; the RHS
    // residual already contains the BDF2 source term)
    std::vector<real_t> diag(local_mesh_.n_total, 0.0);
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        real_t vol = local_mesh_.owned_cells[i].volume;
        real_t d = vol / std::max(dt_local[i], 1e-30);
        if (is_transient) d += 1.5 * vol / dt_phys;
        // Viscous spectral radius lumped on the diagonal: for each face the
        // diffusion eigenvalue is ~ dv*S^2/V (dv = max(mu/rho, kk/(rho*cv))),
        // so the diagonal must carry that term or the viscous part of the
        // residual is effectively stepped explicitly with the convective dt.
        real_t dv_i = 0.0;
        if (viscous && mu > 0) {
            auto qi = prims_from_conservative(U[i], gamma, Rgas);
            dv_i = std::max(mu / qi.rho, kk / (qi.rho * cv));
        }
        for (idx_t fid : local_mesh_.owned_cells[i].face_ids) {
            const auto& face = faces[fid];
            real_t Sf = std::sqrt(face.normal[0] * face.normal[0] +
                                  face.normal[1] * face.normal[1]);
            // symmetric spectral-radius diagonal: 0.5*(kappa + kappa) S / 2
            // accumulated once per face from the two half contributions.
            d += diag_factor * 0.5 * (lam_plus[fid] + lam_minus[fid]);
            if (viscous && mu > 0) d += diag_factor * dv_i * Sf * Sf / vol;
        }
        diag[i] = d;
    }

    std::vector<StateVec> dU(local_mesh_.n_owned, StateVec::Zero());

    // Forward sweep
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        // The implicit system is (V/dt + ...) dU = V*R: the residual is the
        // cell-averaged (per-unit-volume) quantity, so the RHS must carry the
        // cell volume to keep the update dimensionless in U.
        StateVec rhs = R[i] * local_mesh_.owned_cells[i].volume;
        for (idx_t fid : local_mesh_.owned_cells[i].face_ids) {
            const auto& face = faces[fid];
            idx_t Lc = face.left_cell, Rr = face.right_cell;
            idx_t nb = (Lc == i) ? Rr : Lc;
            if (nb < 0) continue;
            if (nb >= local_mesh_.n_owned) continue;  // ghost: explicit (zero dU)
            if (nb >= i) continue;                    // only already-swept cells
            real_t coeff = (Lc == i) ? lam_minus[fid] : lam_plus[fid];
            rhs += coeff * dU[nb];
        }
        dU[i] = omega * rhs / diag[i];
    }

    // Backward sweep
    for (idx_t i = local_mesh_.n_owned - 1; i >= 0; i--) {
        StateVec rhs = R[i] * local_mesh_.owned_cells[i].volume;
        for (idx_t fid : local_mesh_.owned_cells[i].face_ids) {
            const auto& face = faces[fid];
            idx_t Lc = face.left_cell, Rr = face.right_cell;
            idx_t nb = (Lc == i) ? Rr : Lc;
            if (nb < 0) continue;
            if (nb >= local_mesh_.n_owned) continue;  // ghost: explicit (zero dU)
            if (nb <= i) continue;                    // only already-swept cells
            real_t coeff = (Lc == i) ? lam_minus[fid] : lam_plus[fid];
            rhs += coeff * dU[nb];
        }
        dU[i] = omega * rhs / diag[i];
    }

    // Per-cell update limiter (state-scale cap), matching the block-Jacobi
    // apply_correction limiter.  On the degenerate LE/TE and wake sliver
    // cells (V ~ 1e-9) the raw scalar-LU-SGS update can be O(1) in the
    // conservative variables even when the volume-weighted flux imbalance is
    // modest, because the per-unit-volume residual is O(1/V).  Applying such
    // an update makes the reconstructed face states (and the next residual)
    // enormous and the inner iteration diverges exponentially.  A global
    // factor for all cells stalls the whole solve (one near-vacuum sliver
    // shrinks the step of every healthy cell to ~0), so each cell instead
    // gets its own factor: healthy cells advance while the slivers creep
    // forward.  Set CFD_LUSGS_GLOBAL_LIMIT=1 to restore the old global cap.
    real_t limiter_frac = getenv("CFD_LUSGS_LIMIT")
        ? std::atof(getenv("CFD_LUSGS_LIMIT")) : 0.25;
    const bool global_limit = getenv("CFD_LUSGS_GLOBAL_LIMIT") != nullptr;
    real_t max_ratio_global = 0.0;
    if (global_limit) {
        for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
            const StateVec& Ui = U[i];
            real_t rho_i = std::max(Ui[0], 1e-30);
            Prims qi = prims_from_conservative(Ui, gamma, Rgas);
            real_t scale[4] = {rho_i,
                               rho_i * (std::fabs(qi.u) + qi.a),
                               rho_i * (std::fabs(qi.v) + qi.a),
                               std::max(std::fabs(Ui[3]),
                                        qi.p / (gamma - 1.0))};
            for (int c = 0; c < 4; c++) {
                real_t ratio = std::fabs(dU[i][c]) / std::max(scale[c], 1e-30);
                if (ratio > max_ratio_global) max_ratio_global = ratio;
            }
        }
    }
    const real_t alpha_global =
        (max_ratio_global > limiter_frac) ? limiter_frac / max_ratio_global : 1.0;

    idx_t n_reject = 0, n_half = 0, n_full = 0;
    real_t alpha_last = 1.0;
    real_t dU_l2max = 0;
    for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
        auto physical = [&](const StateVec& s) {
            if (s[0] <= 0) return false;
            real_t rho = s[0];
            real_t ke = 0.5 * (s[1] * s[1] + s[2] * s[2]) / rho;
            real_t p = (gamma - 1.0) * (s[3] - ke);
            return p > 0;
        };
        real_t alpha = alpha_global;
        if (!global_limit) {
            const StateVec& Ui = U[i];
            real_t rho_i = std::max(Ui[0], 1e-30);
            Prims qi = prims_from_conservative(Ui, gamma, Rgas);
            real_t scale[4] = {rho_i,
                               rho_i * (std::fabs(qi.u) + qi.a),
                               rho_i * (std::fabs(qi.v) + qi.a),
                               std::max(std::fabs(Ui[3]),
                                        qi.p / (gamma - 1.0))};
            real_t ratio = 0.0;
            for (int c = 0; c < 4; c++) {
                ratio = std::max(ratio, std::fabs(dU[i][c]) /
                                        std::max(scale[c], 1e-30));
            }
            alpha = (ratio > limiter_frac) ? limiter_frac / ratio : 1.0;
            for (int bt = 0; bt < 45; bt++) {
                StateVec trial = Ui + alpha * dU[i];
                if (trial[0] > 0) {
                    real_t ke = 0.5 * (trial[1] * trial[1] +
                                       trial[2] * trial[2]) / trial[0];
                    if ((gamma - 1.0) * (trial[3] - ke) > 0) break;
                }
                alpha *= 0.5;
            }
        }
        StateVec limited = dU[i] * alpha;
        alpha_last = alpha;
        StateVec trial = U[i] + limited;
        if (!physical(trial)) {
            trial = U[i] + 0.5 * limited;
            if (!physical(trial)) {
                trial = U[i]; // reject the update
                n_reject++;
            } else {
                n_half++;
            }
        } else {
            n_full++;
        }
        real_t w = limited.cwiseAbs().maxCoeff();
        if (w > dU_l2max) dU_l2max = w;
        U[i] = trial;
    }
    if (getenv("CFD_DEBUG_UPD") && rank_ == 0) {
        std::cerr << "LU-SGS apply: full=" << n_full << " half=" << n_half
                  << " alpha=" << alpha_last << " maxratio=" << max_ratio_global
                  << " reject=" << n_reject << " max|dU|=" << dU_l2max << "\n";
        // top flux-imbalance cells
        std::vector<std::pair<real_t, idx_t>> wlist;
        wlist.reserve(local_mesh_.n_owned);
        for (idx_t i = 0; i < local_mesh_.n_owned; i++) {
            real_t w = (R[i] * local_mesh_.owned_cells[i].volume).norm();
            wlist.emplace_back(w, i);
        }
        std::partial_sort(wlist.begin(), wlist.begin() + 5, wlist.end(),
                          std::greater<std::pair<real_t, idx_t>>());
        for (int k = 0; k < 5; k++) {
            idx_t c = wlist[k].second;
            std::cerr << "  R*V cell=" << c << " w=" << wlist[k].first
                      << " at (" << local_centroids_[c][0] << "," << local_centroids_[c][1] << ")"
                      << " vol=" << local_volumes_[c] << "\n";
        }
    }
    if (const char* e = getenv("CFD_DEBUG_CELL")) {
        idx_t dbg_cell = (idx_t)std::atoll(e);
        if (rank_ == 0 && dbg_cell >= 0 && dbg_cell < local_mesh_.n_owned) {
            std::cerr << "CELL " << dbg_cell << " U=(" << U[dbg_cell][0] << ","
                      << U[dbg_cell][1] << "," << U[dbg_cell][2] << ","
                      << U[dbg_cell][3] << ") R*V=(" << R[dbg_cell][0]*local_volumes_[dbg_cell]
                      << "," << R[dbg_cell][1]*local_volumes_[dbg_cell] << ","
                      << R[dbg_cell][2]*local_volumes_[dbg_cell] << ","
                      << R[dbg_cell][3]*local_volumes_[dbg_cell] << ")"
                      << " diag=(";
            // recompute diagonal for this cell (same formula as the sweep)
            const real_t gamma = case_input_.gamma;
            const real_t Rgas = case_input_.R;
            const real_t prandtl = case_input_.prandtl;
            const real_t mu = case_input_.mu_ref;
            const bool viscous = case_input_.physics_mode == "laminar";
            real_t cv = Rgas / (gamma - 1.0);
            real_t kk = (viscous && mu > 0) ? mu * gamma * Rgas / ((gamma - 1.0) * prandtl) : 0.0;
            Prims qi = prims_from_conservative(U[dbg_cell], gamma, Rgas);
            real_t dv_i = (viscous && mu > 0)
                ? std::max(mu / qi.rho, kk / (qi.rho * cv)) : 0.0;
            real_t dd = local_volumes_[dbg_cell] / std::max(dt_local[dbg_cell], 1e-30);
            for (idx_t fid : local_mesh_.owned_cells[dbg_cell].face_ids) {
                const auto& face = faces[fid];
                real_t Sf = std::sqrt(face.normal[0] * face.normal[0] +
                                      face.normal[1] * face.normal[1]);
                dd += 2.0 * 0.5 * (lam_plus[fid] + lam_minus[fid]);
                if (viscous && mu > 0) dd += 2.0 * dv_i * Sf * Sf / local_volumes_[dbg_cell];
            }
            std::cerr << dd << ")\n";
        }
    }
}

real_t Solver::block_jacobi_correction(const std::vector<StateVec>& R,
                                       const std::vector<StateVec>& pseudo_old,
                                       const real_t* dt_local,
                                       real_t dt_phys) {
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;
    const real_t diss = case_input_.rusanov_dissipation_scale;
    const real_t mu = case_input_.mu_ref;
    const real_t prandtl = case_input_.prandtl;
    const bool viscous = case_input_.physics_mode == "laminar";
    const auto& faces = local_mesh_.faces;
    const idx_t n_owned = local_mesh_.n_owned;
    const idx_t n_total = local_mesh_.n_total;

    // Pseudo-time coefficient c[i] = V / dt_p (the full spectral-radius sum
    // including the viscous part, matching the reference block-Jacobi
    // solver's pseudo_coefficient = evaluation.diagonal / CFL).
    std::vector<real_t> c(n_owned, 0.0);
    for (idx_t i = 0; i < n_owned; i++) {
        c[i] = local_volumes_[i] / std::max(dt_local[i], 1e-30);
    }

    // Norm of the inner-solve residual evaluated at the current state before
    // the correction.  In the steady pseudo-transient continuation the
    // inner equation is spatial(U) + (U - U_old)/dt_p = 0, so the
    // pseudo-time term belongs in the norm.  In the transient dual-time
    // solve the physical equation spatial(U) + V*BDF2(U) = 0 is being
    // solved and the pseudo-time term is a pure diagonal preconditioner --
    // including it (with the fixed reference U^n) would make the inner
    // solve converge back to U^n instead of U^{n+1}.
    real_t local_l2 = 0.0;
    for (idx_t i = 0; i < n_owned; i++) {
        StateVec G = (dt_phys > 0)
            ? R[i]
            : R[i] - (U_[i] - pseudo_old[i]) / std::max(dt_local[i], 1e-30);
        StateVec w = G * local_volumes_[i];
        local_l2 += w.squaredNorm();
    }
    MPI_Allreduce(MPI_IN_PLACE, &local_l2, 1, MPI_DOUBLE, MPI_SUM, comm_);
    const real_t pseudo_norm = std::sqrt(local_l2);
    if (getenv("CFD_DEBUG_BLKJ") && rank_ == 0) {
        // Top cells by volume-weighted flux imbalance with conditioning info.
        std::vector<std::pair<real_t, idx_t>> wlist;
        wlist.reserve(n_owned);
        for (idx_t i = 0; i < n_owned; i++) {
            real_t w = (R[i] * local_volumes_[i]).norm();
            wlist.emplace_back(w, i);
        }
        std::partial_sort(wlist.begin(), wlist.begin() + 5, wlist.end(),
                          std::greater<std::pair<real_t, idx_t>>());
        for (int k = 0; k < 5; k++) {
            idx_t cid = wlist[k].second;
            std::cerr << "TOPBLKJ cell=" << cid
                      << " w=" << wlist[k].first
                      << " at (" << local_centroids_[cid][0] << ","
                      << local_centroids_[cid][1] << ")"
                      << " vol=" << local_volumes_[cid]
                      << " c=" << c[cid]
                      << " R=(" << R[cid][0] << "," << R[cid][1] << ","
                      << R[cid][2] << "," << R[cid][3] << ")\n";
        }
    }

    std::vector<FaceImplicit> fimp(faces.size());
    std::vector<Mat4> block_diag(n_owned, Mat4::Zero());
    assemble_inner_jacobian(block_diag, fimp, dt_local, dt_phys);

    // Damped block-Jacobi sweeps: fewer, more conservative sweeps for the
    // transient dual-time system (the physical-time mass dominates the
    // diagonal, so the face coupling is weaker and 2-3 sweeps suffice).
    int min_sweeps = (dt_phys > 0) ? 2 : 2;
    int max_sweeps = (dt_phys > 0) ? 3 : 8;
    real_t target_ratio = (dt_phys > 0) ? 0.2 : 0.05;
    real_t damp = (dt_phys > 0) ? 0.2 : 0.8;
    if (const char* e = getenv("CFD_BLOCKJ_SWEEPS_MIN")) min_sweeps = std::atoi(e);
    if (const char* e = getenv("CFD_BLOCKJ_SWEEPS_MAX")) max_sweeps = std::atoi(e);
    if (const char* e = getenv("CFD_BLOCKJ_TARGET")) target_ratio = std::atof(e);
    if (const char* e = getenv("CFD_BLOCKJ_DAMP")) damp = std::atof(e);

    // Damped block-Jacobi sweeps: solve the block-diagonal system with the
    // face coupling carried to the right-hand side.
    std::vector<StateVec> correction(n_total, StateVec::Zero());
    std::vector<StateVec> previous(n_total, StateVec::Zero());
    std::vector<StateVec> next(n_owned);
    int sweeps = 0;
    real_t initial_change = 1.0;
    for (; sweeps < max_sweeps; sweeps++) {
        exchange_correction(correction);
        previous = correction;
        for (idx_t i = 0; i < n_owned; i++) {
            StateVec neighbor_sum = StateVec::Zero();
            for (idx_t fid : local_mesh_.owned_cells[i].face_ids) {
                const auto& face = faces[fid];
                idx_t Lc = face.left_cell, Rr = face.right_cell;
                idx_t other = -1;
                if (Lc == i) other = Rr;
                else if (Rr == i) other = Lc;
                if (other < 0 || !fimp[fid].used) continue;
                const Mat4& blk =
                    (Lc == i) ? fimp[fid].off_owner : fimp[fid].off_neighbor;
                neighbor_sum += blk * previous[other];
            }
            // Newton step for G(U) = 0, where G is the inner equation from
            // above.  For the steady pseudo-transient continuation G =
            // spatial + c*(U - U_old) and the RHS carries -c*(U-U_old); for
            // the transient dual-time solve G = spatial + V*BDF2 (the
            // physical residual, R*V) and the pseudo-time diagonal c I acts
            // only as a conditioning/trust-region boost.
            StateVec rhs = R[i] * local_volumes_[i] - neighbor_sum;
            if (dt_phys <= 0)
                rhs -= c[i] * (U_[i] - pseudo_old[i]);
            StateVec jacobi = solve_block4(block_diag[i], rhs);
            next[i] = damp * jacobi + (1.0 - damp) * previous[i];
            if (getenv("CFD_DEBUG_BLKCELL") && rank_ == 0 &&
                i == (idx_t)std::atoll(getenv("CFD_DEBUG_BLKCELL"))) {
                std::cerr << "BLKCELL i=" << i << " sweep=" << sweeps
                          << " diag=" << block_diag[i].diagonal().transpose()
                          << " rhs=(" << rhs[0] << "," << rhs[1] << ","
                          << rhs[2] << "," << rhs[3] << ")"
                          << " corr=(" << jacobi[0] << "," << jacobi[1] << ","
                          << jacobi[2] << "," << jacobi[3] << ")"
                          << " U=(" << U_[i][0] << "," << U_[i][1] << ","
                          << U_[i][2] << "," << U_[i][3] << ")"
                          << " R=(" << R[i][0] << "," << R[i][1] << ","
                          << R[i][2] << "," << R[i][3] << ")\n";
            }
        }
        real_t local_change = 0.0;
        for (idx_t i = 0; i < n_owned; i++) {
            StateVec diff = next[i] - previous[i];
            local_change += diff.squaredNorm();
            correction[i] = next[i];
        }
        real_t global_change = 0.0;
        MPI_Allreduce(&local_change, &global_change, 1, MPI_DOUBLE, MPI_SUM, comm_);
        global_change = std::sqrt(global_change);
        if (sweeps == 0) initial_change = std::max(global_change, 1e-300);
        const int completed = sweeps + 1;
        if (completed >= min_sweeps && global_change / initial_change <= target_ratio) {
            ++sweeps;
            break;
        }
    }

    // Per-cell state-scale limiter with local positivity backtracking.  A
    // global damping factor is fatal on meshes with degenerate TE/wake sliver
    // cells: one near-vacuum sliver (V ~ 1e-9) shrinks the step for every
    // cell to ~0 and stalls the whole solve.  Each cell instead gets its own
    // factor, so healthy cells advance while the slivers creep forward.
    real_t alpha_min = 1.0;
    idx_t limited_cells = 0;
    real_t corr_norm2 = 0.0;
    for (idx_t i = 0; i < n_owned; i++) {
        corr_norm2 += correction[i].squaredNorm();
        const StateVec& Ui = U_[i];
        Prims qi = prims_from_conservative(Ui, gamma, Rgas);
        real_t scale[4] = {std::max(Ui[0], 1e-30),
                           Ui[0] * (std::fabs(qi.u) + qi.a),
                           Ui[0] * (std::fabs(qi.v) + qi.a),
                           std::max(std::fabs(Ui[3]), qi.p / (gamma - 1.0))};
        real_t ratio = 0.0;
        for (int comp = 0; comp < 4; comp++) {
            ratio = std::max(ratio, std::fabs(correction[i][comp]) /
                                    std::max(scale[comp], 1e-30));
        }
        real_t alpha = (ratio > 0.25) ? 0.25 / ratio : 1.0;
        for (int bt = 0; bt < 45; bt++) {
            StateVec trial = Ui + alpha * correction[i];
            if (trial[0] > 0) {
                real_t ke = 0.5 * (trial[1] * trial[1] + trial[2] * trial[2]) / trial[0];
                if ((gamma - 1.0) * (trial[3] - ke) > 0) break;
            }
            alpha *= 0.5;
        }
        if (alpha < alpha_min) alpha_min = alpha;
        if (alpha < 1.0) limited_cells++;
        if (alpha > 0) U_[i] += alpha * correction[i];
    }

    if (getenv("CFD_DEBUG_UPD") && rank_ == 0) {
        std::cerr << "BLKJ sweeps=" << sweeps << " alpha_min=" << alpha_min
                  << " limited_cells=" << limited_cells << "/" << n_owned
                  << " corr_norm=" << std::sqrt(corr_norm2)
                  << " pseudo_norm=" << pseudo_norm << "\n";
    }
    return pseudo_norm;
}

ForceCoeffs Solver::compute_forces(bool gather_surface) {
    const real_t gamma = case_input_.gamma;
    const real_t Rgas = case_input_.R;
    const real_t mu = case_input_.mu_ref;
    const bool viscous = case_input_.physics_mode == "laminar";
    const real_t qinf = 0.5 * case_input_.rho_inf *
                        (case_input_.u_inf * case_input_.u_inf + case_input_.v_inf * case_input_.v_inf);
    const real_t c_ref = case_input_.ref_length;
    const real_t a_ref = case_input_.ref_area;

    ForceCoeffs fc;
    std::vector<WallRow> rows;
    const auto& faces = local_mesh_.faces;

    for (idx_t f = 0; f < (idx_t)faces.size(); f++) {
        const auto& face = faces[f];
        if (face.right_cell >= 0) continue;
        BCType btype = wall_bc_[f];
        if (btype != BCType::SlipWall && btype != BCType::NoSlipAdiabaticWall) continue;

        idx_t L = face.left_cell;
        Prims qL = prims_from_conservative(U_[L], gamma, Rgas);
        real_t nx = face.normal[0], ny = face.normal[1];
        real_t S = std::sqrt(nx * nx + ny * ny);
        if (S <= 0) continue;
        real_t nxn = nx / S, nyn = ny / S;

        real_t pressure = qL.p;
        real_t tau_xx = 0, tau_yy = 0, tau_xy = 0;
        if (viscous && btype == BCType::NoSlipAdiabaticWall) {
            // Mirrored-ghost wall stress (see wall_viscous_stress): O(1/d)
            // and well-conditioned at the degenerate wall cells where the
            // cell-centered Green-Gauss gradient is O(1/V).  Use the
            // normal-projected wall distance with a floor at half the cell
            // length scale (same conditioning as compute_residual).
            real_t dx = face.centroid[0] - local_centroids_[L][0];
            real_t dy = face.centroid[1] - local_centroids_[L][1];
            real_t d = std::fabs(dx * nxn + dy * nyn);
            real_t d_eff = std::max(
                d, 0.5 * std::sqrt(std::max(local_volumes_[L], 1e-30)));
            if (d_eff > 0) {
                wall_viscous_stress(qL.u, qL.v, nxn, nyn, d_eff, mu,
                                    tau_xx, tau_yy, tau_xy);
            }
        }

        // Mesh wall normals point from the fluid cell toward the wall (into the
        // body), so the force exerted by the fluid on the body is
        // +p*n_hat*S - (tau.n_hat)*S.
        real_t Fx = (pressure * nxn - (tau_xx * nxn + tau_xy * nyn)) * S;
        real_t Fy = (pressure * nyn - (tau_xy * nxn + tau_yy * nyn)) * S;
        real_t pFx = pressure * nxn * S;
        real_t pFy = pressure * nyn * S;

        fc.pressure_drag += pFx;
        fc.pressure_lift += pFy;
        fc.viscous_drag += Fx - pFx;
        fc.viscous_lift += Fy - pFy;

        real_t rx = face.centroid[0] - case_input_.moment_center[0];
        real_t ry = face.centroid[1] - case_input_.moment_center[1];
        fc.cmz += rx * Fy - ry * Fx;

        if (gather_surface) {
            WallRow row;
            row.x = face.centroid[0];
            row.y = face.centroid[1];
            row.nx = nxn;
            row.ny = nyn;
            row.pressure = pressure;
            row.cp = (pressure - case_input_.p_inf) / qinf;
            // skin friction from tangential shear
            real_t txx_n = tau_xx * nxn + tau_xy * nyn;
            real_t tyy_n = tau_xy * nxn + tau_yy * nyn;
            real_t tx = txx_n - pressure * nxn;
            real_t ty = tyy_n - pressure * nyn;
            real_t tn = tx * nxn + ty * nyn;
            real_t txx_t = tx - tn * nxn;
            real_t tyy_t = ty - tn * nyn;
            real_t tau_w = std::sqrt(txx_t * txx_t + tyy_t * tyy_t);
            row.cf = tau_w / qinf;
            row.rho = qL.rho;
            if (btype == BCType::NoSlipAdiabaticWall) {
                row.u = 0; row.v = 0; row.mach = 0;
            } else {
                real_t un = qL.u * nxn + qL.v * nyn;
                row.u = qL.u - un * nxn;
                row.v = qL.v - un * nyn;
                row.mach = std::sqrt(row.u * row.u + row.v * row.v) / qL.a;
            }
            row.tag = face_tag_name_[f].empty() ? "wall" : face_tag_name_[f];
            rows.push_back(std::move(row));
        }
    }

    real_t local[5] = {fc.pressure_drag, fc.pressure_lift, fc.viscous_drag,
                       fc.viscous_lift, fc.cmz};
    real_t global[5] = {0};
    MPI_Allreduce(local, global, 5, MPI_DOUBLE, MPI_SUM, comm_);
    fc.pressure_drag = global[0] / qinf / c_ref;
    fc.pressure_lift = global[1] / qinf / c_ref;
    fc.viscous_drag = global[2] / qinf / c_ref;
    fc.viscous_lift = global[3] / qinf / c_ref;
    fc.cmz = global[4] / qinf / c_ref / c_ref;
    fc.cl = fc.pressure_lift + fc.viscous_lift;
    fc.cd = fc.pressure_drag + fc.viscous_drag;

    if (gather_surface) {
        int n = (int)rows.size();
        std::vector<int> counts(n_ranks_);
        MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm_);
        std::vector<int> displs(n_ranks_, 0);
        int total = 0;
        for (int r = 0; r < n_ranks_; r++) { displs[r] = total; total += counts[r]; }
        constexpr int NP = 11;
        std::vector<real_t> local_buf(n * NP), recv_buf(total * NP);
        for (int i = 0; i < n; i++) {
            local_buf[i*NP+0] = rows[i].x; local_buf[i*NP+1] = rows[i].y;
            local_buf[i*NP+2] = rows[i].nx; local_buf[i*NP+3] = rows[i].ny;
            local_buf[i*NP+4] = rows[i].pressure; local_buf[i*NP+5] = rows[i].cp;
            local_buf[i*NP+6] = rows[i].cf; local_buf[i*NP+7] = rows[i].rho;
            local_buf[i*NP+8] = rows[i].u; local_buf[i*NP+9] = rows[i].v;
            local_buf[i*NP+10] = rows[i].mach;
        }
        std::vector<int> recvcounts(n_ranks_), recvdispls(n_ranks_);
        for (int r = 0; r < n_ranks_; r++) {
            recvcounts[r] = counts[r] * NP;
            recvdispls[r] = displs[r] * NP;
        }
        MPI_Gatherv(local_buf.data(), n * NP, MPI_DOUBLE, recv_buf.data(),
                    recvcounts.data(), recvdispls.data(), MPI_DOUBLE, 0, comm_);
        if (rank_ == 0) {
            wall_rows_.clear();
            for (int r = 0; r < n_ranks_; r++) {
                for (int k = 0; k < counts[r]; k++) {
                    WallRow row;
                    row.x = recv_buf[(displs[r]+k)*NP+0];
                    row.y = recv_buf[(displs[r]+k)*NP+1];
                    row.nx = recv_buf[(displs[r]+k)*NP+2];
                    row.ny = recv_buf[(displs[r]+k)*NP+3];
                    row.pressure = recv_buf[(displs[r]+k)*NP+4];
                    row.cp = recv_buf[(displs[r]+k)*NP+5];
                    row.cf = recv_buf[(displs[r]+k)*NP+6];
                    row.rho = recv_buf[(displs[r]+k)*NP+7];
                    row.u = recv_buf[(displs[r]+k)*NP+8];
                    row.v = recv_buf[(displs[r]+k)*NP+9];
                    row.mach = recv_buf[(displs[r]+k)*NP+10];
                    row.tag = "wall";
                    wall_rows_.push_back(std::move(row));
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

// ---------------------------------------------------------------------------
// Reconstruction helpers
// ---------------------------------------------------------------------------

void compute_gradients(const Solver& solver, const std::vector<StateVec>& U,
                       GradWorkspace& ws) {
    const auto& mesh = solver.mesh();
    const auto& ci = solver.case_input();
    const real_t gamma = ci.gamma, R = ci.R;
    // NOTE: PrimGrad{} does NOT zero Eigen members (Eigen's default ctor
    // leaves fixed-size vectors uninitialized), so zero each gradient
    // explicitly.  Ghost-cell gradients are later averaged from owned
    // neighbors; starting from garbage poisoned them with NaN on np>1.
    ws.grads.resize(mesh.n_total);
    for (auto& g : ws.grads) {
        g.grho.setZero();
        g.gu.setZero();
        g.gv.setZero();
        g.gT.setZero();
    }
    ws.prims.assign(mesh.n_total, Prims{});

    for (idx_t i = 0; i < mesh.n_total; i++) {
        ws.prims[i] = prims_from_conservative(U[i], gamma, R);
    }

    // Weighted least-squares gradients (1/d^2 weights over the face-adjacent
    // stencil, with mirrored-ghost samples on boundary faces).  Green-Gauss
    // divides the face-moment sum by the cell volume, so on the degenerate
    // leading/trailing-edge and wake sliver cells (V ~ 1e-9) it produces
    // O(1/V) gradient artifacts that leak through the corrected-central
    // viscous gradient (the component perpendicular to the cell connector is
    // not corrected) and the wall stress, destabilizing the run.  WLS is
    // bounded by the neighbor distances regardless of the cell volume.
    for (idx_t i = 0; i < mesh.n_owned; i++) {
        const Vec2 cc = solver.local_centroids()[i];
        const Prims& qc = ws.prims[i];
        real_t mxx = 0, mxy = 0, myy = 0;
        real_t bx[4] = {0, 0, 0, 0}, by[4] = {0, 0, 0, 0};
        real_t strongest_d2 = 0, sx = 0, sy = 0;
        auto add_sample = [&](const Vec2& loc, const Prims& q) {
            Vec2 dr(loc[0] - cc[0], loc[1] - cc[1]);
            real_t d2 = dr[0] * dr[0] + dr[1] * dr[1];
            if (!(d2 > 1e-30)) return;
            real_t w = 1.0 / d2;
            mxx += w * dr[0] * dr[0];
            mxy += w * dr[0] * dr[1];
            myy += w * dr[1] * dr[1];
            if (d2 > strongest_d2) { strongest_d2 = d2; sx = dr[0]; sy = dr[1]; }
            real_t vals[4] = {q.rho - qc.rho, q.u - qc.u, q.v - qc.v, q.T - qc.T};
            for (int k = 0; k < 4; k++) {
                bx[k] += w * dr[0] * vals[k];
                by[k] += w * dr[1] * vals[k];
            }
        };
        for (idx_t fid : mesh.owned_cells[i].face_ids) {
            const auto& face = mesh.faces[fid];
            idx_t nb = (face.left_cell == i) ? face.right_cell : face.left_cell;
            if (nb >= 0) {
                add_sample(solver.local_centroids()[nb], ws.prims[nb]);
            } else {
                // Mirrored-ghost sample across the boundary face.
                Vec2 gcent(2.0 * face.centroid[0] - cc[0],
                           2.0 * face.centroid[1] - cc[1]);
                Prims qg = qc;
                BCType bt = solver.wall_bc_of_face()[fid];
                if (bt == BCType::Farfield) {
                    qg = prims_from_conservative(ci.freestream_state, gamma, R);
                } else if (bt == BCType::NoSlipAdiabaticWall) {
                    // No-slip wall: do NOT mirror the velocity into the
                    // gradient stencil.  The mirrored ghost (u_g = -u_cell)
                    // injects an O(2u/d) one-sided boundary-layer gradient
                    // into the wall cell, and at the degenerate leading/trailing
                    // edge sliver cells (V ~ 1e-9, faces with S/V ~ 1e5) the
                    // face-correction leaves a large spurious tangential
                    // component that produces an enormous viscous energy flux
                    // for an otherwise uniform state and destabilizes the run.
                    // The wall shear is imposed separately by the one-sided
                    // wall_viscous_stress term (O(1/d), well-conditioned), so
                    // the gradient stencil uses the cell's own value here; the
                    // physical boundary-layer slope then enters the interior
                    // faces through the cell-to-cell differences.
                    qg = qc;
                } else if (bt == BCType::SlipWall) {
                    real_t nxn = face.normal[0], nyn = face.normal[1];
                    real_t Sn = std::sqrt(nxn * nxn + nyn * nyn);
                    if (Sn > 0) { nxn /= Sn; nyn /= Sn; }
                    real_t un = qc.u * nxn + qc.v * nyn;
                    qg.u = qc.u - 2.0 * un * nxn;
                    qg.v = qc.v - 2.0 * un * nyn;
                }
                add_sample(gcent, qg);
            }
        }
        real_t trace = mxx + myy;
        real_t det = mxx * myy - mxy * mxy;
        bool full_rank = trace > 0 && det > 1e-13 * trace * trace;
        Vec2 g[4];
        if (full_rank) {
            for (int k = 0; k < 4; k++) {
                g[k][0] = (myy * bx[k] - mxy * by[k]) / det;
                g[k][1] = (mxx * by[k] - mxy * bx[k]) / det;
            }
        } else if (strongest_d2 > 0) {
            // Singular fallback: directional derivative along the strongest
            // sample direction.
            real_t inv = 1.0 / std::sqrt(strongest_d2);
            real_t dx = sx * inv, dy = sy * inv;
            real_t denom = 0;
            real_t num[4] = {0, 0, 0, 0};
            for (idx_t fid : mesh.owned_cells[i].face_ids) {
                const auto& face = mesh.faces[fid];
                idx_t nb = (face.left_cell == i) ? face.right_cell : face.left_cell;
                Vec2 loc;
                Prims q;
                if (nb >= 0) {
                    loc = solver.local_centroids()[nb];
                    q = ws.prims[nb];
                } else {
                    loc = Vec2(2.0 * face.centroid[0] - cc[0],
                               2.0 * face.centroid[1] - cc[1]);
                    q = qc;
                    BCType bt = solver.wall_bc_of_face()[fid];
                    if (bt == BCType::Farfield) {
                        q = prims_from_conservative(ci.freestream_state, gamma, R);
                    } else if (bt == BCType::NoSlipAdiabaticWall) {
                        q = qc;
                    } else if (bt == BCType::SlipWall) {
                        real_t nxn = face.normal[0], nyn = face.normal[1];
                        real_t Sn = std::sqrt(nxn * nxn + nyn * nyn);
                        if (Sn > 0) { nxn /= Sn; nyn /= Sn; }
                        real_t un = qc.u * nxn + qc.v * nyn;
                        q.u = qc.u - 2.0 * un * nxn;
                        q.v = qc.v - 2.0 * un * nyn;
                    }
                }
                Vec2 dr(loc[0] - cc[0], loc[1] - cc[1]);
                real_t d2 = dr[0] * dr[0] + dr[1] * dr[1];
                if (!(d2 > 1e-30)) continue;
                real_t w = 1.0 / d2;
                real_t proj = dr[0] * dx + dr[1] * dy;
                denom += w * proj * proj;
                real_t vals[4] = {q.rho - qc.rho, q.u - qc.u, q.v - qc.v, q.T - qc.T};
                for (int k = 0; k < 4; k++) num[k] += w * proj * vals[k];
            }
            for (int k = 0; k < 4; k++) {
                real_t dd = (denom > 1e-30) ? num[k] / denom : 0.0;
                g[k][0] = dd * dx;
                g[k][1] = dd * dy;
            }
        } else {
            for (int k = 0; k < 4; k++) { g[k][0] = 0; g[k][1] = 0; }
        }
        ws.grads[i].grho = g[0];
        ws.grads[i].gu = g[1];
        ws.grads[i].gv = g[2];
        ws.grads[i].gT = g[3];
    }

    // Propagate owned-cell gradients to ghost cells so viscous fluxes on
    // inter-partition faces have a face-adjacent gradient estimate.  Ghost
    // cells store no usable local face list, so we average the gradients of
    // the owned cells that share a partition face with each ghost.
    std::vector<int> gcount(mesh.n_ghost, 0);
    for (idx_t f = 0; f < (idx_t)mesh.faces.size(); f++) {
        const auto& face = mesh.faces[f];
        idx_t L = face.left_cell, Rc = face.right_cell;
        if (Rc >= mesh.n_owned && Rc < mesh.n_total && L >= 0 && L < mesh.n_owned) {
            idx_t g = Rc - mesh.n_owned;
            ws.grads[Rc].grho += ws.grads[L].grho;
            ws.grads[Rc].gu += ws.grads[L].gu;
            ws.grads[Rc].gv += ws.grads[L].gv;
            ws.grads[Rc].gT += ws.grads[L].gT;
            gcount[g]++;
        }
    }
    for (idx_t g = 0; g < mesh.n_ghost; g++) {
        if (gcount[g] > 0) {
            real_t inv = 1.0 / (real_t)gcount[g];
            ws.grads[mesh.n_owned + g].grho *= inv;
            ws.grads[mesh.n_owned + g].gu *= inv;
            ws.grads[mesh.n_owned + g].gv *= inv;
            ws.grads[mesh.n_owned + g].gT *= inv;
        }
    }
}

void apply_limiter(const Solver& solver, const std::vector<StateVec>& U,
                   GradWorkspace& ws) {
    const auto& mesh = solver.mesh();
    const auto& ci = solver.case_input();
    const real_t gamma = ci.gamma, R = ci.R;
    ws.limiter.assign(mesh.n_owned, 1.0);

    // Per-cell min/max over the face-adjacent neighbors (owned + ghost),
    // including the cell itself.
    std::vector<Prims> mins(mesh.n_owned), maxs(mesh.n_owned);
    for (idx_t i = 0; i < mesh.n_owned; i++) {
        mins[i] = ws.prims[i];
        maxs[i] = ws.prims[i];
        for (idx_t fid : mesh.owned_cells[i].face_ids) {
            const auto& face = mesh.faces[fid];
            idx_t nb = (face.left_cell == i) ? face.right_cell : face.left_cell;
            Prims q;
            if (nb >= 0) {
                q = ws.prims[nb];
            } else {
                // Boundary face value in the reconstruction stencil: the
                // no-slip wall value is exactly zero velocity (mirrored ghost),
                // the farfield value is freestream, and the slip wall is the
                // specular reflection.  Including these keeps the near-wall
                // reconstruction bounded by the physical wall state.
                q = ws.prims[i];
                BCType bt = solver.wall_bc_of_face()[fid];
                if (bt == BCType::Farfield) {
                    q = prims_from_conservative(ci.freestream_state, gamma, R);
                } else if (bt == BCType::NoSlipAdiabaticWall) {
                    q.u = 0.0;
                    q.v = 0.0;
                } else if (bt == BCType::SlipWall) {
                    real_t nxn = face.normal[0], nyn = face.normal[1];
                    real_t Sn = std::sqrt(nxn * nxn + nyn * nyn);
                    if (Sn > 0) { nxn /= Sn; nyn /= Sn; }
                    real_t un = ws.prims[i].u * nxn + ws.prims[i].v * nyn;
                    q.u = ws.prims[i].u - 2.0 * un * nxn;
                    q.v = ws.prims[i].v - 2.0 * un * nyn;
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

    // Barth-Jespersen limiter on the primitive variables by default.  Setting
    // CFD_LIMITER_K selects the smooth Venkatakrishnan variant with that K
    // (used for experiments; the hard BJ bound is the production default).
    const char* lim_env = getenv("CFD_LIMITER_K");
    const bool use_venk = (lim_env != nullptr);
    real_t lim_K = lim_env ? std::atof(lim_env) : 0.0;
    for (idx_t i = 0; i < mesh.n_owned; i++) {
        const auto& g = ws.grads[i];
        const Prims& qc = ws.prims[i];
        real_t phi = 1.0;
        const Vec2* grads[4] = {&g.grho, &g.gu, &g.gv, &g.gT};
        real_t qmin[4] = {mins[i].rho, mins[i].u, mins[i].v, mins[i].T};
        real_t qmax[4] = {maxs[i].rho, maxs[i].u, maxs[i].v, maxs[i].T};
        real_t q0[4] = {qc.rho, qc.u, qc.v, qc.T};
        real_t eps2 = 0.0;
        if (use_venk) {
            // epsilon^2 = (K * h)^3 with h the local cell size (2D: sqrt(vol)).
            real_t h = std::sqrt(std::max(mesh.owned_cells[i].volume, 1e-30));
            eps2 = lim_K * lim_K * lim_K * h * h * h;
        }
        for (idx_t fid : mesh.owned_cells[i].face_ids) {
            const auto& face = mesh.faces[fid];
            Vec2 dr(face.centroid[0] - solver.local_centroids()[i][0],
                    face.centroid[1] - solver.local_centroids()[i][1]);
            for (int v = 0; v < 4; v++) {
                real_t delta = grads[v]->dot(dr);
                real_t phiv = 1.0;
                if (delta > 1e-30) {
                    real_t dm = qmax[v] - q0[v];
                    if (dm <= 1e-30) {
                        phiv = 0.0;
                    } else if (use_venk) {
                        phiv = (dm * dm + eps2 + 2.0 * dm * delta) /
                               (dm * dm + eps2 + dm * delta + delta * delta);
                    } else {
                        phiv = dm / delta;
                    }
                } else if (delta < -1e-30) {
                    real_t dm = qmin[v] - q0[v];
                    if (dm >= -1e-30) {
                        phiv = 0.0;
                    } else if (use_venk) {
                        phiv = (dm * dm + eps2 + 2.0 * dm * delta) /
                               (dm * dm + eps2 + dm * delta + delta * delta);
                    } else {
                        phiv = dm / delta;
                    }
                }
                if (phiv < 0) phiv = 0;
                if (phiv > 1) phiv = 1;
                phi = std::min(phi, phiv);
            }
        }
        // positivity safeguard
        if (phi < 0) phi = 0;
        ws.limiter[i] = phi;
    }
}

void reconstruct_face(const Solver& solver, const std::vector<StateVec>& U,
                      const GradWorkspace& ws, idx_t face_id,
                      Prims& qL, Prims& qR) {
    const auto& mesh = solver.mesh();
    const auto& face = mesh.faces[face_id];
    idx_t L = face.left_cell, Rc = face.right_cell;
    const auto& ci = solver.case_input();

    // Per-face limiter evaluation would need per-face phi; approximate with the
    // cell's limited gradient from compute_gradients + limiter cached in ws.
    // Use the cell centroid-to-face-центroid displacement.
    real_t phiL = (L < mesh.n_owned) ? ws.limiter[L] : 0.0;
    // Ghost cells have no usable local face list, so their gradient is only
    // an average of the owning rank's neighbors and cannot be limited against
    // a per-cell min/max stencil.  Reconstructing from it at full strength
    // extrapolates the (large) partition-boundary gradients unboundedly and
    // blows up the flux imbalance at degenerate trailing-edge cells on np>1.
    // Use the ghost cell value directly (first-order) on partition faces.
    real_t phiR = (Rc >= 0 && Rc < mesh.n_owned) ? ws.limiter[Rc] : 0.0;

   auto recon = [&](idx_t c, real_t phi) -> Prims {
       Prims q = ws.prims[c];
       if (getenv("CFD_FIRST_ORDER") || solver.first_order_phase()) return q;
        // First-order fallback for degenerate cells with tiny volume:
        // the O(face_area/volume) spectral radius and the anisotropic
        // gradient at the trailing-edge wake sliver cells (V ~ 1e-9) make
        // the reconstructed extrapolation unbounded, driving the cell
        // state to near-vacuum during the startup transient.  The
        // threshold is set to 1e-7 (about 1/50 of the median cell volume
        // 5.9e-6), which only affects the extreme sliver cells at the
        // leading/trailing edges and does not degrade the overall
        // second-order accuracy.
        if (solver.local_volumes()[c] < 1.0e-7) {
            return q;
        }
       const auto& g = ws.grads[c];
       Vec2 dr(face.centroid[0] - solver.local_centroids()[c][0],
               face.centroid[1] - solver.local_centroids()[c][1]);
        q.rho += phi * g.grho.dot(dr);
        q.u += phi * g.gu.dot(dr);
        q.v += phi * g.gv.dot(dr);
        q.T += phi * g.gT.dot(dr);
        // positivity fallback: if the reconstructed state is nonphysical,
        // fall back to the cell-average state
        if (q.rho <= 0 || q.T <= 0) return ws.prims[c];
        q.p = q.rho * solver.case_input().R * q.T;
        q.a = std::sqrt(ci.gamma * q.p / q.rho);
        q.e = q.p / ((ci.gamma - 1.0) * q.rho);
        return q;
    };

    qL = recon(L, phiL);
    qR = (Rc >= 0) ? recon(Rc, phiR) : qL;
}

} // namespace cfd
