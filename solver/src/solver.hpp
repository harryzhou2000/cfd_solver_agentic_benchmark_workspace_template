#pragma once

#include <mpi.h>

#include <functional>
#include <string>
#include <vector>

#include "case_config.hpp"
#include "local_mesh.hpp"
#include "physics.hpp"

struct Forces {
    double cl = 0, cd = 0, cmz = 0;
    double pressure_drag = 0, viscous_drag = 0;
    double pressure_lift = 0, viscous_lift = 0;
};

struct SurfaceRow {
    double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
    std::string tag;
};

struct ResidualNorms {
    double comp[4] = {0, 0, 0, 0}; // per-equation RMS
    double l2 = 0;                 // RMS over all equations
    double linf = 0;               // max over all cells/equations
};

struct InnerStats {
    long total_steps = 0;
    long sum_inner = 0;
    int min_inner = 1 << 30;
    int max_inner = 0;
    long target_misses = 0;
    double last_inner_residual_ratio = 0.0;
    double mean_inner() const { return total_steps ? (double)sum_inner / total_steps : 0.0; }
    double converged_fraction() const {
        return total_steps ? 1.0 - (double)target_misses / total_steps : 1.0;
    }
};

struct SolverConfig {
    std::string inviscid_flux = "rusanov"; // "rusanov" | "hllc"
    double rusanov_dissipation_scale = 1.0;
    double low_mach_mfloor = 0.0; // >0 enables low-Mach flux scaling
    bool second_order = true;
    std::string limiter = "barth_jespersen";
    int report_level = 1; // 0 brief, 1 full
};

// The finite-volume solver. Owns the distributed state for its rank.
class Solver {
  public:
    Solver(MPI_Comm comm, LocalMesh lm, const CaseConfig& cfg, const SolverConfig& scfg);

    void initialize(); // freestream everywhere
    // returns convergence status string ("converged" | "statistically_periodic")
    std::string solve_steady(const std::string& out_dir);
    std::string solve_transient(const std::string& out_dir);

    // optional callback for intermediate transient field dumps (physical time)
    std::function<void(double)> field_callback;
    // optional callback for periodic restart dumps (step, physical time)
    std::function<void(long, double)> restart_callback;
    void set_step_time(long step, double time) {
        step_ = step - 1; // loop pre-increments
        phys_time_ = time;
    }

    // data for final outputs
    const LocalMesh& mesh() const { return lm_; }
    const CaseConfig& config() const { return cfg_; }
    const std::vector<double>& state() const { return U_; } // 4*n_cell
    std::vector<double>& state_mut() { return U_; }
    // set owned-cell states from a restart buffer in local owned order
    void load_owned_state(const std::vector<double>& owned_U);
    // add a smooth transverse velocity perturbation near the origin (used to
    // trigger vortex shedding in symmetric wakes)
    void add_transverse_perturbation(double eps);
    const InnerStats& inner_stats() const { return inner_stats_; }
    long final_step() const { return step_; }
    double final_time() const { return phys_time_; }
    double residual_reduction_orders() const { return res_reduction_orders_; }
    int positivity_fixes() const { return positivity_fixes_; }
    int line_search_backtracks() const { return line_search_backtracks_; }
    double wall_time() const { return wall_time_; }

    Forces compute_forces() const;
    std::vector<SurfaceRow> surface_rows() const;
    ResidualNorms current_residual_norms();

  private:
    MPI_Comm comm_;
    int rank_, nranks_;
    LocalMesh lm_;
    CaseConfig cfg_;
    SolverConfig scfg_;
    Gas gas_;
    double mu_ = 0.0;   // dynamic viscosity (0 inviscid)
    double k_ = 0.0;    // heat conductivity
    Vec4 Uinf_{};
    double qinf_ = 0.5;
    double rhofloor_, pfloor_;

    // state: 4 components per cell (owned + ghost)
    std::vector<double> U_;
    std::vector<double> R_; // 4*n_own, integral residual
    std::vector<double> grad_; // 8*n_cell: grad of (rho,u,v,p), 2 comps each (owned computed, ghosts exchanged)
    std::vector<double> phi_;  // 4*n_cell limiter
    std::vector<double> dt_;   // n_own local pseudo time step
    std::vector<double> sface_; // n_face convective spectral radius * area
    std::vector<double> svface_; // n_face viscous spectral radius * area
    std::vector<double> dU_, dU_old_, dU_prev_; // 4*n_cell LU-SGS work arrays
    std::vector<double> diag_; // n_own LU-SGS diagonal
    std::vector<double> Un_, Unm1_; // 4*n_own BDF histories
    // cell -> faces adjacency
    std::vector<int> cf_off_, cf_face_, cf_side_;

    long step_ = 0;
    double phys_time_ = 0.0;
    double res_reduction_orders_ = 0.0;
    int positivity_fixes_ = 0;
    int line_search_backtracks_ = 0;
    double wall_time_ = 0.0;
    InnerStats inner_stats_;
    std::string start_time_utc_, end_time_utc_;

    void build_cell_face_adj();
    void halo_exchange(std::vector<double>& field, int ncomp);
    void compute_gradients_limiter();
    void compute_residual();
    void compute_spectral_radii();
    void local_timesteps(double cfl);
    void lusgs_sweep(bool final_exchange = true);
    // one nonlinear (pseudo) step for steady solves; returns linear residual ratio
    double nonlinear_step(double cfl, int min_inner, int max_inner, double inner_target,
                          int& used_inner);
    void apply_update(bool use_relax);
    void boundary_state(const LocalMesh::LFace& f, const double* WL, Vec4& UR_cons) const;
    Vec4 face_inviscid_flux(const LocalMesh::LFace& f, const Vec4& UL, const Vec4& UR,
                            double& lambda) const;
    void add_viscous_flux(const LocalMesh::LFace& f, int i, int j, Vec4& flux) const;
    void primitive(int c, double& rho, double& u, double& v, double& p) const;
    void reconstruct_face(const LocalMesh::LFace& f, int c, double* W) const;
    ResidualNorms residual_norms(const std::vector<double>& R) const;
    static std::string utc_now();
};
