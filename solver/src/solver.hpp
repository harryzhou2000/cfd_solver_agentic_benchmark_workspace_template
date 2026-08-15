#pragma once
// Finite-volume solver: reconstruction + limiter, conservative residual
// (Roe/Rusanov inviscid + viscous + boundary conditions), matrix-free LU-SGS
// implicit solve, steady pseudo-time march and BDF2 transient two-loop.
#include <mpi.h>
#include <string>
#include <vector>
#include "case.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "physics.hpp"
#include "types.hpp"

namespace cfd {

struct InnerStats {
  long total_inner = 0;
  long total_steps = 0;
  int observed_min = 0;
  int observed_max = 0;
  long target_misses = 0;
  double last_ratio = 0.0;
  void reset() { *this = InnerStats{}; }
  void record(int iters, bool hit, double ratio) {
    total_inner += iters;
    total_steps += 1;
    if (total_steps == 1) { observed_min = observed_max = iters; }
    else { observed_min = std::min(observed_min, iters); observed_max = std::max(observed_max, iters); }
    if (!hit) ++target_misses;
    last_ratio = ratio;
  }
  double meanInner() const { return total_steps ? (double)total_inner / total_steps : 0.0; }
  double convergedFraction() const {
    return total_steps ? 1.0 - (double)target_misses / total_steps : 1.0;
  }
};

struct Solver {
  CaseConfig cfg;
  Gas gas;
  Prim fs;            // freestream primitive
  double mu = 0.0;    // dynamic viscosity
  double k_cond = 0.0;
  int rank = 0, nranks = 1;
  LocalMesh lm;

  // Conservative state for all local cells (owned + ghost), 4 doubles each.
  std::vector<double> U;
  // Primitive gradients at owned cell centers: 6 per cell (ux,uy,vx,vy,Tx,Ty).
  std::vector<double> grad;
  std::vector<double> greconBuf;  // reconstruction gradients [rho,u,v,p] x,y per owned cell
  std::vector<double> Wbuf;        // primitive buffer (reused)
  // Barth limiter per owned cell, per primitive (rho,u,v,p).
  std::vector<double> phi;

 // LU-SGS connectivity: per owned cell, lower/upper (neighbor local idx,
 // off-diagonal coefficient) lists.
 std::vector<int> lowerOff, lowerCell, lowerFace;   // neighbors with idx < i
 std::vector<int> upperOff, upperCell, upperFace;   // neighbors with idx > i
 std::vector<double> lamFace;                       // spectral radius per face
 std::vector<double> specRadCell;                   // sum |lambda|*len per owned cell

 bool useRoe = true;
 double entropyCoeff = 0.10;
 double rusanovScale = 1.0;
 InnerStats innerStats;  // transient inner-iteration statistics
 const Mesh* globalMesh = nullptr;  // full mesh on rank 0 for output only

  void initialize(const CaseConfig& cfg_, int rank_, int nranks_);
  void initFreestream();
  void exchangeHalo(std::vector<double>& state);
  void computeGradients();
  void computeLimiters();
  // Compute the spatial residual R (4 per owned cell) and the LU-SGS diagonal
  // D and spectral data.  R already includes inviscid + viscous + BC fluxes.
  void computeResidual(const std::vector<double>& state, std::vector<double>& R,
                       std::vector<double>& D);
  // LU-SGS sweep: solves (D + L + U) dU = -R approximately.  state is updated
  // in place (owned cells only; ghosts held fixed during the sweep).
  void lusgsSolve(std::vector<double>& state, const std::vector<double>& R,
                  const std::vector<double>& D, const std::vector<double>& Ustart);
  // Run the steady pseudo-time march.  Returns final residual L2.
  double runSteady(const std::string& outDir);
  // Run the BDF2 transient.  Returns final physical time.
  double runTransient(const std::string& outDir);
  // Force coefficients (pressure + viscous, separated).  Filled by computeForces.
  void computeForces(const std::vector<double>& state, double& cl, double& cd,
                     double& cmz, double& pdrag, double& vdrag, double& plift,
                     double& vlift);
};

}  // namespace cfd
