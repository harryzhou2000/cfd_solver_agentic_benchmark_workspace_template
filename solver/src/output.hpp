#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "partition.hpp"
#include "types.hpp"

namespace cfd {

// Parameters written to metadata.json at the start of a run.
struct MetadataParams {
    std::string case_id;
    std::string description;
    std::string solver_name = "cfd_solver";
    std::string solver_version = "1.0";
    std::string git_revision;  // empty -> null in JSON
    std::string mesh_file;
    std::string mode;               // inviscid | laminar | turbulent
    double reynolds = 0.0;
    std::string viscosity_model;
    double mach = 0.0;
    double alpha = 0.0;             // deg
    double gamma = 1.4;
    double gas_R = 1.0;
    double prandtl = 0.72;
    std::string run_type;           // steady | transient
    std::string time_integrator;
    double time_step = 0.0;
    double final_time = 0.0;
    int max_steps = 0;
    int spatial_order = 2;
    std::string inviscid_flux;
    std::string limiter;  // e.g. "barth_jespersen" | "none"
    int n_ranks = 1;

    // OUTPUT_CONTRACT.md fields.
    cgsize_t num_cells_global = 0;
    cgsize_t num_faces_global = 0;
    cgsize_t num_cells_owned_local = 0;
    cgsize_t num_cells_ghost_local = 0;
    std::string partitioner = "metis_kway";
    int partition_edge_cut = 0;
    std::string halo_exchange = "neighbor_isend_irecv";
    std::string equation_set = "compressible_navier_stokes_2d";
    std::string entropy_fix;  // empty -> null
    std::string viscous_flux = "face_gradient_avg";
    std::string implicit_solver = "lusgs";
    std::string reconstruction = "least_squares_linear";
    std::string positivity_preservation = "density_pressure_threshold";
    std::string wall_boundary_output_semantics = "boundary_value";
    bool true_bdf2_inner_loop = false;
    int typical_inner_iterations = 0;
    int min_inner_iterations = 0;
    int max_inner_iterations = 0;
    int observed_min_inner_iterations = 0;
    int observed_max_inner_iterations = 0;
    double inner_residual_reduction_target = 0.0;
    int inner_target_misses = 0;
    double inner_target_converged_fraction = 1.0;
    double last_inner_residual_ratio = 0.0;
    std::string start_time_utc;
    std::string end_time_utc;
    bool completed = true;
    std::string convergence_status;
};

// One row of residuals.csv.
struct ResidualRow {
    int step = 0;
    double physical_time = 0.0;
    int inner_iter = 0;
    double cfl = 0.0;
    double dt = 0.0;
    double rho = 0.0;    // L2 norms per conservative component
    double rhou = 0.0;
    double rhov = 0.0;
    double rhoE = 0.0;
    double residual_l2 = 0.0;
    double residual_linf = 0.0;
};

// One row of forces.csv.
struct ForceRow {
    int step = 0;
    double physical_time = 0.0;
    double cl = 0.0;
    double cd = 0.0;
    double cmz = 0.0;
    double pressure_drag = 0.0;
    double viscous_drag = 0.0;
    double pressure_lift = 0.0;
    double viscous_lift = 0.0;
};

// One row of surface.csv.
struct SurfaceRow {
    double x = 0.0;
    double y = 0.0;
    double nx = 0.0;
    double ny = 0.0;
    double pressure = 0.0;
    double cp = 0.0;
    double cf = 0.0;
    double rho = 0.0;
    double u = 0.0;
    double v = 0.0;
    double mach = 0.0;
    std::string tag;
};

// run_status.json fields (OUTPUT_CONTRACT.md).
struct RunStatusParams {
    std::string case_id;
    std::string command;
    int n_ranks = 1;
    double wall_time_seconds = 0.0;
    int final_step = 0;
    double final_physical_time = 0.0;
    std::string convergence_status = "completed";
    double residual_reduction_orders = 0.0;
    std::string notes;
};

// File output writers. Contract filenames: metadata.json, residuals.csv,
// forces.csv, surface.csv, field_final.vtk, restart_final.bin,
// run_status.json, partition_diagnostics.csv.
class OutputWriter {
public:
    // Writes metadata.json summarizing the run configuration.
    static void write_metadata(const std::string& output_dir,
                               const MetadataParams& params);

    // Writes residuals.csv (appends rows; header written if file is new).
    static void write_residuals(const std::string& output_dir,
                                const std::vector<ResidualRow>& rows);

    // Writes forces.csv (appends rows; header written if file is new).
    static void write_forces(const std::string& output_dir,
                             const std::vector<ForceRow>& rows);

    // Writes surface.csv (replaces content each call).
    static void write_surface(const std::string& output_dir,
                              const std::vector<SurfaceRow>& rows);

    // Writes field_final.vtk (ASCII unstructured grid). Rank 0 writes the
    // full mesh and the gathered global field; other ranks pass empty data.
    //   cell_nodes : node ids per cell (global, from the mesh reader)
    //   U_global   : full conservative field on rank 0 (owned cells in
    //                global order); empty elsewhere
    //   cell_owner : owning rank per global cell (rank 0 only)
    static void write_field_vtk(const std::string& output_dir,
                                const Mesh& mesh,
                                const std::vector<std::vector<cgsize_t>>&
                                    cell_nodes,
                                const std::vector<Vector4>& U_global,
                                const std::vector<int>& cell_owner,
                                const GasParams& gas, int rank);

    // Writes restart_final.bin: header (magic, version, step, physical
    // time, total owned count) followed by the owned conservative states of
    // all ranks, gathered in rank order. Rank 0 performs the I/O.
    static void write_restart(const std::string& output_dir,
                              const std::vector<Vector4>& U_local,
                              const LocalMesh& local_mesh, int step,
                              double physical_time, MPI_Comm comm);

    // Writes run_status.json with the current run state.
    static void write_run_status(const std::string& output_dir,
                                 const RunStatusParams& params);

    // Writes partition_diagnostics.csv (append one row for this rank; the
    // header is written by rank 0 when the file is new).
    static void write_partition_diagnostics(
        const std::string& output_dir, int rank, int n_ranks,
        cgsize_t n_owned, cgsize_t n_ghost, cgsize_t n_boundary_faces_owned,
        const std::vector<int>& neighbor_ranks,
        const std::vector<int>& send_cells,
        const std::vector<int>& recv_cells);
};

// Gathers the owned-cell conservative states to rank 0 in global cell
// order. Returns the full field on rank 0, empty on other ranks.
std::vector<Vector4> gather_global_field(const LocalMesh& local_mesh,
                                         const std::vector<Vector4>& U_local,
                                         cgsize_t n_cells_global,
                                         MPI_Comm comm);

// Builds the surface.csv rows for all wall boundary faces. Requires the
// gathered global field on rank 0 (empty elsewhere). Returns the rows on
// rank 0, empty otherwise.
std::vector<SurfaceRow> build_surface_rows(
    const Mesh& mesh, const std::vector<Vector4>& U_global,
    const GasParams& gas, const FreestreamParams& freestream,
    double viscosity, MPI_Comm comm);

// ISO-8601 UTC timestamp of the current wall-clock time.
std::string utc_now_iso();

// Reads a restart file written by write_restart and returns this rank's
// owned conservative states (ghost cells must be filled by exchange_halo).
// `step` and `physical_time` receive the values stored in the file.
std::vector<Vector4> read_restart(const std::string& file,
                                  cgsize_t n_owned_local, int* step,
                                  double* physical_time, MPI_Comm comm);

}  // namespace cfd
