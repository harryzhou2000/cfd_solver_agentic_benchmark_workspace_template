// FV residual assembly (Phase 3a). For each owned cell:
//   R_i = sum_faces F_n(U_i, U_j) * A_face
// Internal faces contribute with the left->right normal; boundary faces use
// the ghost state from apply_boundary_condition and the outward normal. The
// global L2 norm is reduced over ranks with MPI_Allreduce.

#include "residual.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "boundary.hpp"
#include "fluxes.hpp"
#include "mpi_utils.hpp"

namespace cfd {

namespace {

// True if `global_cell` is owned by this rank (owned list is ascending).
bool is_owned(const LocalMesh& local_mesh, cgsize_t global_cell) {
    return std::binary_search(local_mesh.owned_cells.begin(),
                              local_mesh.owned_cells.end(), global_cell);
}

// Resolves the BC type of a boundary face: the precomputed per-face enum
// wins; the case-file family map is the fallback for Unknown faces.
BCType resolve_bface_type(
    const Mesh& mesh, cgsize_t bface_index,
    const std::map<std::string, std::string>& bc_map) {
    const size_t i = static_cast<size_t>(bface_index);
    const BCType stored = static_cast<BCType>(mesh.bface_bc_type[i]);
    if (stored != BCType::Unknown) return stored;
    const auto it = bc_map.find(mesh.bface_tag[i]);
    if (it != bc_map.end()) return bc_type_from_string(it->second);
    return BCType::Unknown;
}

}  // namespace

double compute_residual(const Mesh& mesh, const LocalMesh& local_mesh,
                        const std::vector<Vector4>& U_local,
                        std::vector<Vector4>& R_local, const GasParams& gas,
                        const FreestreamParams& freestream,
                        const std::map<std::string, std::string>& bc_map,
                        double viscosity, double dissipation_scale,
                        MPI_Comm comm, ResidualStats* stats,
                        const std::vector<Gradients>* grad_local,
                        const std::vector<PrimitiveState>* prim_local) {
    const size_t n_owned = static_cast<size_t>(local_mesh.n_owned);
    R_local.assign(n_owned, Vector4{});

    // Primitive states for all local cells (derived when not provided).
    std::vector<PrimitiveState> prim_scratch;
    if (!prim_local) {
        prim_scratch.resize(U_local.size());
        for (size_t i = 0; i < U_local.size(); ++i) {
            prim_scratch[i] = conservative_to_primitive(U_local[i], gas);
        }
        prim_local = &prim_scratch;
    }
    const bool second_order =
        grad_local != nullptr && !grad_local->empty() &&
        grad_local->size() >= U_local.size();

    // --- 1. Internal faces -------------------------------------------------
    // Flux through face f with the left->right normal is added to the left
    // cell and subtracted from the right cell. A face is processed here only
    // for the sides owned by this rank; the owning rank handles the other
    // side (conservation is exact across ranks because every face is visited
    // by both owners and the flux is identical on both sides).
    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const size_t fi = static_cast<size_t>(f);
        const cgsize_t gl = mesh.face_left[fi];
        const cgsize_t gr = mesh.face_right[fi];
        const bool own_l = is_owned(local_mesh, gl);
        const bool own_r = is_owned(local_mesh, gr);
        if (!own_l && !own_r) continue;

        const auto it_l = local_mesh.global_to_local.find(gl);
        const auto it_r = local_mesh.global_to_local.find(gr);
        if (it_l == local_mesh.global_to_local.end() ||
            it_r == local_mesh.global_to_local.end()) {
            throw std::runtime_error(
                "compute_residual: face neighbor not in local map");
        }
        const size_t li = static_cast<size_t>(it_l->second);
        const size_t ri = static_cast<size_t>(it_r->second);

        // Face states: reconstructed (second order) or cell-center (first).
        PrimitiveState primL, primR;
        Vector4 UL, UR;
        if (second_order) {
            const FaceRecon fr = reconstruct_face(
                li, ri, *prim_local, *grad_local, mesh, local_mesh, gas, f);
            primL = fr.left;
            primR = fr.right;
            UL = primitive_to_conservative(primL, gas);
            UR = primitive_to_conservative(primR, gas);
        } else {
            UL = U_local[li];
            UR = U_local[ri];
            primL = (*prim_local)[li];
            primR = (*prim_local)[ri];
        }

        // Inviscid flux through the face (normal magnitude = area).
        const Vector4 F = inviscid_flux_rusanov(UL, UR, mesh.face_nx[fi],
                                                mesh.face_ny[fi], gas,
                                                dissipation_scale);
        if (own_l) R_local[li] += F;
        if (own_r) R_local[ri] -= F;

        // Viscous flux: face-averaged cell gradients (second order) or the
        // two-cell jump (first order).
        if (viscosity > 0.0) {
            Vector4 Fv;
            if (second_order) {
                const Gradients& gL = (*grad_local)[li];
                const Gradients& gR = (*grad_local)[ri];
                Fv = viscous_flux_from_gradient(
                    primL, primR, mesh.face_nx[fi], mesh.face_ny[fi],
                    0.5 * (gL.du_dx + gR.du_dx),
                    0.5 * (gL.du_dy + gR.du_dy),
                    0.5 * (gL.dv_dx + gR.dv_dx),
                    0.5 * (gL.dv_dy + gR.dv_dy),
                    0.5 * (gL.dT_dx + gR.dT_dx),
                    0.5 * (gL.dT_dy + gR.dT_dy), gas, viscosity);
            } else {
                const double dx =
                    mesh.cell_center_x[static_cast<size_t>(gr)] -
                    mesh.cell_center_x[static_cast<size_t>(gl)];
                const double dy =
                    mesh.cell_center_y[static_cast<size_t>(gr)] -
                    mesh.cell_center_y[static_cast<size_t>(gl)];
                Fv = viscous_flux(UL, UR, primL, primR, mesh.face_nx[fi],
                                  mesh.face_ny[fi], dx, dy, gas, viscosity);
            }
            if (own_l) R_local[li] += Fv;
            if (own_r) R_local[ri] -= Fv;
        }
    }

    // --- 2. Boundary faces --------------------------------------------------
    for (cgsize_t bf = 0; bf < mesh.n_boundary_faces; ++bf) {
        const size_t bi = static_cast<size_t>(bf);
        const cgsize_t gc = mesh.bface_cell[bi];
        if (!is_owned(local_mesh, gc)) continue;  // owner rank only
        const size_t li = static_cast<size_t>(local_mesh.global_to_local.at(gc));
        const Vector4& U_int = U_local[li];

        const BCType bc_type = resolve_bface_type(mesh, bf, bc_map);
        const double nx = mesh.bface_nx[bi];
        const double ny = mesh.bface_ny[bi];

        // Interior face state: reconstructed (second order, with positivity
        // fallback) or the cell-center state (first order). The ghost state
        // is built from this face state by the boundary condition.
        PrimitiveState prim_face;
        Vector4 U_face;
        if (second_order) {
            prim_face = reconstruct_boundary_state(
                li, *prim_local, *grad_local, mesh, local_mesh, gas, bf);
            U_face = primitive_to_conservative(prim_face, gas);
        } else {
            prim_face = (*prim_local)[li];
            U_face = U_int;
        }

        const BCFlux bc =
            apply_boundary_condition(U_face, nx, ny, bc_type, freestream, gas);

        const Vector4 F = inviscid_flux_rusanov(U_face, bc.U_ext, nx, ny, gas,
                                                dissipation_scale);
        R_local[li] += F;

        // Viscous boundary flux only for no-slip walls (slip walls are
        // inviscid even in laminar runs). The no-slip condition is enforced
        // at the wall itself, so the gradient uses the one-sided
        // approximation (u_wall - u_cell) / d_cell with the wall state
        // (u = v = 0, adiabatic T_wall = T_cell) and d_cell the distance
        // from the cell center to the face.
        if (viscosity > 0.0 && bc_type == BCType::NoSlipAdiabaticWall) {
            const PrimitiveState prim_int = (*prim_local)[li];
            // Wall state: zero velocity, p and T from the interior
            // (zero normal gradients), rho = p / (R * T).
            PrimitiveState prim_wall;
            prim_wall.rho = prim_int.rho;
            prim_wall.u = 0.0;
            prim_wall.v = 0.0;
            prim_wall.p = prim_int.p;
            const Vector4 U_wall = primitive_to_conservative(prim_wall, gas);
            const double gx = mesh.cell_center_x[static_cast<size_t>(gc)];
            const double gy = mesh.cell_center_y[static_cast<size_t>(gc)];
            const double dx = mesh.bface_center_x[bi] - gx;
            const double dy = mesh.bface_center_y[bi] - gy;
            const Vector4 Fv = viscous_flux(U_int, U_wall, prim_int, prim_wall,
                                            nx, ny, dx, dy, gas, viscosity);
            R_local[li] += Fv;
        }
    }

    // --- 3. Global norms -----------------------------------------------------
    double local_sum = 0.0;
    double local_linf = 0.0;
    double local_rho = 0.0, local_rhou = 0.0, local_rhov = 0.0, local_rhoE = 0.0;
    for (size_t i = 0; i < n_owned; ++i) {
        const Vector4& R = R_local[i];
        local_sum += R.r * R.r + R.u * R.u + R.v * R.v + R.e * R.e;
        local_linf = std::max(local_linf, std::max(
            {std::abs(R.r), std::abs(R.u), std::abs(R.v), std::abs(R.e)}));
        local_rho += R.r * R.r;
        local_rhou += R.u * R.u;
        local_rhov += R.v * R.v;
        local_rhoE += R.e * R.e;
    }

    const double n_global = static_cast<double>(mesh.n_cells);
    double sum[5] = {local_sum, local_rho, local_rhou, local_rhov, local_rhoE};
    double gsum[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    MPI_Allreduce(sum, gsum, 5, MPI_DOUBLE, MPI_SUM, comm);
    double linf_global = 0.0;
    MPI_Allreduce(&local_linf, &linf_global, 1, MPI_DOUBLE, MPI_MAX, comm);

    if (stats) {
        stats->l2 = std::sqrt(gsum[0] / (4.0 * n_global));
        stats->linf = linf_global;
        // location of the max residual component (owned cells only)
        {
            double local_max = -1.0;
            cgsize_t local_cell = 0;
            for (size_t i = 0; i < n_owned; ++i) {
                const Vector4& R = R_local[i];
                const double m = std::max(
                    {std::abs(R.r), std::abs(R.u), std::abs(R.v),
                     std::abs(R.e)});
                if (m > local_max) {
                    local_max = m;
                    local_cell = static_cast<cgsize_t>(i);
                }
            }
            double loc[3] = {local_max, 0.0, 0.0};
            if (local_max > 0.0 &&
                static_cast<size_t>(local_cell) <
                    local_mesh.cell_center_x_local.size()) {
                loc[1] = local_mesh.cell_center_x_local[local_cell];
                loc[2] = local_mesh.cell_center_y_local[local_cell];
            }
            double loc_global[3] = {0.0, 0.0, 0.0};
            MPI_Allreduce(loc, loc_global, 3, MPI_DOUBLE, MPI_MAX, comm);
            stats->max_residual = loc_global[0];
            stats->max_residual_x = loc_global[1];
            stats->max_residual_y = loc_global[2];
        }
        stats->rho = std::sqrt(gsum[1] / n_global);
        stats->rhou = std::sqrt(gsum[2] / n_global);
        stats->rhov = std::sqrt(gsum[3] / n_global);
        stats->rhoE = std::sqrt(gsum[4] / n_global);
    }
    return std::sqrt(gsum[0] / (4.0 * n_global));
}

double residual_l2_norm(const std::vector<Vector4>& residual,
                        cgsize_t n_cells_local) {
    double sum = 0.0;
    for (const Vector4& R : residual) {
        sum += R.r * R.r + R.u * R.u + R.v * R.v + R.e * R.e;
    }
    const double n = static_cast<double>(n_cells_local);
    return (n > 0.0) ? std::sqrt(sum / (4.0 * n)) : 0.0;
}

}  // namespace cfd
