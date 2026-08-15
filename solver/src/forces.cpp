// Force and moment coefficient computation (Phase 3a).
//
// Wall-face integration: dF = (p * n - tau * n) * dA with n the outward
// unit normal of the fluid domain. Pressure-only for slip walls; pressure
// plus viscous shear for no-slip walls (viscosity == 0 makes the shear part
// identically zero). Face gradients for the shear stress use the first-order
// wall-normal approximation with the no-slip ghost state (u = v = 0).

#include "forces.hpp"

#include <algorithm>
#include <cmath>

#include "boundary.hpp"
#include "fluxes.hpp"

namespace cfd {

namespace {

// Wall-normal gradient of a scalar at a no-slip wall: the no-slip value is
// enforced at the wall (the face) itself, so the one-sided derivative is
// (u_wall - u_cell) / d_cell = -u_cell / d_cell with d_cell the distance
// from the interior cell center to the face.
double wall_gradient(double value_int, double dist_to_face) {
    return (dist_to_face > 0.0) ? -value_int / dist_to_face : 0.0;
}

bool is_owned(const LocalMesh& local_mesh, cgsize_t global_cell) {
    return std::binary_search(local_mesh.owned_cells.begin(),
                              local_mesh.owned_cells.end(), global_cell);
}

}  // namespace

ForceResult compute_forces(const Mesh& mesh, const LocalMesh& local_mesh,
                           const std::vector<Vector4>& U_local,
                           const GasParams& gas,
                           const FreestreamParams& freestream,
                           const ReferenceParams& reference,
                           double viscosity, MPI_Comm comm) {
    ForceResult local;

    for (cgsize_t bf = 0; bf < mesh.n_boundary_faces; ++bf) {
        const size_t bi = static_cast<size_t>(bf);
        const BCType type = static_cast<BCType>(mesh.bface_bc_type[bi]);
        if (type != BCType::SlipWall &&
            type != BCType::NoSlipAdiabaticWall) {
            continue;  // forces integrate wall faces only
        }
        const cgsize_t gc = mesh.bface_cell[bi];
        if (!is_owned(local_mesh, gc)) continue;  // owner rank only

        const size_t li = static_cast<size_t>(local_mesh.global_to_local.at(gc));
        const PrimitiveState prim = conservative_to_primitive(U_local[li], gas);

        const double nx = mesh.bface_nx[bi];  // magnitude = area
        const double ny = mesh.bface_ny[bi];
        const double area = mesh.bface_area[bi];
        const double inv_area = (area > 0.0) ? 1.0 / area : 0.0;
        const double n_x = nx * inv_area;
        const double n_y = ny * inv_area;

        const double fx_face = mesh.bface_center_x[bi];
        const double fy_face = mesh.bface_center_y[bi];

        // Pressure contribution: p * n * dA (same for slip and no-slip).
        const double p_fx = prim.p * nx;
        const double p_fy = prim.p * ny;

        // Viscous contribution (no-slip walls only, laminar runs).
        double v_fx = 0.0, v_fy = 0.0;
        if (viscosity > 0.0 && type == BCType::NoSlipAdiabaticWall) {
            // Distance from the interior cell center to the face.
            const double gx = mesh.cell_center_x[static_cast<size_t>(gc)];
            const double gy = mesh.cell_center_y[static_cast<size_t>(gc)];
            const double dx_c = fx_face - gx;
            const double dy_c = fy_face - gy;
            const double dist = std::hypot(dx_c, dy_c);

            // First-order wall gradients: the no-slip values (u = v = 0,
            // adiabatic T_wall = T_cell) are enforced at the wall face, so
            // du/dn = -u_cell / d_cell and dT/dn = 0.
            const double du = wall_gradient(prim.u, dist);
            const double dv = wall_gradient(prim.v, dist);
            const double dT = 0.0;  // adiabatic wall: zero normal T gradient

            const double u_x = du * n_x, u_y = du * n_y;
            const double v_x = dv * n_x, v_y = dv * n_y;
            const double div = u_x + v_y;
            const double tau_xx = viscosity * (2.0 * u_x - (2.0 / 3.0) * div);
            const double tau_yy = viscosity * (2.0 * v_y - (2.0 / 3.0) * div);
            const double tau_xy = viscosity * (u_y + v_x);

            // Traction on the body: -tau * n (force of fluid on the wall).
            const double tn_x = tau_xx * n_x + tau_xy * n_y;
            const double tn_y = tau_xy * n_x + tau_yy * n_y;
            v_fx = -tn_x * area;
            v_fy = -tn_y * area;
            (void)dT;
        }

        const double fx = p_fx + v_fx;
        const double fy = p_fy + v_fy;
        local.fx += fx;
        local.fy += fy;
        local.pressure_drag += p_fx;
        local.pressure_lift += p_fy;
        local.viscous_drag += v_fx;
        local.viscous_lift += v_fy;
        // Moment about the reference center: Mz = (x - x_ref) Fy - (y - y_ref) Fx.
        local.mz += (fx_face - reference.moment_x) * fy -
                    (fy_face - reference.moment_y) * fx;
    }

    // --- Global reduction ---
    double sum[6] = {local.fx,        local.fy,        local.mz,
                     local.pressure_drag, local.pressure_lift,
                     local.viscous_drag};
    double gsum[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    MPI_Allreduce(sum, gsum, 6, MPI_DOUBLE, MPI_SUM, comm);
    double viscous_lift_global = 0.0;
    MPI_Allreduce(&local.viscous_lift, &viscous_lift_global, 1, MPI_DOUBLE,
                  MPI_SUM, comm);

    ForceResult result;
    result.fx = gsum[0];
    result.fy = gsum[1];
    result.mz = gsum[2];
    result.pressure_drag = gsum[3];
    result.pressure_lift = gsum[4];
    result.viscous_drag = gsum[5];
    result.viscous_lift = viscous_lift_global;

    // --- Coefficients ---
    const double q_inf = 0.5 * freestream.density * freestream.velocity *
                         freestream.velocity;
    const double q_area = q_inf * reference.area;
    const double q_mom = q_inf * reference.area * reference.length;
    const double alpha = freestream.alpha * 3.14159265358979323846 / 180.0;
    const double ca = std::cos(alpha), sa = std::sin(alpha);
    // Rotate the body force into freestream axes: drag along the flow,
    // lift perpendicular (positive upward). At zero incidence:
    // CD = Fx, CL = -Fy.
    const double F_drag = result.fx * ca + result.fy * sa;
    const double F_lift = -result.fx * sa + result.fy * ca;
    result.cd = (q_area > 0.0) ? F_drag / q_area : 0.0;
    result.cl = (q_area > 0.0) ? F_lift / q_area : 0.0;
    result.cmz = (q_mom > 0.0) ? result.mz / q_mom : 0.0;

    return result;
}

}  // namespace cfd
