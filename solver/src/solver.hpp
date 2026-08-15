#pragma once

#include <mpi.h>

#include <map>
#include <string>
#include <vector>

#include "config.hpp"
#include "forces.hpp"
#include "gas.hpp"
#include "mpi_utils.hpp"
#include "partition.hpp"
#include "reconstruction.hpp"
#include "residual.hpp"
#include "types.hpp"

namespace cfd {

// Initializes a local (owned + ghost) field with the freestream state
// everywhere.
void initialize_freestream_field(std::vector<Vector4>& U_local,
                                 cgsize_t n_owned, cgsize_t n_ghost,
                                 const FreestreamParams& freestream,
                                 const GasParams& gas);

// Phase 3a smoke test: fills U_local with the freestream state, performs
// one halo exchange, assembles one residual and returns its global L2 norm.
// `stats` (optional) receives the component-wise norms.
double solver_initial_residual(const Mesh& mesh, const LocalMesh& local_mesh,
                               const HaloExchangePlan& plan,
                               std::vector<Vector4>& U_local,
                               const CaseConfig& cfg, double viscosity,
                               MPI_Comm comm, ResidualStats* stats = nullptr);

// Convective and viscous spectral radii of one cell, summed over all its
// faces:
//   lambda_c = sum_faces (|v . n_face| + a * A_face)
//   lambda_v = max(4/3, gamma) * mu / (rho * Pr) * sum A_face^2 / V
//             (0 for inviscid)
struct SpectralRadii {
    double lambda_c = 0.0;
    double lambda_v = 0.0;
};

SpectralRadii cell_spectral_radii(cgsize_t cell_local,
                                  const PrimitiveState& prim,
                                  const CellConnectivity& conn,
                                  const Mesh& mesh,
                                  const LocalMesh& local_mesh,
                                  const GasParams& gas, double viscosity);

// Local pseudo-time step of one cell: dt = CFL * V / (lambda_c + lambda_v).
double compute_local_time_step(cgsize_t cell_local,
                               const PrimitiveState& prim,
                               const CellConnectivity& conn,
                               const Mesh& mesh, const LocalMesh& local_mesh,
                               const GasParams& gas, double cfl,
                               double viscosity);

// One implicit inner sweep: nonlinear LU-SGS / Gauss-Seidel relaxation of
// the pseudo-time system (V/dt + J) dU = -R with the diagonal spectral-
// radius Jacobian sigma_i = V_i / dt_i + 2 * (lambda_c + lambda_v). Each
// cell's update is
//   dU_i = -R_i(U_current) / sigma_i
// where the flux balance R_i is recomputed with the CURRENT neighbor
// states (freshly relaxed where available), which keeps the sweep stable
// at any CFL. A forward pass over the cells in order is followed by a
// backward pass. Non-physical updates (rho <= 0 or p <= 0) are reverted
// cell-wise. Exchanges the halo after the sweep and returns the L2 norm of
// the residual used for the update.
// One symmetric LU-SGS pass (forward + backward) with matrix-free
// off-diagonal flux differences; applies the correction to U_local and
// exchanges the halo. R_local is the residual at the current state.
void implicit_sweep(
    const Mesh& mesh, const LocalMesh& local_mesh,
    std::vector<Vector4>& U_local, const std::vector<double>& sigma_local,
    const std::vector<PrimitiveState>& prim_local,
    const std::vector<Gradients>& grad_local,
    const std::vector<Vector4>& R_local, const CellConnectivity& conn,
    const GasParams& gas, double dissipation_scale,
    const HaloExchangePlan& halo_plan, MPI_Comm comm);

// Diagnostics of one steady solve.
struct SolverStats {
    int steps = 0;              // outer steps performed
    int inner_iterations = 0;   // total inner sweeps across all steps
    double initial_residual = 0.0;
    double final_residual = 0.0;
    bool converged = false;
    std::vector<double> residual_history;      // L2 per outer step
    std::vector<ResidualStats> stats_history;  // component norms per step
    std::vector<ForceResult> force_history;    // forces per outer step
    std::vector<double> cfl_history;           // CFL per outer step
    std::vector<double> time_history;          // physical time per step
    std::vector<double> dt_history;            // physical dt per step
    // Inner-iteration statistics (contract metadata).
    std::vector<int> inner_history;            // inner sweeps per outer step
    int observed_min_inner = 0;
    int observed_max_inner = 0;
    double typical_inner_iterations = 0.0;     // mean inner sweeps per step
    int inner_target_misses = 0;               // steps not meeting the target
    double inner_target_converged_fraction = 1.0;
    double last_inner_residual_ratio = 0.0;    // last step's final/first ratio
};

// Steady implicit solve loop (pseudo-time march with CFL ramp, point-
// implicit inner sweeps, second-order limited reconstruction):
//   for step = 1..max_steps:
//     cfl = cfl_initial + (cfl_max - cfl_initial) * min(1, step/ramp)
//     prim/gradients (limited) from current U
//     inner sweeps until inner_residual_reduction_target or max_inner
//     forces + residual record
//     converged when R < residual_target or R < R0 * 10^-reduction_target
// U_local is initialized to the freestream state and returned converged
// (or at the last step). When `init_freestream` is false, U_local must
// already hold a valid owned+ghost state (e.g. from a restart file).
SolverStats steady_solve(
    const Mesh& mesh, const LocalMesh& local_mesh,
    std::vector<Vector4>& U_local, const RunControlParams& run_ctrl,
    const GasParams& gas, const FreestreamParams& freestream,
    const ReferenceParams& ref,
    const std::map<std::string, std::string>& bc_map, double viscosity,
    double dissipation_scale, const HaloExchangePlan& halo_plan,
    MPI_Comm comm, bool init_freestream = true);

// Transient solve with BDF2 dual-time stepping (backward Euler for the
// first physical step):
//   for step = 1..n_physical_steps (n = ceil(final_time / time_step)):
//     U^{n-1}, U^n frozen; inner loop over U^{n+1}:
//       R_total = R_spatial(U^{n+1})
//               + (V/2dt) * (3 U^{n+1} - 4 U^n + U^{n-1})    (BDF2)
//               + (V/dt)  * (U^{n+1} - U^n)                  (step 1, BE)
//       LU-SGS sweep with diagonal sigma = V/dt_pseudo + 4(lambda_c +
//       lambda_v) + 3V/(2dt) (V/dt for step 1); break when the total
//       residual drops below inner_residual_reduction_target of its first
//       value (after at least min_inner_iterations sweeps).
//     histories updated once the inner solve for the step is accepted
//     forces/residuals recorded at the physical time
// The physical step count comes from final_time/time_step (or
// max_iterations when final_time is not set). When `init_freestream` is
// false, U_local must already hold a valid owned+ghost state (restart);
// the restart resumes with a backward-Euler step at the stored time.
SolverStats transient_solve(
    const Mesh& mesh, const LocalMesh& local_mesh,
    std::vector<Vector4>& U_local, const RunControlParams& run_ctrl,
    const GasParams& gas, const FreestreamParams& freestream,
    const ReferenceParams& ref,
    const std::map<std::string, std::string>& bc_map, double viscosity,
    double dissipation_scale, const HaloExchangePlan& halo_plan,
    MPI_Comm comm, bool init_freestream = true,
    double start_time = 0.0);

// Implicit solver driver and time-integration loop (Phase 1 API kept for
// compatibility; steady runs use steady_solve, transient runs land in a
// later phase).
class Solver {
public:
    Solver(const FreestreamParams& freestream, const RunControlParams& control,
           const GasParams& gas)
        : freestream_(freestream), control_(control), gas_(gas) {}

    // Runs the full simulation; writes outputs at control_.output_interval.
    // Returns the final residual norm.
    double run(std::vector<Vector4>& U);

    // Single explicit time-integration step (forward Euler placeholder).
    void step(std::vector<Vector4>& U, double dt);

    // Computes the stable explicit time step from the current state.
    double compute_dt(const std::vector<Vector4>& U) const;

    const RunControlParams& control() const { return control_; }

private:
    FreestreamParams freestream_;
    RunControlParams control_;
    GasParams gas_;
};

}  // namespace cfd
