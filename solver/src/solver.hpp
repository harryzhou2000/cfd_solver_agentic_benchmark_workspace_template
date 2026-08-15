// Solver: distributed finite-volume compressible Navier-Stokes driver.
// Owns the rank-local mesh (LocalMesh), physics, conservative state, and all
// scratch fields. Methods are implemented across solver.cpp (setup + run
// orchestration), reconstruction.cpp (Green-Gauss gradients + Barth-Jespersen
// limiter), residual.cpp (residual assembly + forces), implicit.cpp (LU-SGS /
// SGS linear solve), time_integrate.cpp (steady pseudo-time + BDF2 transient),
// and io.cpp (output-contract files).
//
// The design leaves clear extension points for 3-D, general EOS, RANS, and
// multi-species: physics is data-driven (not case-hard-coded), boundary
// conditions and fluxes are pluggable, and the mesh/geometry abstractions are
// dimension-parameterized at the data level (Vec2 -> VecN would generalize).
#pragma once
#include "case.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "physics.hpp"
#include <mpi.h>
#include <string>
#include <vector>

namespace cfd {

// inner-iteration statistics for the transient case (aggregated)
struct InnerStats {
  long total_inner = 0;
  long n_steps = 0;
  int min_inner = 1<<30, max_inner = 0;
  long target_misses = 0;
  double last_ratio = 1.0;
};

class Solver {
public:
  // ---- configuration ----
  int rank = 0, nranks = 1;
  CaseDef cd;
  Physics phys;
  LocalMesh lm;
  std::string output_dir;

  // ---- solution state (owned + ghost cells, size (n_owned+n_ghost)*NEQ) ----
  std::vector<double> U;        // conservative
  std::vector<double> Un, Unm1; // transient history (BDF2)
  // primitive (size (n_owned+n_ghost))
  std::vector<double> Wrho, Wu, Wv, Wp, WT;
  // cell-center gradients (owned only): per-variable x/y
  std::vector<double> gRhoX, gRhoY, gUX, gUY, gVX, gVY, gPX, gPY, gTX, gTY;
  // Barth-Jespersen limiter (owned only)
  std::vector<double> limRho, limU, limV, limP;
  // residual + update (owned)
  std::vector<double> R;        // size n_owned*NEQ
  std::vector<double> dU;       // size (n_owned+n_ghost)*NEQ (ghost unused)
  std::vector<double> diag;    // implicit diagonal (owned)
  double cfl_current = 1.0;     // current pseudo-time CFL (set per step)
  std::vector<int> sgs_inner;   // internal faces with both sides owned (topology cache)
  std::vector<int> sgs_bnd;     // boundary faces touching owned cells (topology cache)
  // flat CSR for the SGS implicit (topology precomputed once in setup; only the
  // per-face coefficients are recomputed each implicitSolve call to avoid the
  // per-call vector-of-vectors allocation churn that dominated runtime).
  std::vector<int> sgs_ptr;        // size n_owned+1
  std::vector<int> sgs_nb;         // neighbor local id per CSR entry
  std::vector<int> sgs_face;       // face index per CSR entry
  std::vector<double> sgs_coef;    // coefficient per CSR entry (recomputed/call)
  // wall-face data for force/surface output (rebuilt each force eval)
  std::vector<double> wcx, wcy, wSx, wSy, wlen, wp, wtx, wty;
  std::vector<double> wRho;  // adjacent cell density at each wall face

  // ---- residual norms (global, recomputed after computeResidual) ----
  double compL2[4] = {0,0,0,0};  // per-equation global L2 (rho,rhou,rhov,rhoE)
  double residual_l2_last = 0.0;
  double residual_linf_last = 0.0;

  // ---- run/report metadata ----
  std::string git_revision = "unknown";
  std::string start_iso;
  std::string run_command;
  std::string run_notes;
  double wall_time_seconds = 0.0;
  Forces last_forces;

  // ---- run statistics ----
  double start_phys_time = 0.0;
  double wall_start = 0.0;
  int final_step = 0;
  double final_phys_time = 0.0;
  std::string convergence_status = "failed";
  double residual_reduction_orders = 0.0;
  InnerStats inner_stats;
  double first_residual_l2 = 0.0;
  bool true_bdf2_inner_loop = false;
  int last_inner_iter = 0;

  // ---- API ----
  void setup(const CaseDef& cd, int rank, int nranks);
  // runs the configured case and writes all output-contract files
  void run();

  // ---- components (defined in sibling .cpp files) ----
  void exchangeHalo();                       // fill ghost U from neighbors
  void computePrimitive();                   // U -> W (owned+ghost)
  void computeGradients();                   // Green-Gauss (owned)
  void computeLimiters();                   // Barth-Jespersen (owned)
  // fills R = -(1/A) sum F.S for owned cells; also collects wall-face data
  // (pressure, traction) into wcx... for force output. If add_bdf2_source,
  // adds the BDF2 physical-time source term to R using Un/Unm1.
  void computeResidual(bool add_bdf2_source, double dt_phys);
  // LU-SGS / SGS linear solve: solve (diag + L+U) dU = R for owned cells.
  // n_sweeps forward/backward SGS passes.
  void implicitSolve(int n_sweeps);
  // one steady pseudo-step; returns L2 residual norm (global-reduced).
  double steadyStep(int step, int n_inner);
 // one BDF2 physical-time step with inner dual-time iterations.
 void bdf2Step(int step, double dt);
 // global L2 of R (MPI-reduced)
 double residualL2() const;
 // global residual norms: per-equation L2, total L2, linf (MPI-reduced)
 void computeResidualNorms();
 void residualComponentsL2(double out[4]) const;
 // estimate local pseudo-time step (CFL * A / spectral radius)
 double localDt(int local_cell, double cfl) const;

 // ---- I/O (io.cpp) ----
 void writeMetadata(const std::string& status) const;
 void writeRunStatus() const;
 void appendResiduals(int step, double phys_time, int inner_iter,
                       double cfl, double dt) const;
 void appendForces(int step, double phys_time);
 void writeSurface() const;
 void writeFieldFinal() const;
 void writeRestart() const;
 void writePartitionDiagnostics() const;
 void writePartitionFilesForExaminer() const;
 Forces computeGlobalForcesLocal();

private:
  // wall-face arrays are rebuilt in computeResidual; helper to clear
  void clearWallData();
};

}  // namespace cfd
