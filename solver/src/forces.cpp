#include "forces.hpp"

#include <cmath>
#include <mpi.h>
#include "reconstruct.hpp"

namespace cfd {

void compute_forces_local(const LocalMesh& mesh, const std::vector<Vec4>& U,
                          const Vec4& U_inf, const GasModel& gas, const CaseConfig& cfg,
                          double mu, double k, SolverState& s, int rank, MPI_Comm comm) {
    double q_inf = 0.5 * cfg.rho_inf * cfg.vel_mag * cfg.vel_mag;
    double Lref = cfg.ref_length;
    double Aref = cfg.ref_area;
    double x0 = cfg.moment_center[0], y0 = cfg.moment_center[1];

    double pressure_drag = 0.0, pressure_lift = 0.0;
    double viscous_drag = 0.0, viscous_lift = 0.0;
    double moment = 0.0;

    // Primitive arrays and cell gradients (recomputed cheaply per output row).
    int nloc = mesh.n_owned + mesh.n_ghost;
    std::vector<double> rho(nloc), u(nloc), v(nloc), p(nloc), T(nloc);
    for (int i = 0; i < nloc; ++i) {
        Prim w = to_prim(U[i], gas);
        rho[i] = w.rho;
        u[i] = w.u;
        v[i] = w.v;
        p[i] = w.p;
        T[i] = w.T;
    }
    auto M = build_ls_matrices(mesh);
    std::vector<CellGrad> grads;
    compute_gradients(mesh, M, rho.data(), u.data(), v.data(), p.data(), grads);

    for (int b = 0; b < (int)mesh.bfaces.size(); ++b) {
        const auto& bf = mesh.bfaces[b];
        if (bf.bc != BCType::NoSlipAdiabaticWall && bf.bc != BCType::SlipWall) continue;
        int i = bf.c;
        double p_wall = p[i];
        double fx = bf.fx, fy = bf.fy;
        double nx = bf.nx, ny = bf.ny;
        double dl = bf.len;

        // Pressure force (always present)
        double dFpx = -p_wall * nx * dl;
        double dFpy = -p_wall * ny * dl;
        pressure_drag += dFpx;
        pressure_lift += dFpy;
        moment += ((fx - x0) * dFpy - (fy - y0) * dFpx);

        if (cfg.viscous && bf.bc == BCType::NoSlipAdiabaticWall) {
            // Face gradients at the no-slip wall: ghost values u_g=-u, v_g=-v,
            // T_g = T (adiabatic).  Normal component from ghost difference,
            // tangential components from the cell least-squares gradient.
            double dperp = std::max((fx - mesh.cell_cx[i]) * nx +
                                        (fy - mesh.cell_cy[i]) * ny,
                                    1e-30);
            const CellGrad& g = grads[i];
            double du_gn = (-u[i] - u[i]) / (2.0 * dperp);  // (u_g - u_i)/(2 d)
            double dv_gn = (-v[i] - v[i]) / (2.0 * dperp);
            double du_gn0 = g.du[0] * nx + g.du[1] * ny;
            double dv_gn0 = g.dv[0] * nx + g.dv[1] * ny;
            double dux_face = g.du[0] + (du_gn - du_gn0) * nx;
            double duy_face = g.du[1] + (du_gn - du_gn0) * ny;
            double dvx_face = g.dv[0] + (dv_gn - dv_gn0) * nx;
            double dvy_face = g.dv[1] + (dv_gn - dv_gn0) * ny;
            double div = dux_face + dvy_face;
            double txx = 2.0 * mu * dux_face - (2.0 / 3.0) * mu * div;
            double tyy = 2.0 * mu * dvy_face - (2.0 / 3.0) * mu * div;
            double txy = mu * (duy_face + dvx_face);
            // Traction vector t = tau . n
            double tx = txx * nx + txy * ny;
            double ty = txy * nx + tyy * ny;
            // Project onto the wall tangent t_hat = (-ny, nx): skin friction.
            double tau_t = tx * (-ny) + ty * nx;
            double dFvx = tau_t * (-ny) * dl;
            double dFvy = tau_t * nx * dl;
            viscous_drag += dFvx;
            viscous_lift += dFvy;
            moment += ((fx - x0) * dFvy - (fy - y0) * dFvx);
        }
    }

    double loc[4] = {pressure_drag, pressure_lift, viscous_drag, viscous_lift};
    double glb[4] = {0, 0, 0, 0};
    MPI_Allreduce(loc, glb, 4, MPI_DOUBLE, MPI_SUM, comm);
    double pd = glb[0], pl = glb[1], vd = glb[2], vl = glb[3];

    double loc_m[1] = {moment};
    double glb_m[1] = {0};
    MPI_Allreduce(loc_m, glb_m, 1, MPI_DOUBLE, MPI_SUM, comm);
    double moment_global = glb_m[0];

    s.pd = pd / (q_inf * Aref);
    s.pl = pl / (q_inf * Aref);
    s.vd = vd / (q_inf * Aref);
    s.vl = vl / (q_inf * Aref);
    s.cd = (pd + vd) / (q_inf * Aref);
    s.cl = (pl + vl) / (q_inf * Aref);
    s.cmz = moment_global / (q_inf * Aref * Lref);
}

}  // namespace cfd
