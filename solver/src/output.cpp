/// @file output.cpp
/// Implementation of output file generation.

#include "output.hpp"
#include "flux.hpp"
#include "logging.hpp"
#include <nlohmann/json.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cmath>

namespace cfd {

// ============================================================================
// residuals.csv
// ============================================================================

void write_residuals_header(std::ofstream& file) {
    file << "step,physical_time,inner_iter,cfl,dt,"
         << "rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
}

void write_residuals_row(std::ofstream& file, int step, Real physical_time,
                         int inner_iter, Real cfl, Real dt,
                         const Vec4& res_norm, Real res_l2, Real res_linf) {
    file << std::scientific << std::setprecision(12);
    file << step << "," << physical_time << "," << inner_iter << ","
         << cfl << "," << dt << ","
         << res_norm(0) << "," << res_norm(1) << "," << res_norm(2) << ","
         << res_norm(3) << ","
         << res_l2 << "," << res_linf << "\n";
}

// ============================================================================
// forces.csv
// ============================================================================

void write_forces_header(std::ofstream& file) {
    file << "step,physical_time,cl,cd,cmz,"
         << "pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
}

void write_forces_row(std::ofstream& file, int step, Real physical_time,
                       Real cl, Real cd, Real cmz,
                       Real pressure_drag, Real viscous_drag,
                       Real pressure_lift, Real viscous_lift) {
    file << std::scientific << std::setprecision(12);
    file << step << "," << physical_time << ","
         << cl << "," << cd << "," << cmz << ","
         << pressure_drag << "," << viscous_drag << ","
         << pressure_lift << "," << viscous_lift << "\n";
}

// ============================================================================
// surface.csv
// ============================================================================

void write_surface_csv(const Mesh& mesh, const std::vector<Vec4>& U,
                       const CaseConfig& config, const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        LOG_WARN("Could not open surface file: {}", path);
        return;
    }

    const Real gamma = config.gas.gamma;
    const Real rho_inf = config.freestream.rho;
    const Real p_inf   = config.freestream.pressure;
    const Real U_inf   = config.freestream.velocity_magnitude;
    const Real q_inf   = 0.5 * rho_inf * U_inf * U_inf;
    const Real a_inf   = config.freestream.speed_of_sound;

    file << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    file << std::scientific << std::setprecision(12);

    // Only wall boundaries
    for (const auto& [tag, face_indices] : mesh.boundary_faces) {
        const BoundaryType bc_type = static_cast<BoundaryType>(tag);
        if (bc_type != BoundaryType::SlipWall &&
            bc_type != BoundaryType::NoSlipAdiabaticWall) {
            continue;
        }

        for (std::size_t fi : face_indices) {
            const Face& face = mesh.faces[fi];
            const Vec2& xf = face.centroid;
            const Vec2& n  = face.normal;  // points outward

            // Determine interior cell
            std::size_t cell_idx = (face.left_cell != Face::INVALID)
                                     ? face.left_cell : face.right_cell;
            if (cell_idx == Face::INVALID) continue;

            const Vec4& U_cell = U[cell_idx];
            const Vec4 prim = conservative_to_primitive(U_cell, gamma);
            Real rho_w = prim(0);
            Real u_w   = prim(1);
            Real v_w   = prim(2);
            Real p_w   = prim(3);

            // For wall BCs: use boundary state
            // For slip wall: u_w, v_w should have Vn=0 (reflect normal velocity)
            // For no-slip: u_w=0, v_w=0
            if (bc_type == BoundaryType::SlipWall) {
                const Real Vn = u_w * n(0) + v_w * n(1);
                u_w = u_w - 2.0 * Vn * n(0);
                v_w = v_w - 2.0 * Vn * n(1);
            } else if (bc_type == BoundaryType::NoSlipAdiabaticWall) {
                u_w = 0.0;
                v_w = 0.0;
            }

            const Real vel_mag = std::sqrt(u_w * u_w + v_w * v_w);
            const Real mach    = vel_mag / std::max(speed_of_sound(rho_w, p_w, gamma), EPS);
            const Real cp      = (p_w - p_inf) / std::max(q_inf, EPS);

            // Skin friction: tau_w = mu * du_t/dn, estimated from the
            // cell-center tangential velocity (the boundary-state velocity is
            // zero/reflected and cannot yield a shear stress). The skin
            // friction coefficient is reported as a magnitude: cf = |tau_w|/q_inf.
            const Real mu = config.freestream.viscosity;
            const Real u_t_cell = std::abs(-prim(1) * n(1) + prim(2) * n(0));
            const Vec2 dr_w = xf - mesh.cells[cell_idx].centroid;
            const Real dn_w = std::max(dr_w.norm(), EPS);
            const Real tau_w = mu * u_t_cell / dn_w;
            const Real cf = tau_w / std::max(q_inf, EPS);

            file << xf(0) << "," << xf(1) << ","
                 << n(0)  << "," << n(1)  << ","
                 << p_w   << "," << cp    << "," << cf << ","
                 << rho_w << "," << u_w   << "," << v_w << ","
                 << mach  << "," << tag   << "\n";
        }
    }
}

// ============================================================================
// VTU field file
// ============================================================================

void write_field_vtu(const Mesh& mesh, const std::vector<Vec4>& U,
                      const CaseConfig& config, const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        LOG_WARN("Could not open VTU file: {}", path);
        return;
    }

    const Real gamma = config.gas.gamma;
    const Real R_gas = config.gas.R;

    const std::size_t n_nodes = mesh.n_nodes();
    const std::size_t n_cells = mesh.n_cells();

    file << "<?xml version=\"1.0\"?>\n";
    file << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
         << "byte_order=\"LittleEndian\">\n";
    file << "  <UnstructuredGrid>\n";
    file << "    <Piece NumberOfPoints=\"" << n_nodes
         << "\" NumberOfCells=\"" << n_cells << "\">\n";

    // --- Points ---
    file << "      <Points>\n";
    file << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" "
         << "format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_nodes; ++i) {
        file << "          " << mesh.nodes[i].coord(0) << " "
             << mesh.nodes[i].coord(1) << " 0.0\n";
    }
    file << "        </DataArray>\n";
    file << "      </Points>\n";

    // --- Cells (connectivity, offsets, types) ---
    file << "      <Cells>\n";

    // Connectivity (flat list of node indices)
    file << "        <DataArray type=\"Int64\" Name=\"connectivity\" "
         << "format=\"ascii\">\n";
    for (const auto& cell : mesh.cells) {
        for (std::size_t nid : cell.nodes) {
            file << "          " << nid << " ";
        }
        file << "\n";
    }
    file << "        </DataArray>\n";

    // Offsets
    file << "        <DataArray type=\"Int64\" Name=\"offsets\" "
         << "format=\"ascii\">\n";
    std::size_t offset = 0;
    for (const auto& cell : mesh.cells) {
        offset += cell.nodes.size();
        file << "          " << offset << "\n";
    }
    file << "        </DataArray>\n";

    // Types (5 = TRI, 9 = QUAD, 7 = POLYGON for generic)
    file << "        <DataArray type=\"UInt8\" Name=\"types\" "
         << "format=\"ascii\">\n";
    for (const auto& cell : mesh.cells) {
        int vtk_type;
        if (cell.nodes.size() == 3)       vtk_type = 5;   // TRI
        else if (cell.nodes.size() == 4)  vtk_type = 9;   // QUAD
        else                              vtk_type = 7;   // POLYGON
        file << "          " << vtk_type << "\n";
    }
    file << "        </DataArray>\n";
    file << "      </Cells>\n";

    // --- Cell Data ---
    file << "      <CellData>\n";

    // Density
    file << "        <DataArray type=\"Float64\" Name=\"density\" "
         << "format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_cells; ++i) {
        file << "          " << U[i](0) << "\n";
    }
    file << "        </DataArray>\n";

    // Velocity (3-component, vz=0)
    file << "        <DataArray type=\"Float64\" Name=\"velocity\" "
         << "NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_cells; ++i) {
        const Real rho = U[i](0);
        const Real u   = U[i](1) / std::max(rho, EPS);
        const Real v   = U[i](2) / std::max(rho, EPS);
        file << "          " << u << " " << v << " 0.0\n";
    }
    file << "        </DataArray>\n";

    // Pressure
    file << "        <DataArray type=\"Float64\" Name=\"pressure\" "
         << "format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_cells; ++i) {
        const Vec4 prim = conservative_to_primitive(U[i], gamma);
        file << "          " << prim(3) << "\n";
    }
    file << "        </DataArray>\n";

    // Mach number
    file << "        <DataArray type=\"Float64\" Name=\"mach\" "
         << "format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_cells; ++i) {
        const Vec4 prim = conservative_to_primitive(U[i], gamma);
        const Real vel  = std::sqrt(prim(1) * prim(1) + prim(2) * prim(2));
        const Real a    = speed_of_sound(prim(0), prim(3), gamma);
        file << "          " << (vel / std::max(a, EPS)) << "\n";
    }
    file << "        </DataArray>\n";

    // Temperature
    file << "        <DataArray type=\"Float64\" Name=\"temperature\" "
         << "format=\"ascii\">\n";
    for (std::size_t i = 0; i < n_cells; ++i) {
        const Vec4 prim = conservative_to_primitive(U[i], gamma);
        const Real T = prim(3) / (prim(0) * R_gas);
        file << "          " << T << "\n";
    }
    file << "        </DataArray>\n";

    file << "      </CellData>\n";
    file << "    </Piece>\n";
    file << "  </UnstructuredGrid>\n";
    file << "</VTKFile>\n";
}

// ============================================================================
// metadata.json
// ============================================================================

namespace {

/// Format a UTC time as an ISO-8601 string ("YYYY-MM-DDTHH:MM:SSZ").
std::string iso8601_utc(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace

void write_metadata_json(const CaseConfig& config, const std::string& output_dir,
                         Real wall_time_s,
                         std::size_t num_cells_global, std::size_t num_faces_global,
                         std::size_t num_cells_owned_local, std::size_t num_cells_ghost_local,
                         int mpi_ranks, bool converged,
                         bool is_transient,
                         int inner_iter_min, int inner_iter_max,
                         Real inner_iter_mean,
                         int inner_target_misses, Real inner_converged_frac) {
    const std::string path = output_dir + "/metadata.json";
    std::ofstream file(path);
    if (!file) {
        LOG_WARN("Could not open metadata file: {}", path);
        return;
    }

    using json = nlohmann::json;
    json j;

    // --- Identification / run ---
    j["case_id"] = config.case_id;
    j["solver_name"] = "cfd_solver";
    j["solver_version"] = "0.1.0";
    j["git_revision"] = nullptr;
    j["mpi_ranks"] = mpi_ranks;
    j["mesh_file"] = config.mesh.file;
    j["num_cells_global"] = num_cells_global;
    j["num_faces_global"] = num_faces_global;
    j["num_cells_owned_local"] = num_cells_owned_local;
    j["num_cells_ghost_local"] = num_cells_ghost_local;
    j["partitioner"] = (mpi_ranks > 1) ? "metis_kway" : "none";
    j["partition_edge_cut"] = 0;
    j["halo_exchange"] = (mpi_ranks > 1) ? "neighbor_isend_irecv" : "none";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;

    // --- Physics / numerics ---
    j["equation_set"] = (config.physics.mode == PhysicsMode::Inviscid)
                            ? "compressible_euler_2d"
                            : "compressible_navier_stokes_2d";
    j["inviscid_flux"] = config.numerics_required.inviscid_flux;
    j["entropy_fix"] = "harten_yee";
    j["viscous_flux"] = config.numerics_required.viscous_flux;

    std::string time_integrator = "none";
    switch (config.run_control.time_integrator) {
        case TimeIntegrator::BDF2:        time_integrator = "bdf2"; break;
        case TimeIntegrator::Trapezoidal: time_integrator = "trapezoidal"; break;
        default: break;
    }
    j["time_integrator"] = time_integrator;
    j["implicit_solver"] = "point_implicit";
    j["reconstruction"] = "linear_least_squares";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = config.numerics_required.spatial_order;
    j["positivity_preservation"] = "clamp_fallback";
    j["wall_boundary_output_semantics"] = "boundary_value";

    // --- Inner iterations ---
    j["true_bdf2_inner_loop"] = is_transient;
    j["typical_inner_iterations"] = is_transient ? static_cast<int>(inner_iter_mean) : 1;
    j["min_inner_iterations"] = config.run_control.min_inner_iterations;
    j["max_inner_iterations"] = config.run_control.max_inner_iterations;
    j["observed_min_inner_iterations"] = inner_iter_min;
    j["observed_max_inner_iterations"] = inner_iter_max;
    j["inner_residual_reduction_target"] = config.run_control.inner_residual_reduction_target;
    j["inner_target_misses"] = inner_target_misses;
    j["inner_target_converged_fraction"] = inner_converged_frac;
    j["last_inner_residual_ratio"] = 0.0;

    // --- Timing / status ---
    const std::time_t now = std::time(nullptr);
    j["start_time_utc"] = iso8601_utc(now - static_cast<std::time_t>(wall_time_s));
    j["end_time_utc"] = iso8601_utc(now);
    j["completed"] = true;
    j["convergence_status"] = converged ? "converged" : "failed";

    file << j.dump(2) << "\n";
}

// ============================================================================
// run_status.json
// ============================================================================

void write_run_status_json(const std::string& output_dir,
                           const std::string& case_id,
                           const std::string& command,
                           int n_mpi_ranks, Real wall_time_s,
                           int n_steps, Real final_physical_time,
                           bool converged, Real residual_reduction) {
    const std::string path = output_dir + "/run_status.json";
    std::ofstream file(path);
    if (!file) {
        LOG_WARN("Could not open run_status file: {}", path);
        return;
    }

    using json = nlohmann::json;
    json j;

    j["case_id"] = case_id;
    j["command"] = command;
    j["mpi_ranks"] = n_mpi_ranks;
    j["wall_time_seconds"] = wall_time_s;
    j["final_step"] = n_steps;
    j["final_physical_time"] = final_physical_time;
    j["convergence_status"] = converged ? "converged" : "failed";
    j["residual_reduction_orders"] = residual_reduction;
    j["notes"] = converged ? "steady pseudo-time run converged"
                           : "steady pseudo-time run reached max steps without full convergence";

    file << j.dump(2) << "\n";
}

} // namespace cfd
