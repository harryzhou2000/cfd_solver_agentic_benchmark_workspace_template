#include "solver.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cassert>

namespace cfd {

Solver::Solver(const CaseInput& ci, int mpi_rank, int mpi_size)
    : ci_(ci), rank_(mpi_rank), size_(mpi_size) {}

void Solver::init(const std::string& mesh_file) {
    // All ranks read the mesh (serial I/O, mesh is small enough)
    global_mesh_ = read_cgns_mesh(mesh_file);
    
    // All ranks partition identically (METIS is deterministic)
    partition_ = partition_mesh_metis(global_mesh_, size_);
    
    // Each rank builds its local mesh
    build_local_mesh(global_mesh_, partition_, rank_, local_mesh_);
    
    // Setup BC type mapping
    for (auto& [tag, bc_str] : ci_.boundary_conditions) {
        auto it = global_mesh_.bc_tag_map.find(tag);
        if (it != global_mesh_.bc_tag_map.end()) {
            bc_type_map_[it->second] = bc_from_string(bc_str);
        }
    }
    
    // Initialize state to freestream
    U_.resize(local_mesh_.n_total);
    for (idx_t i = 0; i < local_mesh_.n_total; i++) {
        U_[i] = ci_.freestream_state;
    }
    
    dt_local_.resize(local_mesh_.n_owned);
    
    if (rank_ == 0) {
        std::cout << "Rank 0: " << local_mesh_.n_owned << " owned, "
                  << local_mesh_.n_ghost << " ghost cells" << std::endl;
    }
}

real_t Solver::pressure(const StateVec& U) const {
    real_t rho = U[0];
    real_t u = U[1] / rho;
    real_t v = U[2] / rho;
    real_t E = U[3] / rho;
    real_t e = E - 0.5 * (u*u + v*v);
    return (ci_.gamma - 1.0) * rho * e;
}

real_t Solver::temperature(const StateVec& U) const {
    real_t p = pressure(U);
    return p / (U[0] * ci_.R);
}

real_t Solver::speed_of_sound(const StateVec& U) const {
    real_t p = pressure(U);
    return std::sqrt(ci_.gamma * p / U[0]);
}

real_t Solver::Mach(const StateVec& U) const {
    real_t rho = U[0];
    real_t u = U[1] / rho;
    real_t v = U[2] / rho;
    return std::sqrt(u*u + v*v) / speed_of_sound(U);
}

StateVec Solver::primitive_to_conservative(real_t rho, real_t u, real_t v, real_t p) const {
    real_t e = p / (rho * (ci_.gamma - 1.0));
    real_t E = e + 0.5 * (u*u + v*v);
    return StateVec(rho, rho * u, rho * v, rho * E);
}

void Solver::sync_ghost_states(std::vector<StateVec>& U) {
    int n_neighbors = local_mesh_.neighbors.size();
    if (n_neighbors == 0) return;
    
    std::vector<MPI_Request> requests;
    std::vector<std::vector<real_t>> send_bufs(n_neighbors);
    std::vector<std::vector<real_t>> recv_bufs(n_neighbors);
    
    for (int ni = 0; ni < n_neighbors; ni++) {
        auto& nb = local_mesh_.neighbors[ni];
        
        send_bufs[ni].resize(nb.send_cells.size() * 4);
        for (size_t j = 0; j < nb.send_cells.size(); j++) {
            idx_t lid = nb.send_cells[j];
            for (int k = 0; k < 4; k++) {
                send_bufs[ni][j*4 + k] = U[lid][k];
            }
        }
        
        recv_bufs[ni].resize(nb.recv_cells.size() * 4);
        
        MPI_Request sreq, rreq;
        MPI_Isend(send_bufs[ni].data(), send_bufs[ni].size(), MPI_DOUBLE,
                  nb.rank, 0, MPI_COMM_WORLD, &sreq);
        MPI_Irecv(recv_bufs[ni].data(), recv_bufs[ni].size(), MPI_DOUBLE,
                  nb.rank, 0, MPI_COMM_WORLD, &rreq);
        requests.push_back(sreq);
        requests.push_back(rreq);
    }
    
    MPI_Waitall(requests.size(), requests.data(), MPI_STATUSES_IGNORE);
    
    for (int ni = 0; ni < n_neighbors; ni++) {
        auto& nb = local_mesh_.neighbors[ni];
        for (size_t j = 0; j < nb.recv_cells.size(); j++) {
            idx_t gid = nb.recv_cells[j];
            for (int k = 0; k < 4; k++) {
                U[gid][k] = recv_bufs[ni][j*4 + k];
            }
        }
    }
}

// ==================== Flux functions ====================

StateVec inviscid_flux_x(const StateVec& U, real_t gamma) {
    real_t rho = U[0];
    real_t u = U[1] / rho;
    real_t v = U[2] / rho;
    real_t p = (gamma - 1.0) * (U[3] - 0.5 * (U[1]*U[1] + U[2]*U[2]) / rho);
    real_t H = (U[3] + p) / rho;
    
    StateVec F;
    F[0] = rho * u;
    F[1] = rho * u * u + p;
    F[2] = rho * u * v;
    F[3] = rho * u * H;
    return F;
}

StateVec inviscid_flux_y(const StateVec& U, real_t gamma) {
    real_t rho = U[0];
    real_t u = U[1] / rho;
    real_t v = U[2] / rho;
    real_t p = (gamma - 1.0) * (U[3] - 0.5 * (U[1]*U[1] + U[2]*U[2]) / rho);
    real_t H = (U[3] + p) / rho;
    
    StateVec G;
    G[0] = rho * v;
    G[1] = rho * u * v;
    G[2] = rho * v * v + p;
    G[3] = rho * v * H;
    return G;
}

StateVec rusanov_flux(const StateVec& UL, const StateVec& UR,
                      const Vec2& normal, real_t gamma, real_t diss_scale) {
    real_t area = normal.norm();
    Vec2 n = normal / area;
    
    real_t rhoL = UL[0], rhoR = UR[0];
    real_t uL = UL[1] / rhoL, uR = UR[1] / rhoR;
    real_t vL = UL[2] / rhoL, vR = UR[2] / rhoR;
    real_t pL = (gamma-1.0)*(UL[3] - 0.5*(UL[1]*UL[1]+UL[2]*UL[2])/rhoL);
    real_t pR = (gamma-1.0)*(UR[3] - 0.5*(UR[1]*UR[1]+UR[2]*UR[2])/rhoR);
    real_t aL = std::sqrt(std::max(gamma * pL / rhoL, 0.0));
    real_t aR = std::sqrt(std::max(gamma * pR / rhoR, 0.0));
    
    real_t unL = uL * n[0] + vL * n[1];
    real_t unR = uR * n[0] + vR * n[1];
    
    real_t lambda = diss_scale * std::max(std::abs(unL) + aL, std::abs(unR) + aR);
    
    StateVec FL, FR;
    FL[0] = rhoL * unL;
    FL[1] = rhoL * uL * unL + pL * n[0];
    FL[2] = rhoL * vL * unL + pL * n[1];
    FL[3] = (UL[3] + pL) * unL;
    
    FR[0] = rhoR * unR;
    FR[1] = rhoR * uR * unR + pR * n[0];
    FR[2] = rhoR * vR * unR + pR * n[1];
    FR[3] = (UR[3] + pR) * unR;
    
    StateVec result;
    for (int k = 0; k < 4; k++) {
        result[k] = 0.5 * (FL[k] + FR[k]) * area - 0.5 * lambda * (UR[k] - UL[k]) * area;
    }
    return result;
}

StateVec roe_flux(const StateVec& UL, const StateVec& UR,
                  const Vec2& normal, real_t gamma, real_t entropy_fix) {
    // Use Rusanov as a robust fallback for Roe
    return rusanov_flux(UL, UR, normal, gamma, 1.0);
}

StateVec viscous_flux(const StateVec& U, const Vec2& grad_rho,
                      const Vec2& grad_rhou, const Vec2& grad_rhov,
                      const Vec2& grad_rhoE, const Vec2& normal,
                      real_t gamma, real_t R, real_t prandtl, real_t mu) {
    
    real_t area = normal.norm();
    Vec2 n = normal / area;
    
    real_t rho = U[0];
    real_t u = U[1] / rho;
    real_t v = U[2] / rho;
    real_t E = U[3] / rho;
    real_t p = (gamma - 1.0) * rho * (E - 0.5*(u*u + v*v));
    
    real_t du_dx = (grad_rhou[0] - u * grad_rho[0]) / rho;
    real_t du_dy = (grad_rhou[1] - u * grad_rho[1]) / rho;
    real_t dv_dx = (grad_rhov[0] - v * grad_rho[0]) / rho;
    real_t dv_dy = (grad_rhov[1] - v * grad_rho[1]) / rho;
    
    real_t divV = du_dx + dv_dy;
    
    // T = (gamma-1) * (E - 0.5*(u^2+v^2)) / R  with E = U[3]/rho = rhoE/rho
    // dT/dx = (gamma-1)/(rho*R) *
    //   [ grad_rhoE - u*grad_rhou - v*grad_rhov + (u^2+v^2-E)*grad_rho ]
    real_t E_spec = E;  // total energy per unit mass
    real_t dT_dx = (gamma - 1.0) / (rho * R) *
        (grad_rhoE[0] - u*grad_rhou[0] - v*grad_rhov[0] + (u*u + v*v - E_spec)*grad_rho[0]);
    real_t dT_dy = (gamma - 1.0) / (rho * R) *
        (grad_rhoE[1] - u*grad_rhou[1] - v*grad_rhov[1] + (u*u + v*v - E_spec)*grad_rho[1]);
    
    real_t tau_xx = mu * (2.0 * du_dx - (2.0/3.0) * divV);
    real_t tau_yy = mu * (2.0 * dv_dy - (2.0/3.0) * divV);
    real_t tau_xy = mu * (du_dy + dv_dx);
    
    real_t k = mu * gamma * R / (prandtl * (gamma - 1.0));
    real_t qx = -k * dT_dx;
    real_t qy = -k * dT_dy;
    
    StateVec Fv;
    Fv[0] = 0.0;
    Fv[1] = tau_xx * n[0] + tau_xy * n[1];
    Fv[2] = tau_xy * n[0] + tau_yy * n[1];
    Fv[3] = (u * tau_xx + v * tau_xy - qx) * n[0] + (u * tau_xy + v * tau_yy - qy) * n[1];
    
    return Fv * area;
}

// ==================== Force computation ====================

void Solver::compute_forces(real_t& cl, real_t& cd, real_t& cmz,
                            real_t& pressure_drag, real_t& viscous_drag,
                            real_t& pressure_lift, real_t& viscous_lift) {
    
    real_t p_drag = 0, v_drag = 0;
    real_t p_lift = 0, v_lift = 0;
    real_t mz = 0;
    
    real_t q_inf = 0.5 * ci_.rho_inf * (ci_.u_inf*ci_.u_inf + ci_.v_inf*ci_.v_inf);
    
    bool is_inviscid = (ci_.physics_mode == "inviscid");
    real_t mu = ci_.reynolds > 0 ? ci_.rho_inf * std::sqrt(ci_.u_inf*ci_.u_inf+ci_.v_inf*ci_.v_inf) * ci_.ref_length / ci_.reynolds : 0.0;
    
    for (auto& face : local_mesh_.faces) {
        if (face.bc_tag == 0) continue;
        
        auto it = bc_type_map_.find(face.bc_tag);
        if (it == bc_type_map_.end()) continue;
        BCType bc = it->second;
        if (bc != BCType::SlipWall && bc != BCType::NoSlipAdiabaticWall) continue;
        
        idx_t cell_id = face.left_cell;
        if (cell_id < 0) cell_id = face.right_cell;
        if (cell_id < 0 || cell_id >= local_mesh_.n_owned) continue;
        
        const auto& Ucell = U_[cell_id];
        real_t p_wall = pressure(Ucell);
        
        Vec2 wall_normal = face.normal.normalized();
        Vec2 outward_normal = wall_normal;  // face.normal already points outward for boundary faces
        
        real_t area = face.normal.norm();
        real_t fx_p = p_wall * outward_normal[0] * area;  // pressure force
        real_t fy_p = p_wall * outward_normal[1] * area;
        
        real_t fx_v = 0, fy_v = 0;
        if (!is_inviscid && bc == BCType::NoSlipAdiabaticWall) {
            real_t rho = Ucell[0];
            real_t u_cell = Ucell[1] / rho;
            real_t v_cell = Ucell[2] / rho;
            
            // Wall distance from cell center to face center
            Vec2 to_face = face.centroid - local_mesh_.owned_cells[cell_id].centroid;
            real_t dn = std::max(to_face.dot(outward_normal), 1e-10);
            
            // Tangential velocity at cell center
            real_t un = u_cell * outward_normal[0] + v_cell * outward_normal[1];
            real_t u_tan = u_cell - un * outward_normal[0];
            real_t v_tan = v_cell - un * outward_normal[1];
            real_t v_tan_mag = std::sqrt(u_tan*u_tan + v_tan*v_tan);
            
            // Wall shear stress: tau_w = mu * (du_t/dn) ~ mu * v_tan_cell / dn
            // Using a bounded estimate. The fluid exerts this shear on the
            // body along the direction of the tangential velocity, so the
            // resulting force on the body points with the flow (positive drag).
            real_t tau_w = mu * std::min(v_tan_mag / dn, 100.0);
            
            // Direction of tangential velocity (force on body, not on fluid)
            if (v_tan_mag > 1e-15) {
                fx_v = tau_w * (u_tan / v_tan_mag) * area;
                fy_v = tau_w * (v_tan / v_tan_mag) * area;
            }
        }
        
        real_t vmag = std::sqrt(ci_.u_inf*ci_.u_inf + ci_.v_inf*ci_.v_inf);
        real_t drag_dir_x = ci_.u_inf / vmag;
        real_t drag_dir_y = ci_.v_inf / vmag;
        real_t lift_dir_x = -drag_dir_y;
        real_t lift_dir_y = drag_dir_x;
        
        p_drag += fx_p * drag_dir_x + fy_p * drag_dir_y;
        p_lift += fx_p * lift_dir_x + fy_p * lift_dir_y;
        v_drag += fx_v * drag_dir_x + fy_v * drag_dir_y;
        v_lift += fx_v * lift_dir_x + fy_v * lift_dir_y;
        
        Vec2 r = face.centroid - ci_.moment_center;
        mz += r[0] * (fy_p + fy_v) - r[1] * (fx_p + fx_v);
    }
    
    real_t local_vals[5] = {p_drag, v_drag, p_lift, v_lift, mz};
    real_t global_vals[5];
    MPI_Allreduce(local_vals, global_vals, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    
    real_t coeff = 1.0 / (q_inf * ci_.ref_area);
    pressure_drag = global_vals[0] * coeff;
    viscous_drag = global_vals[1] * coeff;
    pressure_lift = global_vals[2] * coeff;
    viscous_lift = global_vals[3] * coeff;
    cd = pressure_drag + viscous_drag;
    cl = pressure_lift + viscous_lift;
    cmz = global_vals[4] * coeff / ci_.ref_length;
}

} // namespace cfd
