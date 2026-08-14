#pragma once
#include "types.hpp"
#include "fluxes.hpp"
#include <mpi.h>
#include <vector>
#include <string>
#include <map>

namespace cfd {

// Access to primitive gradient workspace (owned + ghost, sized to local mesh)
struct GradWorkspace {
    std::vector<PrimGrad> grads;
    std::vector<Prims> prims;
    std::vector<real_t> limiter;
};

class Solver {
public:
    Solver(const CaseInput& case_input, const LocalMesh& local_mesh,
           const Mesh& global_mesh, const PartitionInfo& part, MPI_Comm comm, int rank, int n_ranks);

    std::vector<StateVec>& state() { return U_; }
    const std::vector<StateVec>& state() const { return U_; }

    // One halo exchange of the conservative state (owned -> ghost).
    void exchange_state();

    // Compute the transient residual R(U) = -[spatial residual] + physical-time term.
    // For steady runs pass is_transient=false and dt=0.
    void compute_residual(std::vector<StateVec>& R, real_t& res_l2, real_t& res_linf,
                          bool is_transient, real_t dt,
                          const std::vector<StateVec>& U_n,
                          const std::vector<StateVec>& U_nm1);
    void compute_residual(std::vector<StateVec>& R, ResNorm& norm,
                          bool is_transient, real_t dt,
                          const std::vector<StateVec>& U_n,
                          const std::vector<StateVec>& U_nm1);

    // One LU-SGS sweep (forward + backward) on the system
    //   (V/dt_p + 3V/(2dt_phys)) dU + sum_f 0.5*lambda*S*(dU_R - dU_L) = R
    // updating U in place. dt_phys=0 and U_n/U_nm1 unused for steady.
    void lusgs_sweep(const std::vector<StateVec>& R, std::vector<StateVec>& U,
                     const real_t* dt_local, bool is_transient, real_t dt_phys);

    // One block-Jacobi implicit correction for the pseudo-time system
    //   spatial(U) + (V/dt_p)(U - U_old) = 0   (flux-imbalance form)
    // using the exact 4x4 Euler/wall flux Jacobians on the block diagonal
    // and 2-8 damped Jacobi sweeps for the face coupling.  R is the pure
    // spatial residual (per-unit-volume).  The correction is applied to
    // state() with a global state-scale limiter and positivity backtracking.
    // Returns the volume-weighted L2 norm of the pseudo-time residual
    //   R - (U - U_old)/dt_p   (per-unit-volume)
    // evaluated before the correction is applied.  For transient runs pass
    // dt_phys > 0 (BDF2): the 3V/(2 dt_phys) mass term is added to the block
    // diagonal and R already contains the physical-time residual.
    real_t block_jacobi_correction(const std::vector<StateVec>& R,
                                   const std::vector<StateVec>& pseudo_old,
                                   const real_t* dt_local,
                                   real_t dt_phys = 0.0);

    // One Newton step for the inner dual-time system
    //   G(U) = R*V (volume-weighted flux imbalance) = 0
    // using the exact assembled 4x4 block Jacobian (the same operator
    // block_jacobi_correction iterates with) solved by right-preconditioned
    // restarted GMRES (preconditioner = block diagonal).  The Newton step
    // is applied to state() with the per-cell state-scale limiter and
    // positivity backtracking.  Returns the volume-weighted L2 norm of the
    // residual G at the state on entry (matching block_jacobi_correction's
    // return value).
    real_t newton_gmres_correction(const std::vector<StateVec>& R,
                                   const real_t* dt_local,
                                   real_t dt_phys = 0.0);

    // Halo exchange for a correction vector (owned -> ghost), same neighbor
    // pattern as exchange_state.
    void exchange_correction(std::vector<StateVec>& corr);

    // Steady local time step from convective + viscous spectral radii.
    void compute_local_dt(real_t* dt_local, real_t cfl);

    ForceCoeffs compute_forces(bool gather_surface);
    void write_surface_csv(const std::string& path);

    const CaseInput& case_input() const { return case_input_; }
    const LocalMesh& mesh() const { return local_mesh_; }
    int rank() const { return rank_; }
    int n_ranks() const { return n_ranks_; }
    MPI_Comm comm() const { return comm_; }
    // First-order reconstruction phase (startup).  When true, reconstruct_face
    // returns the cell-averaged primitive state directly (no gradient
    // extrapolation).  The driver switches this off after the startup phase so
    // the final solution is second-order accurate.
    bool first_order_phase() const { return first_order_phase_; }
    void set_first_order_phase(bool v) { first_order_phase_ = v; }
    const std::vector<idx_t>& bc_of_face() const { return bc_of_face_; }
    const std::vector<BCType>& wall_bc_of_face() const { return wall_bc_; }
    const std::vector<Vec2>& local_centroids() const { return local_centroids_; }
    const std::vector<real_t>& local_volumes() const { return local_volumes_; }
    std::vector<real_t> cell_dt;

    // global mesh cell centroid/volume for owned cells, indexed by global id
    std::string family_tag_name(idx_t tag) const;

private:
    // Per-face off-diagonal 4x4 blocks of the inner implicit operator.
    struct FaceImplicit {
        Mat4 off_owner;
        Mat4 off_neighbor;
        bool used = false;
    };

    // Assemble the block-diagonal and face off-diagonal 4x4 Jacobian blocks
    // of the inner dual-time/pseudo-time system at the current state.  The
    // pseudo-time diagonal (c[i] = V/dt_p) and the BDF2 mass term are
    // included in block_diag.  Shared by block_jacobi_correction and
    // newton_gmres_correction.
    void assemble_inner_jacobian(std::vector<Mat4>& block_diag,
                                 std::vector<FaceImplicit>& fimp,
                                 const real_t* dt_local,
                                 real_t dt_phys) const;

    const CaseInput& case_input_;
    const LocalMesh& local_mesh_;
    const Mesh& global_mesh_;
    const PartitionInfo& part_;
    MPI_Comm comm_;
    int rank_, n_ranks_;

    // per-local-face BC classification
    std::vector<idx_t> bc_of_face_;   // 0 interior, else family tag id
    std::vector<BCType> wall_bc_;     // BCType for boundary faces
    std::vector<real_t> face_length_; // edge length
    std::vector<std::string> face_tag_name_;
    std::vector<Vec2> local_centroids_;
    std::vector<real_t> local_volumes_;

    // local conservative state, size n_total (owned + ghost)
    std::vector<StateVec> U_;
    bool first_order_phase_ = false;

    // wall surface rows for force/surface output (populated by compute_forces)
    struct WallRow {
        real_t x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
        std::string tag;
    };
    std::vector<WallRow> wall_rows_;
};

// Green-Gauss gradients on the cell-centered field (uses owned + ghost states).
void compute_gradients(const Solver& solver, const std::vector<StateVec>& U,
                       GradWorkspace& ws);

// Barth-Jespersen limiter (primitive-variable based), returns per-cell limiter.
void apply_limiter(const Solver& solver, const std::vector<StateVec>& U,
                   GradWorkspace& ws);

// Reconstruct the left/right primitive states at a face from cell states and
// limited gradients.
void reconstruct_face(const Solver& solver, const std::vector<StateVec>& U,
                      const GradWorkspace& ws, idx_t face_id,
                      Prims& qL, Prims& qR);

} // namespace cfd
