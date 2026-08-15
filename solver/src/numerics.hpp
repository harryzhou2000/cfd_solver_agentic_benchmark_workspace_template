#pragma once

#include "common.hpp"
#include "fluxes.hpp"

#include <mpi.h>
#include <vector>

namespace cfd {

// Gradient/primitive/limiter workspace sized to local mesh (owned + ghost).
struct GradWork {
    std::vector<PrimGrad> grads;
    std::vector<Prims> prims;
    std::vector<Real> phi; // Barth limiter per owned cell
};

// Main solver class managing the local state, residual assembly, implicit
// solves, force computation, and halo exchange.
class Solver {
  public:
    Solver(const CaseInput& ci, const LocalMesh& lm, const GlobalMesh& gm,
           const Partition& part, MPI_Comm comm, int rank, int n_ranks);

    // Accessors
    std::vector<State>& state() { return U_; }
    const std::vector<State>& state() const { return U_; }
    const CaseInput& case_input() const { return ci_; }
    const LocalMesh& mesh() const { return lm_; }
    int rank() const { return rank_; }
    int n_ranks() const { return n_ranks_; }
    MPI_Comm comm() const { return comm_; }
    const std::vector<Real>& local_volumes() const { return vols_; }
    const std::vector<Vec2>& local_centroids() const { return cents_; }
    const std::vector<BcKind>& face_bc() const { return face_bc_; }
    bool first_order_phase() const { return first_order_; }
    void set_first_order_phase(bool v) { first_order_ = v; }

    // Halo exchange: send owned cells to neighbor ranks, fill ghost cells.
    void exchange_state();
    void exchange_correction(std::vector<State>& corr);

    // Local pseudo-time step from convective + viscous spectral radii.
    void compute_local_dt(Real* dt, Real cfl);

    // Residual assembly: R = -spatial / V [+ physical-time terms].
    // For steady runs, is_transient=false, dt=0, and U_n/U_nm1 are unused.
    void compute_residual(std::vector<State>& R, ResNorm& norm,
                          bool is_transient, Real dt,
                          const std::vector<State>& U_n,
                          const std::vector<State>& U_nm1);

    // Scalar LU-SGS sweep (forward + backward) for the pseudo-time system.
    void lusgs_sweep(const std::vector<State>& R, std::vector<State>& U,
                     const Real* dt_local, bool is_transient, Real dt_phys);

    // Damped block-Jacobi correction with exact 4x4 Euler Jacobians.
    // Returns the pseudo-time residual norm of the inner equation.
    Real block_jacobi_correction(const std::vector<State>& R,
                                 const std::vector<State>& pseudo_old,
                                 const Real* dt_local, Real dt_phys);

    // Newton-GMRES correction for the dual-time system.
    Real newton_gmres_correction(const std::vector<State>& R,
                                 const Real* dt_local, Real dt_phys);

    // Force and surface computation (collective).  If gather_surface is true,
    // all ranks contribute wall rows and rank 0 assembles the full list.
    ForceCoeffs compute_forces(bool gather_surface);
    void write_surface_csv(const std::string& path);

  private:
    const CaseInput& ci_;
    const LocalMesh& lm_;
    const GlobalMesh& gm_;
    const Partition& part_;
    MPI_Comm comm_;
    int rank_, n_ranks_;

    std::vector<State> U_;
    std::vector<Real> vols_;
    std::vector<Vec2> cents_;
    std::vector<BcKind> face_bc_;   // per local face
    std::vector<Real> face_len_;    // edge length
    std::vector<std::string> face_tag_;
    bool first_order_ = false;

    struct WallRow {
        Real x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
        std::string tag;
    };
    std::vector<WallRow> wall_rows_;

    // Assembly of the 4x4 block-diagonal and face off-diagonal blocks for
    // the implicit inner system.
    struct FaceImplicit {
        Mat4 off_owner;
        Mat4 off_neighbor;
        bool used = false;
    };
    void assemble_jacobian(std::vector<Mat4>& block_diag,
                           std::vector<FaceImplicit>& fimp,
                           const Real* dt_local, Real dt_phys) const;
};

// Compute WLS primitive gradients (rho, u, v, T) using 1/d^2 weights.
void compute_gradients(const Solver& solver, const std::vector<State>& U,
                       GradWork& gw);

// Barth-Jespersen limiter on primitive variables.
void barth_limiter(const Solver& solver, const std::vector<State>& U,
                   GradWork& gw);

// Reconstruct left/right primitive states at a face from cell-averaged
// states and limited gradients.
void reconstruct_face(const Solver& solver, const std::vector<State>& U,
                      const GradWork& gw, Index face_id,
                      Prims& qL, Prims& qR);

} // namespace cfd
