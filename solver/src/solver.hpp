#pragma once
#include "types.hpp"
#include "partition.hpp"
#include "mpi_comm.hpp"
#include "output.hpp"
#include <string>

struct SolverStats {
    int total_steps = 0;
    double final_time = 0.0;
    double wall_time_seconds = 0.0;
    double residual_reduction_orders = 0.0;
    std::string convergence_status = "failed";
    int observed_min_inner = 0;
    int observed_max_inner = 0;
    double observed_mean_inner = 0.0;
    int inner_target_misses = 0;
    double inner_converged_fraction = 0.0;
    double last_inner_residual_ratio = 0.0;
};

class Solver {
public:
    void init(const CaseConfig& config, LocalMesh& lm, MPI_Comm comm);
    SolverStats run(const std::string& output_dir);

private:
    CaseConfig config_;
    LocalMesh* lm_;
    HaloExchanger halo_;
    MPI_Comm comm_;
    int rank_, nranks_;
    int num_owned_, num_total_;
    double mu_ = 0.0;
    double recon_ramp_ = 0.0;

    std::vector<Vec4> U_;
    std::vector<Vec4> U_n_;
    std::vector<Vec4> U_nm1_;
    std::vector<Vec4> residual_;
    std::vector<std::array<Vec4, 2>> gradients_;
    std::vector<double> limiter_phi_;

    void initialize_state();
    void compute_gradients();
    void compute_limiter();
    void compute_residual();
    Vec4 roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal) const;
    Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal) const;
    Vec4 viscous_flux(int fi) const;
    Vec4 farfield_flux(int fi) const;
    Vec4 slip_wall_flux(int fi) const;
    Vec4 noslip_wall_flux(int fi) const;
    Vec4 noslip_viscous_flux(int fi) const;

    void lusgs_sweep(double cfl, const std::vector<Vec4>& rhs, std::vector<Vec4>& dU, double dt_phys_contrib = 0.0);
    double spectral_radius(int ci) const;
    double viscous_spectral_radius(int ci) const;

    Vec4 reconstruct_left(int fi) const;
    Vec4 reconstruct_right(int fi) const;
    Vec4 ensure_positive(const Vec4& U_recon, const Vec4& U_cell) const;

    double compute_residual_l2() const;
    double compute_residual_linf() const;
    void compute_forces(double& cl, double& cd, double& cmz,
                       double& pdrag, double& vdrag, double& plift, double& vlift) const;

    OutputWriter writer_;
};
