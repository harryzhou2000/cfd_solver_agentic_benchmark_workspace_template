#include "ImplicitSolver.hpp"
#include "MpiHalo.hpp"
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
                MPI_Comm comm,
                std::vector<StateVec>& dU) {
    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;
    dU.assign(n_total, {0,0,0,0});

    // Diagonal: D_i = V_i/dt_i + spectral_radii[i]
    std::vector<double> diag(n_owned);
    for (int i = 0; i < n_owned; i++) {
        double dt_i = dt_local[i];
        if (dt_i <= 0) dt_i = 1e-10;
        diag[i] = lm.cell_vol[i] / dt_i + spectral_radii[i];
    }

    // Compute preconditioned lambda for a face between i_cell and neighbour j.
    auto face_lambda = [&](int fidx, int i_cell, int j) -> double {
        const StateVec& Uj = states[j];
        double rho_j = Uj[0], u = Uj[1]/rho_j, v = Uj[2]/rho_j;
        double p_j = pressure(Uj, gamma);
        double a_j = sound_speed(rho_j, p_j, gamma);
        double nx = lm.face_nx[fidx], ny = lm.face_ny[fidx];
        double area = lm.face_area[fidx];
        double vn_f = u*nx + v*ny;
        double a_eff = (mach_ref > 0.0)
                       ? std::max(std::abs(vn_f) + 1e-10, mach_ref * a_j)
                       : a_j;
        double rho_i = states[i_cell][0];
        double vol_face = std::max(lm.cell_vol[i_cell], lm.cell_vol[j]);
        double visc_lambda = (mu > 0.0) ? (mu / rho_i * area * area / vol_face) : 0.0;
        return 0.5 * (std::abs(vn_f) + a_eff) * area + visc_lambda;
    };

    // Forward sweep: (D + L)*dU* = -R
    // Ghost cells are always upper-triangle in local ordering (j >= n_owned > i),
    // so they never participate in the forward sweep.
    std::vector<StateVec> dU_star(n_total, {0,0,0,0});
    for (int i = 0; i < n_owned; i++) {
        StateVec rhs = {-residuals[i][0], -residuals[i][1], -residuals[i][2], -residuals[i][3]};

        for (int fidx : lm.cell_face_ids_local[i]) {
            int L = lm.face_left_local[fidx], R = lm.face_right_local[fidx];
            if (R < 0) continue;
            int j = (L == i) ? R : L;
            if (j >= n_owned) continue;  // ghost -- always upper-triangle
            if (j >= i) continue;        // upper triangle

            double lambda = face_lambda(fidx, i, j);
            for (int k=0; k<4; k++) rhs[k] += lambda * dU_star[j][k];
        }

        double inv_d = 1.0 / diag[i];
        for (int k=0; k<4; k++) dU_star[i][k] = rhs[k] * inv_d;
    }

    // Share forward-sweep result so ghost slots carry the owning rank's dU_star.
    haloExchange(dU_star, lm, comm);

    // Seed ghost slots in dU with dU_star as initial proxy for ghost upper-triangle.
    for (int j = n_owned; j < n_total; j++) dU[j] = dU_star[j];

    // Backward sweep: (D + U)*dU = D*dU* - U*dU
    // Ghost cells contribute via dU[j] which improves with each exchange pass.
    // Only owned lower-triangle neighbours (j < n_owned && j <= i) are skipped.
    auto backwardSweep = [&]() {
        for (int i = n_owned-1; i >= 0; i--) {
            StateVec rhs = dU_star[i];
            for (int fidx : lm.cell_face_ids_local[i]) {
                int L = lm.face_left_local[fidx], R = lm.face_right_local[fidx];
                if (R < 0) continue;
                int j = (L == i) ? R : L;
                if (j < n_owned && j <= i) continue;  // owned lower triangle
                double lambda = face_lambda(fidx, i, j);
                for (int k=0; k<4; k++) rhs[k] += (lambda / diag[i]) * dU[j][k];
            }
            dU[i] = rhs;
        }
    };

    // Perform multiple backward sweep passes with MPI exchange between each pass.
    // n_bsweeps = n_ranks ensures information propagates across all partition
    // boundaries in the domain (each exchange adds one rank-distance of coupling).
    // For np=1 this is exactly 1 sweep (no ghost cells, no exchange needed).
    int n_bsweeps = std::max(1, lm.n_ranks);
    for (int pass = 0; pass < n_bsweeps; pass++) {
        backwardSweep();
        if (pass < n_bsweeps - 1) {
            // Exchange updated dU so each rank's ghost slots carry the neighbor's
            // current backward-sweep result before the next pass.
            haloExchange(dU, lm, comm);
        }
    }
}
