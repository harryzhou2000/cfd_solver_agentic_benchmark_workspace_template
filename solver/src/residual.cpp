#include "solver/residual.hpp"

#include "solver/boundary.hpp"        // apply_boundary_condition
#include "solver/flux.hpp"            // rusanov_flux, roe_flux, viscous_flux_term, ...
#include "solver/limiter.hpp"         // apply_barth_jespersen_limiter
#include "solver/reconstruction.hpp"  // compute_gradients, reconstruct_face_states

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace solver {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Map every local face to its BC family type ("farfield" when untagged).
// bc_families[].face_ids are LOCAL face indices into mesh.local_faces.
std::vector<std::string> build_face_bc_types(const DistributedMesh& mesh) {
    const int num_faces = static_cast<int>(mesh.local_faces.size());
    std::vector<std::string> face_bc(num_faces, "farfield");
    for (const auto& bc : mesh.bc_families) {
        for (int bf : bc.face_ids) {
            if (bf >= 0 && bf < num_faces) face_bc[bf] = bc.bc_type;
        }
    }
    return face_bc;
}

} // namespace

ResidualNorms compute_residual(const DistributedMesh& mesh,
                               std::vector<double>& state,
                               std::vector<double>& residual,
                               const CaseConfig& config,
                               const std::string& inviscid_flux_type,
                               double dissipation_scale) {
    const double gamma = config.gas.gamma;
    const double R = config.gas.R;
    const double Pr = config.gas.prandtl;
    const bool is_laminar = (config.physics.mode == "laminar");
    const double mu = config.freestream.viscosity;

    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_ghost = static_cast<int>(mesh.ghost_cells.size());
    const int num_total = num_owned + num_ghost;
    const int num_faces = static_cast<int>(mesh.local_faces.size());

    // 1. Cell-average gradients over owned + ghost cells. The Barth-Jespersen
    //    limiter below only touches owned-cell entries, so the ghost-cell
    //    gradients remain the fresh unlimited least-squares values computed
    //    from the (already halo-exchanged) ghost state.
    std::vector<Vec2> gradients = compute_gradients(mesh, state, gamma);

    // 2. Limit the owned-cell gradients.
    apply_barth_jespersen_limiter(mesh, state, gradients, gamma);

    // 3. Piecewise-linear reconstruction of the left/right states at each
    //    face. For boundary faces reconstruct_face_states leaves face_UR
    //    zeroed; the BC implementation fills it in step 4.
    std::vector<Vec4> face_UL, face_UR;
    reconstruct_face_states(mesh, state, gradients, face_UL, face_UR);

    // 4. Apply boundary conditions to boundary faces (face_right == -1).
    const std::vector<std::string> face_bc = build_face_bc_types(mesh);
    for (int f = 0; f < num_faces; ++f) {
        if (mesh.face_right[f] < 0) {
            face_UR[f] = apply_boundary_condition(
                face_UL[f], mesh.local_faces[f].normal, face_bc[f], config);
        }
    }

    // 5. Flux assembly. residual[c] accumulates sum(F·n·A) over the faces of
    //    cell c; the time derivative is dU/dt = -residual/vol. With the face
    //    normal pointing left -> right, F·n > 0 is outflow from the left cell
    //    and inflow to the right cell, so the left cell loses state and the
    //    right cell gains it.
    residual.assign(static_cast<size_t>(num_owned) * kStateSize, 0.0);

    const double roe_delta = 0.05 * config.freestream.speed_of_sound;

    // Face-averaged gradient of conservative variable `var`: mean of the
    // left/right cell gradients (left-cell gradient only on boundary faces).
    auto face_grad = [&](int var, int left, int right) -> Vec4 {
        const Vec2& gl = gradients[left + var * num_total];
        if (right < 0) {
            return Vec4(gl.x(), gl.y(), 0.0, 0.0);
        }
        const Vec2& gr = gradients[right + var * num_total];
        return Vec4(0.5 * (gl.x() + gr.x()), 0.5 * (gl.y() + gr.y()), 0.0, 0.0);
    };

    for (int f = 0; f < num_faces; ++f) {
        const Vec2& normal = mesh.local_faces[f].normal;
        const double area = mesh.local_faces[f].area;
        const int left = mesh.face_left[f];
        const int right = mesh.face_right[f];

        // Inviscid numerical flux (F·n, outflow-positive for the left cell).
        Vec4 flux;
        if (inviscid_flux_type == "roe") {
            flux = roe_flux(face_UL[f], face_UR[f], normal, gamma, roe_delta);
        } else {
            flux = rusanov_flux(face_UL[f], face_UR[f], normal, gamma,
                                dissipation_scale);
        }

        // Viscous flux (laminar mode only, and only with a nonzero
        // viscosity).
        if (is_laminar && mu > 0.0) {
            const Vec4 U_face = 0.5 * (face_UL[f] + face_UR[f]);
            const Vec4 W_face = conservative_to_primitive(U_face, gamma);
            const double T_face = W_face(3) / (W_face(0) * R);  // T = p/(rho*R)

            // TODO: temperature gradient. A proper implementation computes
            // grad_T from the face-averaged conservative gradients:
            //   T = p/(rho*R),  p = (gamma-1)*(rhoE - 0.5*(rhou^2+rhov^2)/rho)
            // For this phase grad_T is set to zero (no heat-flux term).
            const Vec2 grad_T(0.0, 0.0);

            const Vec4 grad_rho = face_grad(0, left, right);
            const Vec4 grad_rhou = face_grad(1, left, right);
            const Vec4 grad_rhov = face_grad(2, left, right);
            const Vec4 grad_rhoE = face_grad(3, left, right);

            flux += viscous_flux_term(U_face, grad_rho, grad_rhou, grad_rhov,
                                      grad_rhoE, T_face, grad_T, normal, mu,
                                      Pr, gamma, R);
        }

        // Assemble into owned cells only; ghost-cell contributions are
        // assembled by the rank that owns them.
        if (left >= 0 && left < num_owned) {
            for (int v = 0; v < kStateSize; ++v) {
                residual[static_cast<size_t>(left) * kStateSize + v] +=
                    flux(v) * area;
            }
        }
        if (right >= 0 && right < num_owned) {
            for (int v = 0; v < kStateSize; ++v) {
                residual[static_cast<size_t>(right) * kStateSize + v] -=
                    flux(v) * area;
            }
        }
    }

    // 6. Residual norms over owned cells (computed after full assembly so
    //    every cell is counted exactly once).
    double local_l2_sum = 0.0;
    double local_linf = 0.0;
    for (int c = 0; c < num_owned; ++c) {
        double cell_norm2 = 0.0;
        for (int v = 0; v < kStateSize; ++v) {
            const double r =
                residual[static_cast<size_t>(c) * kStateSize + v];
            cell_norm2 += r * r;
            local_linf = std::max(local_linf, std::abs(r));
        }
        local_l2_sum += cell_norm2;
    }

    ResidualNorms norms;
    MPI_Allreduce(&local_l2_sum, &norms.l2, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);
    norms.l2 = std::sqrt(norms.l2);
    MPI_Allreduce(&local_linf, &norms.linf, 1, MPI_DOUBLE, MPI_MAX, mesh.comm);

    return norms;
}

ForceCoeffs compute_forces(const DistributedMesh& mesh,
                           const std::vector<double>& state,
                           const CaseConfig& config) {
    const double gamma = config.gas.gamma;
    const double rho_inf = config.freestream.rho;
    const double u_inf = config.freestream.u_inf;
    const double v_inf = config.freestream.v_inf;
    const double aoa = config.freestream.aoa_degrees * kPi / 180.0;
    const double q_inf = 0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);
    const double ref_area = config.reference.area;
    const double ref_len = config.reference.length;
    const double x_mc = config.reference.moment_center[0];
    const double y_mc = config.reference.moment_center[1];

    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_total =
        num_owned + static_cast<int>(mesh.ghost_cells.size());
    const int num_faces = static_cast<int>(mesh.local_faces.size());

    // Reconstruct wall pressures to second order, consistent with the
    // residual assembly: compute the (limited) cell gradients, then
    // extrapolate the cell-average pressure to each wall-face centroid.
    // compute_forces is a diagnostic called at output intervals (not every
    // iteration), so a fresh gradient evaluation is acceptable.
    std::vector<Vec2> gradients = compute_gradients(mesh, state, gamma);
    apply_barth_jespersen_limiter(mesh, state, gradients, gamma);

    // Local face -> BC type; only wall faces contribute.
    const std::vector<std::string> face_bc = build_face_bc_types(mesh);

    // Accumulated (mesh-frame) force components and pitching moment.
    double local_fx_p = 0.0, local_fy_p = 0.0;  // pressure
    double local_fx_v = 0.0, local_fy_v = 0.0;  // viscous
    double local_cmz = 0.0;

    for (int f = 0; f < num_faces; ++f) {
        if (mesh.face_right[f] >= 0) continue;  // interior face

        const std::string& bc_type = face_bc[f];
        if (bc_type != "slip_wall" &&
            bc_type != "no_slip_adiabatic_wall") {
            continue;
        }

        const Vec2& normal = mesh.local_faces[f].normal;
        const double area = mesh.local_faces[f].area;
        const Vec2& fc = mesh.local_faces[f].centroid;
        const int left = mesh.face_left[f];

        // Interior (left) cell average. Boundary faces always have an owned
        // interior cell (local faces touch at least one owned cell).
        Vec4 U_c;
        for (int v = 0; v < kStateSize; ++v) {
            U_c[v] = state[static_cast<size_t>(left) * kStateSize + v];
        }
        const Vec4 W_c = conservative_to_primitive(U_c, gamma);

        // Second-order face pressure: extrapolate the cell-average pressure
        // to the wall-face centroid using the limited pressure gradient.
        //   p = (gamma-1)*(rhoE - 0.5*(rhou^2+rhov^2)/rho)
        //   grad_p = (gamma-1)*(grad_rhoE - u*grad_rhou - v*grad_rhov
        //                       + 0.5*q2*grad_rho)
        const double u = W_c(1);
        const double v = W_c(2);
        const double q2 = u * u + v * v;
        const Vec2 grad_p =
            (gamma - 1.0) *
            (gradients[left + 3 * num_total] -
             u * gradients[left + 1 * num_total] -
             v * gradients[left + 2 * num_total] + 0.5 * q2 * gradients[left]);
        const Vec2& cc = (left < num_owned)
                             ? mesh.owned_cells[left].centroid
                             : mesh.ghost_cells[left - num_owned].centroid;
        double p_face = W_c(3) + grad_p.dot(fc - cc);
        if (p_face <= 0.0) p_face = W_c(3);  // positivity fallback

        // The boundary-face normal points outward (from the fluid toward the
        // body), so the fluid presses on the body with force p·n·A.
        const Vec2 p_force = p_face * normal * area;
        local_fx_p += p_force.x();
        local_fy_p += p_force.y();

        // Pitching moment about the reference moment center: (r × F)_z.
        const double rx = fc.x() - x_mc;
        const double ry = fc.y() - y_mc;
        local_cmz += rx * p_force.y() - ry * p_force.x();

        // Viscous (skin-friction) force: not computed in this phase (zero).
        // A later phase should estimate the wall shear stress at no-slip
        // adiabatic walls from the one-sided velocity gradient.
    }

    // MPI-reduce the mesh-frame components, then rotate into the freestream
    // frame (freestream is at angle `aoa` to the mesh x-axis):
    //   D = Fx*cos(aoa) + Fy*sin(aoa)
    //   L = -Fx*sin(aoa) + Fy*cos(aoa)
    ForceCoeffs forces;
    double fx_p = 0.0, fy_p = 0.0, fx_v = 0.0, fy_v = 0.0, cmz = 0.0;
    MPI_Allreduce(&local_fx_p, &fx_p, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);
    MPI_Allreduce(&local_fy_p, &fy_p, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);
    MPI_Allreduce(&local_fx_v, &fx_v, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);
    MPI_Allreduce(&local_fy_v, &fy_v, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);
    MPI_Allreduce(&local_cmz, &cmz, 1, MPI_DOUBLE, MPI_SUM, mesh.comm);

    const double ca = std::cos(aoa);
    const double sa = std::sin(aoa);
    const double drag_p = fx_p * ca + fy_p * sa;
    const double lift_p = -fx_p * sa + fy_p * ca;
    const double drag_v = fx_v * ca + fy_v * sa;
    const double lift_v = -fx_v * sa + fy_v * ca;

    const double denom = q_inf * ref_area;
    if (denom != 0.0) {
        forces.pressure_drag = drag_p / denom;
        forces.viscous_drag = drag_v / denom;
        forces.pressure_lift = lift_p / denom;
        forces.viscous_lift = lift_v / denom;
        forces.cl = forces.pressure_lift + forces.viscous_lift;
        forces.cd = forces.pressure_drag + forces.viscous_drag;
        forces.cmz = cmz / (q_inf * ref_area * ref_len);
    }

    return forces;
}

} // namespace solver
