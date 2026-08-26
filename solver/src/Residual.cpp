#include "Residual.hpp"
#include "Physics.hpp"
#include "Reconstruction.hpp"
#include <cmath>
#include <algorithm>

void computeResidual(const ResidualContext& ctx,
                     std::vector<StateVec>& residuals,
                     std::vector<double>& spectral_radii) {
    const auto& lm = ctx.lm;
    const auto& cfg = ctx.cfg;
    const auto& states = ctx.states;
    const auto& grads = ctx.grads;
    const auto& limiters = ctx.limiters;
    const auto& prim_grads = ctx.prim_grads;
    double gamma = cfg.gas.gamma;
    double R_gas = cfg.gas.R;
    double mu = ctx.mu;
    double k_cond = ctx.k_cond;
    double diss_scale = cfg.run_control.rusanov_dissipation_scale;

    int n_owned = lm.n_owned;
    residuals.assign(n_owned, {0,0,0,0});
    spectral_radii.assign(n_owned, 0.0);

    // Get freestream state for farfield BC
    StateVec U_inf = freestream_state(cfg);

    // ---- Interior faces ----
    for (int f = 0; f < lm.n_faces_local; f++) {
        int L = lm.face_left_local[f], R = lm.face_right_local[f];
        if (R < 0) continue;  // boundary, handled separately

        double nx = lm.face_nx[f], ny = lm.face_ny[f];
        double area = lm.face_area[f];
        double fx = lm.face_cx[f], fy = lm.face_cy[f];

        // Reconstruct states at face
        StateVec UL = reconstruct(L, fx, fy, lm, states, grads, limiters);
        StateVec UR = reconstruct(R, fx, fy, lm, states, grads, limiters);

        // Positivity fix
        if (UL[0] < 1e-14) UL = states[L];
        if (UR[0] < 1e-14) UR = states[R];
        double pL = pressure(UL, gamma), pR = pressure(UR, gamma);
        if (pL < 1e-14 || pR < 1e-14) { UL = states[L]; UR = states[R]; }

        // Inviscid flux
        StateVec flux = rusanov_flux(UL, UR, nx, ny, gamma, diss_scale);

        // Viscous flux (if laminar)
        if (mu > 0.0) {
            // Average primitive gradients at face
            GradVec grad_u_f = {0.5*(prim_grads[L][0][0]+prim_grads[R][0][0]),
                                0.5*(prim_grads[L][0][1]+prim_grads[R][0][1])};
            GradVec grad_v_f = {0.5*(prim_grads[L][1][0]+prim_grads[R][1][0]),
                                0.5*(prim_grads[L][1][1]+prim_grads[R][1][1])};
            GradVec grad_T_f = {0.5*(prim_grads[L][2][0]+prim_grads[R][2][0]),
                                0.5*(prim_grads[L][2][1]+prim_grads[R][2][1])};
            // Face averaged velocity
            double rhoL = UL[0], uL = UL[1]/rhoL, vL = UL[2]/rhoL;
            double rhoR = UR[0], uR = UR[1]/rhoR, vR = UR[2]/rhoR;
            double u_f = 0.5*(uL+uR), v_f = 0.5*(vL+vR);
            StateVec vis_flux = viscous_flux_dotN(u_f, v_f, grad_u_f, grad_v_f, grad_T_f,
                                                   nx, ny, mu, k_cond);
            for (int k=0; k<4; k++) flux[k] -= vis_flux[k];  // subtract viscous
        }

        // Add to residuals (R[L] += flux*area, R[R] -= flux*area)
        for (int k=0; k<4; k++) {
            if (L < n_owned) residuals[L][k] += flux[k] * area;
            if (R < n_owned) residuals[R][k] -= flux[k] * area;
        }

        // Spectral radius estimate
        double rhoL = states[L][0], uL = states[L][1]/rhoL, vL = states[L][2]/rhoL;
        double rhoR = states[R][0], uR = states[R][1]/rhoR, vR = states[R][2]/rhoR;
        double pLc = pressure(states[L], gamma), pRc = pressure(states[R], gamma);
        double aL = sound_speed(rhoL, pLc, gamma), aR = sound_speed(rhoR, pRc, gamma);
        double vnL = std::abs(uL*nx + vL*ny) + aL;
        double vnR = std::abs(uR*nx + vR*ny) + aR;
        double smax = std::max(vnL, vnR) * area;
        // Accumulate spectral radii: sum_f lambda_f * A_f  (units m^2/s)
        if (mu > 0.0) {
            double volL = lm.cell_vol[L], volR = lm.cell_vol[R];
            double visc_rL = mu/rhoL * area * area / volL;
            double visc_rR = mu/rhoR * area * area / volR;
            if (L < n_owned) spectral_radii[L] += smax + visc_rL;
            if (R < n_owned) spectral_radii[R] += smax + visc_rR;
        } else {
            if (L < n_owned) spectral_radii[L] += smax;
            if (R < n_owned) spectral_radii[R] += smax;
        }
    }

    // ---- Boundary faces ----
    for (int bf = 0; bf < lm.n_bfaces; bf++) {
        int fid = lm.bface_face_id[bf];
        int cell = lm.face_left_local[fid];
        if (cell >= n_owned) continue;

        double nx = lm.face_nx[fid], ny = lm.face_ny[fid];
        double area = lm.face_area[fid];
        double fx = lm.face_cx[fid], fy = lm.face_cy[fid];

        StateVec U_int = reconstruct(cell, fx, fy, lm, states, grads, limiters);
        if (U_int[0] < 1e-14 || pressure(U_int,gamma) < 1e-14) U_int = states[cell];

        StateVec U_ghost;
        BcType bc = lm.bface_type[bf];
        switch (bc) {
            case BcType::Farfield:
                U_ghost = bc_farfield(U_int, nx, ny, U_inf, gamma);
                break;
            case BcType::SlipWall:
                U_ghost = bc_slip_wall(U_int, nx, ny);
                break;
           case BcType::NoSlipAdiabaticWall:
                U_ghost = bc_no_slip_adiabatic(U_int);
               break;
            default:
                U_ghost = U_int;
                break;
        }

        // Inviscid flux
        StateVec flux = rusanov_flux(U_int, U_ghost, nx, ny, gamma, diss_scale);

        // Viscous flux at no-slip wall
        if (mu > 0.0 && bc == BcType::NoSlipAdiabaticWall) {
            // At no-slip wall: velocity = 0, temperature gradient = 0
            // Use interior primitive gradients
            GradVec zero_grad = {0,0};
            GradVec grad_u_f = prim_grads[cell][0];
            GradVec grad_v_f = prim_grads[cell][1];
            // No heat flux at adiabatic wall
            StateVec vis_flux = viscous_flux_dotN(0.0, 0.0, grad_u_f, grad_v_f, zero_grad,
                                                   nx, ny, mu, 0.0);
            for (int k=0; k<4; k++) flux[k] -= vis_flux[k];
        }

        for (int k=0; k<4; k++) residuals[cell][k] += flux[k] * area;

       // Spectral radius
       double rho = states[cell][0];
       double u = states[cell][1]/rho, v = states[cell][2]/rho;
       double p = pressure(states[cell], gamma);
       double a = sound_speed(rho, p, gamma);
       double vn = std::abs(u*nx + v*ny) + a;
       spectral_radii[cell] += vn * area;
        // Viscous spectral radius for wall cells: prevents timestep violating diffusive CFL
        // (critical for high-viscosity / low-Re cases with anisotropic wall cells)
        if (mu > 0.0 && bc == BcType::NoSlipAdiabaticWall) {
            spectral_radii[cell] += mu / rho * area * area / lm.cell_vol[cell];
        }
    }
}
