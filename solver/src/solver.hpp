#pragma once

/// @file solver.hpp
/// Main solver class orchestrating residual assembly, LU-SGS, and output.

#include "common.hpp"
#include "flux.hpp"
#include "boundary.hpp"
#include "reconstruction.hpp"
#include "halo.hpp"
#include "output.hpp"

#include <mpi.h>

#include <string>
#include <vector>
#include <fstream>

namespace cfd {

/// Main finite-volume flow solver.
class Solver {
public:
    /// Construct from case configuration, mesh, and MPI communicator.
    /// If MPI size > 1, partitions the mesh and builds halo exchange.
    Solver(const CaseConfig& config, Mesh& mesh, MPI_Comm comm = MPI_COMM_WORLD);

    /// Initialize all owned cells with freestream state.
    void initialize();

    /// Run the steady-state pseudo-time loop to convergence.
    void run_steady(const std::string& output_dir);

    /// Run a BDF2 transient time integration (cylinder Re200).
    void run_transient(const std::string& output_dir);

    // ------------------------------------------------------------------
    // Per-step methods (exposed for testing / phase 3 integration)
    // ------------------------------------------------------------------

    /// Compute the spatial residual R(U) for all cells.
    /// @return  L2 norm of the full residual (sum of |R_i|² / n_cells)
    Real compute_residual();

    /// Compute local pseudo-time step for all owned cells.
    Real compute_dt_local(int step);

    /// Perform one LU-SGS sweep (forward + backward).
    void lusgs_sweep(Real cfl, const std::vector<Vec4>& rhs);

    /// Apply Delta-U to the state vector (owned cells only).
    void update_solution();

    /// Compute aerodynamic forces (lift, drag, moment) on wall boundaries.
    /// Uses MPI_Allreduce when running in parallel.
    void compute_forces();

    // ------------------------------------------------------------------
    // Public state for output / inspection
    // ------------------------------------------------------------------

    const CaseConfig& config_;
    Mesh& mesh_;

    std::vector<Vec4> U_;         // conservative states [n_cells]
    std::vector<Vec4> R_;         // residuals [n_cells]
    std::vector<Vec4> dU_;        // solution increments [n_cells]
    std::vector<Real> dt_local_;  // local time steps [n_cells]

    // Primitive variable gradients (for reconstruction)
    std::vector<Vec2> grad_rho_, grad_u_, grad_v_, grad_p_;

    // Force coefficients
    Real cl_{0.0}, cd_{0.0}, cmz_{0.0};
    Real pressure_drag_{0.0}, viscous_drag_{0.0};
    Real pressure_lift_{0.0}, viscous_lift_{0.0};

    // MPI
    MPI_Comm comm_;
    int mpi_rank_{0};
    int mpi_size_{1};

    /// Number of owned cells (first n_owned_ in the cells vector).
    std::size_t n_owned_{0};

    // Partition data
    HaloExchange halo_;
    Mesh full_mesh_copy_;  // kept for reference; actual solve uses local mesh

private:
    // --- Initial residual for normalization ---
    Real initial_residual_l2_{1.0};

    // --- CFL ramping ---
    Real compute_cfl(int step) const;

    // --- Spectral radius for a face ---
    Real face_spectral_radius(const Vec4& UL, const Vec4& UR,
                              const Vec2& normal, Real gamma, Real mu) const;

    // --- LU-SGS diagonal block approximation ---
    Vec4 lusgs_diag(std::size_t cell_idx, Real dt, Real cfl, Real mu) const;

    // --- LU-SGS off-diagonal contribution ---
    Vec4 lusgs_offdiag_contribution(const Vec4& dU_nb, const Face& face,
                                     const Vec4& U_self, Real gamma) const;
};

} // namespace cfd
