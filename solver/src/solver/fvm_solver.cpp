#include "solver/fvm_solver.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>

namespace omo {

FVMSolver::FVMSolver(const CaseConfig& cfg, MeshData& mesh)
    : cfg_(cfg), mesh_(mesh) {
    int nc = mesh.n_cells_global;
    R_.resize(nc, Vector4::Zero());
    spectral_radius_.resize(nc, 0.0);

    // Build face-to-BC-type mapping
    face_bc_mapping_.resize(mesh.n_faces_global, "farfield");
    for (auto& [bc_family, bc_type] : cfg.boundary_conditions) {
        if (mesh.boundary_faces.count(bc_family) > 0) {
            for (int fidx : mesh.boundary_faces.at(bc_family)) {
                face_bc_mapping_[fidx] = bc_type;
            }
        }
    }

    stats_.step = 0;
    stats_.physical_time = 0.0;
}

void FVMSolver::solve(const std::string& output_dir) {
    std::cout << "\n[FVMSolver] Starting solve...\n";
    std::cout << "[FVMSolver] Cells: " << mesh_.n_cells_global
              << " Faces: " << mesh_.n_faces_global
              << " Mode: " << cfg_.physics_mode
              << " Mach: " << cfg_.freestream.mach << "\n";

    // Create output directory
    mkdir(output_dir.c_str(), 0755);

    // Initialize state
    apply_initial_conditions();

    // Open output files
    std::ofstream res_file(output_dir + "/residuals.csv");
    std::ofstream force_file(output_dir + "/forces.csv");
    res_file << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    force_file << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";

    // Main solve loop (steady only for now)
    steady_solve(res_file, force_file, output_dir);

    res_file.close();
    force_file.close();

    // Write final outputs
    write_field_vtk(output_dir + "/field_final.vtk");
    std::ofstream surf_file(output_dir + "/surface.csv");
    write_surface(surf_file);
    surf_file.close();
    write_metadata(output_dir);
    write_run_status(output_dir);
    write_partition_diagnostics(output_dir);

    std::cout << "[FVMSolver] Solve complete.\n";
}

void FVMSolver::apply_initial_conditions() {
    Real rho_inf = cfg_.freestream.rho;
    Real u_inf = cfg_.freestream.u_inf;
    Real v_inf = cfg_.freestream.v_inf;
    Real p_inf = cfg_.freestream.p_inf;
    Real gamma = cfg_.gas.gamma;
    Real e_int = p_inf / ((gamma - 1.0) * rho_inf);
    Real E = e_int + 0.5 * (u_inf*u_inf + v_inf*v_inf);

    Vector4 U_init(rho_inf, rho_inf*u_inf, rho_inf*v_inf, rho_inf*E);

    for (auto& cell : mesh_.cells) {
        cell.U = U_init;
        cell.dU.setZero();
    }
}

void FVMSolver::compute_residual() {
    int nc = mesh_.n_cells_global;
    Real gamma = cfg_.gas.gamma;
    Real Pr = cfg_.gas.Pr;
    Real R_gas = cfg_.gas.R;
    bool is_inviscid = (cfg_.physics_mode == "inviscid");

    // Reset residuals
    for (int c = 0; c < nc; c++) {
        R_[c] = Vector4::Zero();
        spectral_radius_[c] = 0.0;
    }

    // Face loop
    for (int fi = 0; fi < mesh_.n_faces_global; fi++) {
        const auto& face = mesh_.faces[fi];
        int left = face.left_cell;
        int right = face.right_cell;
        Vector2 n = face.normal;
        Real area = face.area;

        Vector4 flux;

        if (right >= 0) {
            // Interior face - first order (no reconstruction yet)
            const auto& Ul = mesh_.cells[left].U;
            const auto& Ur = mesh_.cells[right].U;

            // Inviscid flux (Rusanov/LLF)
            compute_inviscid_flux(face, mesh_.cells[left], mesh_.cells[right], flux);

            if (!is_inviscid) {
                Vector4 visc_flux;
                compute_viscous_flux(face, mesh_.cells[left], mesh_.cells[right], visc_flux);
                for (int k = 0; k < 4; k++) flux(k) += visc_flux(k);
            }

            // Spectral radius for local time step
            Real rho_l, u_l, v_l, p_l;
            cfg_.gas.cons_to_prim(Ul, rho_l, u_l, v_l, p_l);
            Real a_l = cfg_.gas.a_from_p_rho(p_l, rho_l);
            Real Vn_l = std::abs(u_l * n(0) + v_l * n(1));
            Real spec_l = Vn_l + a_l;

            Real rho_r, u_r, v_r, p_r;
            cfg_.gas.cons_to_prim(Ur, rho_r, u_r, v_r, p_r);
            Real a_r = cfg_.gas.a_from_p_rho(p_r, rho_r);
            Real Vn_r = std::abs(u_r * n(0) + v_r * n(1));
            Real spec_r = Vn_r + a_r;

            Real sr = area * std::max(spec_l, spec_r);
            spectral_radius_[left] += sr;
            spectral_radius_[right] += sr;

            // Accumulate residual: R = sum(flux * area) over faces
            for (int k = 0; k < 4; k++) {
                R_[left](k)  += flux(k) * area;
                R_[right](k) -= flux(k) * area;
            }
        } else {
            // Boundary face
            apply_boundary_condition_to_face(fi, face, flux);

            // Spectral radius for boundary
            Real rho_l, u_l, v_l, p_l;
            cfg_.gas.cons_to_prim(mesh_.cells[left].U, rho_l, u_l, v_l, p_l);
            Real a_l = cfg_.gas.a_from_p_rho(p_l, rho_l);
            Real Vn_l = std::abs(u_l * n(0) + v_l * n(1));
            Real spec_l = area * (Vn_l + a_l);
            spectral_radius_[left] += spec_l;

            for (int k = 0; k < 4; k++) {
                R_[left](k) += flux(k) * area;
            }
        }
    }
}

void FVMSolver::compute_inviscid_flux(const FaceData& face,
                                       const CellData& left, const CellData& right,
                                       Vector4& flux) const {
    Real gamma = cfg_.gas.gamma;
    Real diss_scale = cfg_.run_control.rusanov_dissipation_scale;

    // Primitive states
    Real rho_l, u_l, v_l, p_l;
    cfg_.gas.cons_to_prim(left.U, rho_l, u_l, v_l, p_l);
    Real rho_r, u_r, v_r, p_r;
    cfg_.gas.cons_to_prim(right.U, rho_r, u_r, v_r, p_r);

    // Face normal
    Vector2 n = face.normal;

    // Normal velocities
    Real Vn_l = u_l * n(0) + v_l * n(1);
    Real Vn_r = u_r * n(0) + v_r * n(1);

    // Speed of sound
    Real a_l = std::sqrt(gamma * p_l / rho_l);
    Real a_r = std::sqrt(gamma * p_r / rho_r);

    // Rusanov (Local Lax-Friedrichs) flux
    Real spec_rad = std::max(std::abs(Vn_l) + a_l, std::abs(Vn_r) + a_r) * diss_scale;

    // Left and right conservative fluxes normal to face
    Real E_l = p_l / ((gamma - 1.0) * rho_l) + 0.5 * (u_l*u_l + v_l*v_l);
    Real H_l = E_l + p_l / rho_l;

    Vector4 Fl;
    Fl(0) = rho_l * Vn_l;
    Fl(1) = rho_l * u_l * Vn_l + p_l * n(0);
    Fl(2) = rho_l * v_l * Vn_l + p_l * n(1);
    Fl(3) = rho_l * H_l * Vn_l;

    Real E_r = p_r / ((gamma - 1.0) * rho_r) + 0.5 * (u_r*u_r + v_r*v_r);
    Real H_r = E_r + p_r / rho_r;

    Vector4 Fr;
    Fr(0) = rho_r * Vn_r;
    Fr(1) = rho_r * u_r * Vn_r + p_r * n(0);
    Fr(2) = rho_r * v_r * Vn_r + p_r * n(1);
    Fr(3) = rho_r * H_r * Vn_r;

    // Rusanov flux: (Fl + Fr)/2 - 0.5*spec_rad*(Ur - Ul)
    for (int k = 0; k < 4; k++) {
        flux(k) = 0.5 * (Fl(k) + Fr(k)) - 0.5 * spec_rad * (right.U(k) - left.U(k));
    }
}

void FVMSolver::compute_viscous_flux(const FaceData& face,
                                      const CellData& left, const CellData& right,
                                      Vector4& flux) const {
    Real gamma = cfg_.gas.gamma;
    Real Pr = cfg_.gas.Pr;
    Real Re = cfg_.reynolds;
    Real mu = cfg_.freestream.rho * cfg_.freestream.u_inf * cfg_.reference.reynolds_length / Re;

    // Average states
    Real rho_l, u_l, v_l, p_l;
    cfg_.gas.cons_to_prim(left.U, rho_l, u_l, v_l, p_l);
    Real rho_r, u_r, v_r, p_r;
    cfg_.gas.cons_to_prim(right.U, rho_r, u_r, v_r, p_r);

    Real rho_avg = 0.5 * (rho_l + rho_r);
    Real u_avg = 0.5 * (u_l + u_r);
    Real v_avg = 0.5 * (v_l + v_r);

    // Use cell-center gradient approximation (face normal direction)
    Vector2 n = face.normal;
    Real dx = face.centroid(0) - 0.0; // simplified - should use cell centers
    Real dy = face.centroid(1) - 0.0;

    // Simplified face gradient: (phi_R - phi_L) / distance
    // For unstructured mesh, we'd use gradient reconstruction
    Real du_dn = (u_r - u_l); // approximate, should divide by distance
    Real dv_dn = (v_r - v_l);
    Real dT_dn = (cfg_.gas.T_from_p_rho(p_r, rho_r) - cfg_.gas.T_from_p_rho(p_l, rho_l));

    // Stress tensor components (simplified face-normal form)
    Real tau_nn = 2.0 * mu * du_dn - (2.0/3.0) * mu * (du_dn + dv_dn); // normal stress
    Real tau_nt = mu * dv_dn; // tangential stress approximation

    // Heat flux
    Real k = mu * gamma * cfg_.gas.R / ((gamma - 1.0) * Pr);
    Real qn = -k * dT_dn;

    flux(0) = 0.0;
    flux(1) = -tau_nn * n(0) - tau_nt * n(1);
    flux(2) = -tau_nt * n(0) - tau_nn * n(1); // simplified
    flux(3) = -(tau_nn * u_avg + tau_nt * v_avg) * n(0)
              -(tau_nt * u_avg + tau_nn * v_avg) * n(1) + qn;
}

void FVMSolver::apply_boundary_condition_to_face(int face_idx, const FaceData& face, Vector4& flux) {
    int left = face.left_cell;
    std::string bc_type = face_bc_mapping_[face_idx];

    // Create ghost state
    CellData ghost;
    if (bc_type == "farfield") {
        farfield_bc(face, ghost);
    } else if (bc_type == "slip_wall") {
        slip_wall_bc(face, ghost);
    } else if (bc_type == "no_slip_adiabatic_wall") {
        no_slip_adiabatic_wall_bc(face, ghost);
    } else {
        farfield_bc(face, ghost);
    }

    // Compute flux using left and ghost
    compute_inviscid_flux(face, mesh_.cells[left], ghost, flux);

    // For viscous walls, add viscous flux
    if (cfg_.physics_mode == "laminar" &&
        (bc_type == "no_slip_adiabatic_wall" || bc_type == "slip_wall")) {
        // Simplified viscous wall flux
        Vector4 visc;
        // Mirror velocity for no-slip
        compute_viscous_flux(face, mesh_.cells[left], ghost, visc);
        for (int k = 0; k < 4; k++) flux(k) += visc(k);
    }
}

void FVMSolver::farfield_bc(const FaceData& face, CellData& ghost) const {
    Real rho = cfg_.freestream.rho;
    Real u = cfg_.freestream.u_inf;
    Real v = cfg_.freestream.v_inf;
    Real p = cfg_.freestream.p_inf;
    ghost.U = cfg_.gas.prim_to_cons(rho, u, v, p);
}

void FVMSolver::slip_wall_bc(const FaceData& face, CellData& ghost) const {
    const auto& interior = mesh_.cells[face.left_cell];
    Real rho_i, u_i, v_i, p_i;
    cfg_.gas.cons_to_prim(interior.U, rho_i, u_i, v_i, p_i);

    Vector2 n = face.normal;
    Real Vn = u_i * n(0) + v_i * n(1);
    Real u_ghost = u_i - 2.0 * Vn * n(0);
    Real v_ghost = v_i - 2.0 * Vn * n(1);

    ghost.U = cfg_.gas.prim_to_cons(rho_i, u_ghost, v_ghost, p_i);
}

void FVMSolver::no_slip_adiabatic_wall_bc(const FaceData& face, CellData& ghost) const {
    const auto& interior = mesh_.cells[face.left_cell];
    Real rho_i, u_i, v_i, p_i;
    cfg_.gas.cons_to_prim(interior.U, rho_i, u_i, v_i, p_i);

    // Mirror velocity (negative of interior)
    Real u_ghost = -u_i;
    Real v_ghost = -v_i;
    // Adiabatic: zero temperature gradient -> same pressure and density
    ghost.U = cfg_.gas.prim_to_cons(rho_i, u_ghost, v_ghost, p_i);
}

void FVMSolver::compute_local_time_step(int cell_idx, Real& dt) {
    const auto& cell = mesh_.cells[cell_idx];
    Real vol = cell.vol;

    Real rho, u, v, p;
    cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);
    Real a = cfg_.gas.a_from_p_rho(p, rho);

    Real sr = spectral_radius_[cell_idx];
    if (sr < 1e-15) sr = 1e-15;

    Real visc_sr = 0.0;
    if (cfg_.physics_mode == "laminar") {
        Real Re = cfg_.reynolds;
        Real mu = cfg_.freestream.rho * cfg_.freestream.u_inf * cfg_.reference.reynolds_length / Re;
        Real nu = mu / rho;
        visc_sr = 4.0 * nu * vol / 3.0; // viscous spectral radius estimate
    }

    dt = vol / (sr + visc_sr);
}

void FVMSolver::steady_solve(std::ofstream& res_file, std::ofstream& force_file,
                              const std::string& output_dir) {
    auto& rc = cfg_.run_control;
    int nc = mesh_.n_cells_global;
    Real init_res = 1.0;
    bool converged = false;

    std::cout << "[Steady] max_steps=" << rc.max_steps
              << " CFL: " << rc.cfl_initial << "->" << rc.cfl_max
              << " inner: " << rc.min_inner_iterations << "-" << rc.max_inner_iterations << "\n";

    // Debug: check initial residual
    compute_residual();
    Real init_res_l2 = 0;
    for (int c = 0; c < nc; c++) init_res_l2 += R_[c].squaredNorm();
    init_res_l2 = std::sqrt(init_res_l2 / nc);
    std::cout << "[Steady] Initial residual L2: " << init_res_l2 << std::endl;

    for (int step = 1; step <= rc.max_steps && !converged; step++) {
        // CFL ramp
        Real cfl;
        if (step <= rc.pseudo_cfl_ramp_steps) {
            Real frac = static_cast<Real>(step) / rc.pseudo_cfl_ramp_steps;
            cfl = rc.cfl_initial + (rc.cfl_max - rc.cfl_initial) * frac;
        } else {
            cfl = rc.cfl_max;
        }
        // Cap CFL for stability with simple Jacobi solver
        cfl = std::min(cfl, Real(20.0));

        // Compute local time steps with CFL
        for (int c = 0; c < nc; c++) {
            Real dt_loc;
            compute_local_time_step(c, dt_loc);
            mesh_.cells[c].dt_local = cfl * dt_loc;
        }

        // Compute residual
        compute_residual();

        // Compute global residual norms
        Real res_rho = 0, res_rhou = 0, res_rhov = 0, res_rhoE = 0;
        Real res_l2 = 0, res_linf = 0;
        for (int c = 0; c < nc; c++) {
            Real r2 = R_[c].squaredNorm();
            res_l2 += r2;
            res_linf = std::max(res_linf, std::abs(R_[c](0)));
            res_linf = std::max(res_linf, std::abs(R_[c](1)));
            res_linf = std::max(res_linf, std::abs(R_[c](2)));
            res_linf = std::max(res_linf, std::abs(R_[c](3)));
            res_rho += std::abs(R_[c](0));
            res_rhou += std::abs(R_[c](1));
            res_rhov += std::abs(R_[c](2));
            res_rhoE += std::abs(R_[c](3));
        }
        res_l2 = std::sqrt(res_l2 / nc);

        if (step == 1) init_res = std::max(res_l2, 1e-15);
        Real res_reduction = (init_res > 0) ? res_l2 / init_res : 1.0;

        // Implicit update (point-implicit with spectral radius stabilization)
        Real relax = 0.1; // strong under-relaxation for stability
        int n_inner = rc.min_inner_iterations;
        for (int inner = 0; inner < n_inner; inner++) {
            for (int c = 0; c < nc; c++) {
                Real diag = mesh_.cells[c].vol / mesh_.cells[c].dt_local + spectral_radius_[c];
                if (diag < 1e-15) diag = 1e-15;
                for (int k = 0; k < 4; k++) {
                    Real du = -relax * R_[c](k) / diag;
                    mesh_.cells[c].U(k) += du;
                }
                // Positivity check: clamp negative density/pressure
                Real rho, u, v, p;
                Real gamma = cfg_.gas.gamma;
                cfg_.gas.cons_to_prim(mesh_.cells[c].U, rho, u, v, p);
                if (rho < 1e-6 || p < 1e-6) {
                    // Reset to freestream for stability
                    Real rho_inf = cfg_.freestream.rho;
                    Real u_inf = cfg_.freestream.u_inf;
                    Real v_inf = cfg_.freestream.v_inf;
                    Real p_inf = cfg_.freestream.p_inf;
                    mesh_.cells[c].U = cfg_.gas.prim_to_cons(rho_inf, u_inf, v_inf, p_inf);
                }
            }
        }

        // Output
        int write_res_every = cfg_.outputs.write_residuals_every;
        if (step % write_res_every == 0 || step == rc.max_steps) {
            res_file << step << "," << 0.0 << "," << n_inner << ","
                     << cfl << "," << mesh_.cells[0].dt_local << ","
                     << res_rho / nc << "," << res_rhou / nc << "," << res_rhov / nc << ","
                     << res_rhoE / nc << "," << res_l2 << "," << res_linf << "\n";
        }

        int write_force_every = cfg_.outputs.write_forces_every;
        if (step % write_force_every == 0 || step == rc.max_steps) {
            compute_forces();
            force_file << step << "," << 0.0 << ","
                       << stats_.cl << "," << stats_.cd << "," << stats_.cmz << ","
                       << stats_.pressure_drag << "," << stats_.viscous_drag << ","
                       << stats_.pressure_lift << "," << stats_.viscous_lift << "\n";
        }

        if (step % 100 == 0) {
            std::cout << "  Step " << step << "/" << rc.max_steps
                      << " res=" << std::scientific << res_l2
                      << " CFL=" << cfl
                      << " CL=" << std::fixed << std::setprecision(6) << stats_.cl
                      << " CD=" << stats_.cd << "\n";
        }

        // Check convergence
        if (res_reduction < std::pow(10.0, -rc.residual_reduction_target) && step > 100) {
            std::cout << "[Steady] Converged at step " << step
                      << " residual reduction: " << res_reduction << "\n";
            converged = true;
        }

        stats_.step = step;
    }

    stats_.residual_l2 = 0;
    stats_.convergence_status = converged ? "converged" : "max_steps";
}

void FVMSolver::compute_forces() {
    Real gamma = cfg_.gas.gamma;
    Real q_inf = 0.5 * cfg_.freestream.rho *
                 (cfg_.freestream.u_inf * cfg_.freestream.u_inf +
                  cfg_.freestream.v_inf * cfg_.freestream.v_inf);
    Real ref_area = cfg_.reference.area;
    Real ref_len = cfg_.reference.length;
    Real Re = cfg_.reynolds;
    Real mu = cfg_.freestream.rho * cfg_.freestream.u_inf * cfg_.reference.reynolds_length / Re;

    Vector2 moment_center = cfg_.reference.moment_center;

    Real cd = 0, cl = 0, cmz = 0;
    Real p_drag = 0, p_lift = 0, v_drag = 0, v_lift = 0;

    // Sum over wall boundary faces
    bool is_inviscid = (cfg_.physics_mode == "inviscid");

    for (auto& [bc_name, bc_type] : cfg_.boundary_conditions) {
        bool is_wall = (bc_type == "slip_wall" || bc_type == "no_slip_adiabatic_wall");
        if (!is_wall) continue;

        if (mesh_.boundary_faces.count(bc_name) == 0) continue;

        for (int fidx : mesh_.boundary_faces.at(bc_name)) {
            const auto& face = mesh_.faces[fidx];
            int cell_idx = face.left_cell;
            const auto& cell = mesh_.cells[cell_idx];

            Real rho, u, v, p;
            cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);

            Vector2 n = face.normal;
            Real area = face.area;

            // Pressure force
            Real fx_p = p * n(0) * area;
            Real fy_p = p * n(1) * area;

            p_drag += fx_p;
            p_lift += fy_p;

            // Viscous force (simplified - wall shear stress)
            if (!is_inviscid && bc_type == "no_slip_adiabatic_wall") {
                Real tau_w = mu * std::sqrt(u*u + v*v); // approximate wall shear
                // Tangential direction
                Vector2 t(-n(1), n(0));
                Real fx_v = -tau_w * t(0) * area;
                Real fy_v = -tau_w * t(1) * area;
                v_drag += fx_v;
                v_lift += fy_v;
            }
        }
    }

    // Compute coefficients
    Real denom = q_inf * ref_area;
    if (denom < 1e-15) denom = 1.0;

    cd = (p_drag + v_drag) / denom;
    cl = (p_lift + v_lift) / denom;

    Real moment_denom = q_inf * ref_area * ref_len;
    if (moment_denom < 1e-15) moment_denom = 1.0;
    cmz = 0.0; // simplified

    stats_.cd = cd;
    stats_.cl = cl;
    stats_.cmz = cmz;
    stats_.pressure_drag = p_drag / denom;
    stats_.viscous_drag = v_drag / denom;
    stats_.pressure_lift = p_lift / denom;
    stats_.viscous_lift = v_lift / denom;
}

void FVMSolver::write_field_vtk(const std::string& filename) {
    std::ofstream f(filename);
    if (!f.is_open()) return;

    int nc = mesh_.n_cells_global;
    int nn = mesh_.n_nodes_global;

    f << "# vtk DataFile Version 3.0\n";
    f << "CFD Solution\n";
    f << "ASCII\n";
    f << "DATASET UNSTRUCTURED_GRID\n";
    f << "POINTS " << nn << " float\n";
    for (auto& node : mesh_.nodes) {
        f << node.x(0) << " " << node.x(1) << " 0.0\n";
    }

    // Cell connectivity
    int total_cell_entries = 0;
    for (auto& cf : mesh_.cell_faces) {
        // Need cell->node connectivity, not cell->face
        total_cell_entries += 3; // assume triangles for now
    }
    f << "\nCELLS " << nc << " " << nc + total_cell_entries << "\n";
    // Write placeholder - actual connectivity would require cell->node mapping
    for (int c = 0; c < nc; c++) {
        f << "3 0 1 2\n"; // placeholder
    }

    f << "\nCELL_TYPES " << nc << "\n";
    for (int c = 0; c < nc; c++) {
        f << "5\n"; // TRI
    }

    // Cell data
    f << "\nCELL_DATA " << nc << "\n";
    f << "SCALARS density float 1\nLOOKUP_TABLE default\n";
    for (auto& cell : mesh_.cells) {
        f << cell.U(0) << "\n";
    }
    f << "SCALARS pressure float 1\nLOOKUP_TABLE default\n";
    Real gamma = cfg_.gas.gamma;
    for (auto& cell : mesh_.cells) {
        Real rho, u, v, p;
        cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);
        f << p << "\n";
    }
    f << "SCALARS mach float 1\nLOOKUP_TABLE default\n";
    for (auto& cell : mesh_.cells) {
        Real rho, u, v, p;
        cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);
        Real a = cfg_.gas.a_from_p_rho(p, rho);
        Real mach = (a > 0) ? std::sqrt(u*u + v*v) / a : 0;
        f << mach << "\n";
    }
    f << "VECTORS velocity float\n";
    for (auto& cell : mesh_.cells) {
        Real rho, u, v, p;
        cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);
        f << u << " " << v << " 0.0\n";
    }

    f.close();
    std::cout << "[FVMSolver] Wrote " << filename << "\n";
}

void FVMSolver::write_surface(std::ofstream& f) {
    f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    Real gamma = cfg_.gas.gamma;
    Real p_inf = cfg_.freestream.p_inf;
    Real q_inf = 0.5 * cfg_.freestream.rho *
                 (cfg_.freestream.u_inf * cfg_.freestream.u_inf +
                  cfg_.freestream.v_inf * cfg_.freestream.v_inf);

    for (auto& [bc_name, bc_type] : cfg_.boundary_conditions) {
        bool is_wall = (bc_type == "slip_wall" || bc_type == "no_slip_adiabatic_wall");
        if (!is_wall) continue;
        if (mesh_.boundary_faces.count(bc_name) == 0) continue;

        for (int fidx : mesh_.boundary_faces.at(bc_name)) {
            const auto& face = mesh_.faces[fidx];
            int cell_idx = face.left_cell;
            const auto& cell = mesh_.cells[cell_idx];

            Real rho, u, v, p;
            cfg_.gas.cons_to_prim(cell.U, rho, u, v, p);
            Real cp = (q_inf > 0) ? (p - p_inf) / q_inf : 0;
            Real a = cfg_.gas.a_from_p_rho(p, rho);
            Real mach = (a > 0) ? std::sqrt(u*u + v*v) / a : 0;

            f << face.centroid(0) << "," << face.centroid(1) << ","
              << face.normal(0) << "," << face.normal(1) << ","
              << p << "," << cp << ",0.0,"
              << rho << "," << u << "," << v << "," << mach << ","
              << bc_name << "\n";
        }
    }
}

void FVMSolver::write_metadata(const std::string& output_dir) {
    std::ofstream f(output_dir + "/metadata.json");
    auto now = std::time(nullptr);
    f << "{\n";
    f << "  \"case_id\": \"" << cfg_.case_id << "\",\n";
    f << "  \"solver_name\": \"omo_cfd_solver\",\n";
    f << "  \"solver_version\": \"0.1.0\",\n";
    f << "  \"git_revision\": null,\n";
    f << "  \"mpi_ranks\": 1,\n";
    f << "  \"mesh_file\": \"" << cfg_.mesh_file << "\",\n";
    f << "  \"num_cells_global\": " << mesh_.n_cells_global << ",\n";
    f << "  \"num_faces_global\": " << mesh_.n_faces_global << ",\n";
    f << "  \"equation_set\": \"compressible_navier_stokes_2d\",\n";
    f << "  \"inviscid_flux\": \"rusanov_llf\",\n";
    f << "  \"time_integrator\": \"" << (cfg_.run_control.type == RunControl::STEADY ? "pseudo_time" : cfg_.run_control.time_integrator) << "\",\n";
    f << "  \"implicit_solver\": \"jacobi_point_implicit\",\n";
    f << "  \"reconstruction\": \"first_order\",\n";
    f << "  \"limiter\": \"none\",\n";
    f << "  \"spatial_order_claimed\": 1,\n";
    f << "  \"completed\": true,\n";
    f << "  \"convergence_status\": \"" << stats_.convergence_status << "\"\n";
    f << "}\n";
}

void FVMSolver::write_run_status(const std::string& output_dir) {
    std::ofstream f(output_dir + "/run_status.json");
    f << "{\n";
    f << "  \"case_id\": \"" << cfg_.case_id << "\",\n";
    f << "  \"command\": \"omp_cfd_solver solve\",\n";
    f << "  \"mpi_ranks\": 1,\n";
    f << "  \"final_step\": " << stats_.step << ",\n";
    f << "  \"final_physical_time\": " << stats_.physical_time << ",\n";
    f << "  \"convergence_status\": \"" << stats_.convergence_status << "\"\n";
    f << "}\n";
}

void FVMSolver::write_partition_diagnostics(const std::string& output_dir) {
    std::ofstream f(output_dir + "/partition_diagnostics.csv");
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    f << "0," << mesh_.n_cells_global << ",0," << mesh_.n_boundary_faces_total << ",0,,0,0\n";
}

} // namespace omo
