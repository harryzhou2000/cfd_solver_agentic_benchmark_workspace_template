#include "ImplicitSolver.hpp"
#include "Physics.hpp"
#include <cmath>
#include <algorithm>

void lusgsSolve(const LocalMesh& lm,
                const CaseConfig& cfg,
                const std::vector<StateVec>& residuals,
                const std::vector<double>& spectral_radii,
                const std::vector<StateVec>& states,
                const std::vector<double>& dt_local,
                double gamma,
                double mu,
                double mach_ref,
                std::vector<StateVec>& dU) {
    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;
    dU.assign(n_total, {0,0,0,0});

    // Diagonal: D_i = V_i/dt_i + spectral_radii[i]
    // spectral_radii are already preconditioned (if mach_ref > 0) so the
    // off-diagonal lambdas below use the same preconditioned wave speed.
    std::vector<double> diag(n_owned);
    for (int i = 0; i < n_owned; i++) {
        double dt_i = dt_local[i];
        if (dt_i <= 0) dt_i = 1e-10;
        diag[i] = lm.cell_vol[i] / dt_i + spectral_radii[i];
    }

    // Helper: compute preconditioned lambda for a face between cell i_cell and neighbour j.
    // When mach_ref > 0 the off-diagonal coupling uses the same Turkel-style acoustic
    // speed that was used when computing spectral_radii, preserving diagonal dominance.
    //
    // Viscous fix: use i_cell's own density and volume for the viscous off-diagonal term,
    // consistent with how Residual.cpp computes the diagonal spectral radius contribution
    // (visc_r = mu/rho_cell * area^2 / vol_cell). Using the neighbour's volume instead
    // caused off-diagonal > diagonal on anisotropic BL meshes (catastrophic for Re<=20).
    auto face_lambda = [&](int fidx, int i_cell, int j) -> double {
        const StateVec& Uj = states[j];
        double rho_j = Uj[0], u = Uj[1]/rho_j, v = Uj[2]/rho_j;
        double p_j = pressure(Uj, gamma);
        double a_j = sound_speed(rho_j, p_j, gamma);
        double nx = lm.face_nx[fidx], ny = lm.face_ny[fidx];
        double area = lm.face_area[fidx];
        double vn_f = u*nx + v*ny;
        // Preconditioned effective acoustic speed (matches Rusanov spectral radius)
        double a_eff = (mach_ref > 0.0)
                       ? std::max(std::abs(vn_f) + 1e-10, mach_ref * a_j)
                       : a_j;
        // Use max(vol_i, vol_j) so the off-diagonal never exceeds either cell's diagonal
        // contribution. This preserves diagonal dominance symmetrically in both sweep
        // directions: forward (j<i, j may be tiny wall cell) and backward (j>i, same).
        double rho_i = states[i_cell][0];
        double vol_face = std::max(lm.cell_vol[i_cell], lm.cell_vol[j]);
        double visc_lambda = (mu > 0.0) ? (mu / rho_i * area * area / vol_face) : 0.0;
        return 0.5 * (std::abs(vn_f) + a_eff) * area + visc_lambda;
    };

    // Forward sweep: (D + L)*dU* = -R
    std::vector<StateVec> dU_star(n_owned, {0,0,0,0});
    for (int i = 0; i < n_owned; i++) {
        StateVec rhs = {-residuals[i][0], -residuals[i][1], -residuals[i][2], -residuals[i][3]};

        for (int fidx : lm.cell_face_ids_local[i]) {
            int L = lm.face_left_local[fidx], R = lm.face_right_local[fidx];
            if (R < 0) continue;  // boundary face
            int j = (L == i) ? R : L;
            if (j >= n_owned) continue;  // ghost — already captured in diagonal
            if (j >= i) continue;        // upper triangle

            double lambda = face_lambda(fidx, i, j);
            for (int k=0; k<4; k++) rhs[k] += lambda * dU_star[j][k];
        }

        double inv_d = 1.0 / diag[i];
        for (int k=0; k<4; k++) dU_star[i][k] = rhs[k] * inv_d;
    }

    // Backward sweep: (D + U)*dU = D*dU* - U*dU
    for (int i = n_owned-1; i >= 0; i--) {
        StateVec rhs = dU_star[i];

        for (int fidx : lm.cell_face_ids_local[i]) {
            int L = lm.face_left_local[fidx], R = lm.face_right_local[fidx];
            if (R < 0) continue;
            int j = (L == i) ? R : L;
            if (j >= n_owned) continue;
            if (j <= i) continue;  // lower triangle

            double lambda = face_lambda(fidx, i, j);
            for (int k=0; k<4; k++) rhs[k] += (lambda / diag[i]) * dU[j][k];
        }
        dU[i] = rhs;
    }
}
