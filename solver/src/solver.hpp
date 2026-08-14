#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <mpi.h>

#include "case_file.hpp"
#include "partition.hpp"

namespace cfd {

using Vec4 = std::array<double, 4>;
using Vec5 = std::array<double, 5>;
using Grad5 = std::array<std::array<double, 2>, 5>;

struct RunStats {
  int final_step = 0;
  double final_time = 0.0;
  double wall_time = 0.0;
  double residual_first = 0.0;
  double residual_last = 0.0;
  double residual_linf_last = 0.0;
  int observed_min_inner = 0;
  int observed_max_inner = 0;
  double observed_mean_inner = 0.0;
  int inner_target_misses = 0;
  int inner_steps_total = 0;
  double last_inner_residual_ratio = 0.0;
  int positivity_fixes = 0;
  std::string convergence_status = "";
  std::string notes;
  Vec4 force_last{0, 0, 0, 0};
  double cl = 0, cd = 0, cmz = 0;
  double pressure_drag = 0, viscous_drag = 0;
  double pressure_lift = 0, viscous_lift = 0;
  std::string start_time_utc, end_time_utc;
};

struct ForceResult {
  double fx_p = 0, fy_p = 0, mz_p = 0;
  double fx_v = 0, fy_v = 0, mz_v = 0;
};

struct SurfaceRow {
  double x, y, nx, ny, p, cp, cf, rho, u, v, mach;
  int32_t family_id;
};

class Solver {
 public:
  Solver(CaseFile cfg, LocalMesh mesh, MPI_Comm comm, std::string out_dir,
         std::string restart_file);

  void run();

  void set_restart_as_initial(bool v) { restart_as_initial_ = v; }

  void debug_gradcheck();
  void debug_visccheck();

  const RunStats& stats() const { return stats_; }
  const LocalMesh& mesh() const { return m_; }
  const CaseFile& cfg() const { return cfg_; }

 private:
  CaseFile cfg_;
  LocalMesh m_;
  MPI_Comm comm_;
  int rank_ = 0, np_ = 1;
  std::string out_dir_;
  std::string restart_file_;
  bool restart_as_initial_ = false;

  double gamma_, Rgas_, prandtl_, cp_, mu_, cond_;
  double rusanov_scale_ = 1.0;
  double mref_ = 1.0;
  bool freeze_limiter_ = false;
  Vec4 Uinf_{};
  Vec5 Winf_{};
  double rho_floor_, p_floor_;
  double qinf_;
  std::array<double, 2> drag_dir_, lift_dir_;
  std::vector<BCType> fam_bc_;

  std::vector<Vec4> U_, Un_, Unm1_;
  std::vector<Vec5> W_;
  std::vector<Grad5> G_;
  std::vector<Vec5> phi_;
  std::vector<Vec4> R_;
  std::vector<Vec4> dU_;
  std::vector<double> diag_;
  std::vector<double> dtau_;

  struct LsqEntry {
    int32_t cell;
    int32_t bnd_face;
    double cx, cy;
  };
  std::vector<std::vector<LsqEntry>> lsq_;
  struct ReconPoint {
    double dx, dy;
  };
  std::vector<std::vector<ReconPoint>> recon_;
  std::vector<std::vector<int32_t>> cell_int_faces_;
  std::vector<std::vector<int32_t>> cell_bnd_faces_;
  std::vector<int32_t> sweep_order_;
  std::vector<std::vector<std::pair<int32_t, int32_t>>> lower_nbr_, upper_nbr_;

  RunStats stats_;
  double phys_time_ = 0.0;
  int phys_step_ = 0;

  FILE* fres_ = nullptr;
  FILE* ffor_ = nullptr;

  void setup();
  void load_restart();
  void halo_exchange(std::vector<Vec4>& a);
  void halo_exchange_grad();
  void cons_to_prim_all();
  void compute_gradients();
  void compute_limiter();
  Vec5 boundary_virtual_state(const Face& f, int32_t cell) const;
  void compute_residual(std::vector<Vec4>& res, double time_diag_coef,
                        const std::vector<Vec4>* uref);
  void compute_face_spectral(const Face& f, const Vec5& wl, const Vec5& wr,
                             double& lam_c, double& lam_v) const;
  void compute_dtau_and_diag(double cfl, double time_diag_coef);
  double lusgs_double_sweep(const std::vector<Vec4>& rhs);
  void apply_update(std::vector<Vec4>& u, const std::vector<Vec4>& du);
  ForceResult compute_forces() const;
  std::vector<SurfaceRow> compute_surface() const;
  std::array<double, 6> residual_norms(const std::vector<Vec4>& res) const;
  double total_residual_norm(const std::vector<Vec4>& res) const;
  void write_residual_row(int step, double time, int inner, double cfl,
                          double dt, const std::array<double, 6>& norms);
  void write_force_row(int step, double time, const ForceResult& fr);
  void write_outputs_final();
  void write_field(const std::string& name);
  void write_restart();
  void run_steady();
  void run_transient();
  std::string utc_now() const;
};

Vec4 cons_from_prim(const Vec5& w, double gamma);
Vec5 prim_from_cons(const Vec4& u, double gamma, double rgas);
Vec4 rusanov_flux(const Vec5& wl, const Vec5& wr, double nx, double ny,
                  double gamma, double scale, double mref);

}  // namespace cfd
