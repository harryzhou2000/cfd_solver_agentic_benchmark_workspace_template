#pragma once

#include <mpi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "case_input.hpp"
#include "distributed_mesh.hpp"
#include "physics.hpp"

namespace cfd {

struct RunStats {
    // Transient inner-iteration statistics (also populated for steady).
    int total_inner_iterations = 0;
    int min_inner_iterations = 0;
    int max_inner_iterations = 0;
    double mean_inner_iterations = 0.0;
    int inner_target_misses = 0;
    double inner_target_converged_fraction = 0.0;
    double last_inner_residual_ratio = 0.0;

    double residual_reduction_orders = 0.0;
    double final_residual_l2 = 0.0;
    double final_residual_linf = 0.0;
    double wall_time_seconds = 0.0;
    int final_step = 0;
    double final_physical_time = 0.0;
    std::string convergence_status = "failed";
    bool completed = false;
};

class Solver {
   public:
    Solver(CaseInput case_input, DistributedMesh mesh, MPI_Comm comm);

    // Runs the full case. Writes CSVs, final field, restart, metadata and
    // run status into `output_dir` (created if necessary).
    RunStats run(const std::string& output_dir);

   private:
    CaseInput case_;
    DistributedMesh mesh_;
    MPI_Comm comm_;
    int rank_ = 0;
    int nranks_ = 1;

    Numerics numerics_;
    std::vector<double> U_;        // n_local * 4 conservative states
    std::vector<double> grad_;     // n_local * 8 primitive gradients
    std::vector<double> limiter_;  // n_local
    std::vector<double> residual_; // n_owned * 4

    // BDF2 histories (owned cells only).
    std::vector<double> U_prev_;
    std::vector<double> U_prev2_;

    // Precomputed LSQ stencils.
    struct LsqNeighbor {
        int cell = -1;
        Vec2 d{0.0, 0.0};
        double w = 0.0;
    };
    std::vector<std::vector<LsqNeighbor>> lsq_stencil_;

    double conv_radius_ref_ = 0.0;  // freestream sound speed used in lambda_c

    void init_state();
    void compute_gradients();
    void compute_limiters();
    void assemble_residual();

    // Per-cell convective + viscous spectral radii from the current state.
    void compute_spectral_radii(std::vector<double>& lambda_c,
                                std::vector<double>& lambda_v) const;

    // One dual-time inner iteration at the given CFL. `physical_term` adds the
    // frozen BDF2 source (3U - 4U^n + U^{n-1})/(2 dt) to the total residual
    // for transient steps; for steady steps it is zero.
    void inner_iteration(double cfl, double physical_dt,
                         std::vector<double>& total_residual,
                         const std::vector<double>& grad_frozen,
                         const std::vector<double>& limiter_frozen);
    // Matrix-free GMRES variant retained as an experimental path.
    void inner_iteration_gmres(double cfl, double physical_dt,
                               std::vector<double>& total_residual,
                               const std::vector<double>& grad_frozen,
                               const std::vector<double>& limiter_frozen);

    void halo_exchange();
    void compute_residual_norms(const std::vector<double>& r, double& l2,
                                double& linf, double l2_components[4],
                                double linf_components[4]);
    ForceSum compute_forces();

    void write_csv_headers(const std::string& dir);
    void append_residual_row(const std::string& dir, int step, double time,
                             int inner_iter, double cfl, double dt,
                             const double comp_l2[4], double l2, double linf);
    void append_force_row(const std::string& dir, int step, double time,
                          const ForceSum& f);
    void write_surface_csv(const std::string& dir);
    void write_field_vtu(const std::string& dir, double time, bool is_final);
    void write_restart(const std::string& dir);
    void write_partition_diagnostics(const std::string& dir);
    void write_metadata(const std::string& dir, const RunStats& stats);
   public:
    void write_run_status(const std::string& dir, const RunStats& stats,
                          const std::string& command);
};

}  // namespace cfd
