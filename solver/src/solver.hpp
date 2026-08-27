#pragma once
// Solver driver: halo exchange, residual assembly, implicit SGS relaxation,
// steady pseudo-time marching, and BDF2 dual-time transient marching.
#include "common.hpp"
#include "case_config.hpp"
#include "local_mesh.hpp"
#include "grad.hpp"
#include <mpi.h>

namespace fv {

struct ForceRecord {
  double cl = 0, cd = 0, cmz = 0;
  double pressure_drag = 0, viscous_drag = 0, pressure_lift = 0, viscous_lift = 0;
};

struct ResidualNorms {
  double eq[4] = {0, 0, 0, 0};  // RMS per conservative equation (R/V)
  double l2 = 0;                // overall RMS
  double linf = 0;              // max |R/V|
};

struct InnerStats {
  long minIt = -1, maxIt = 0, totalIt = 0, steps = 0;
  long misses = 0;  // steps that failed the inner residual target
  double lastRatio = 0.0;
  double mean() const { return steps > 0 ? (double)totalIt / steps : 0.0; }
  double convergedFraction() const { return steps > 0 ? 1.0 - (double)misses / steps : 1.0; }
};

class Solver {
public:
  Solver(CaseConfig cfg, LocalMesh mesh, MPI_Comm comm, bool useRoe);
  void run(const string& outdir, const string& restartFile);
  const CaseConfig& cfg() const { return cfg_; }
  const LocalMesh& mesh() const { return mesh_; }

private:
  CaseConfig cfg_;
  LocalMesh mesh_;
  MPI_Comm comm_;
  int rank_ = 0, nranks_ = 1;
  bool useRoe_ = true;
  bool firstOrder_ = false;  // debug: FV2D_ORDER=1 disables reconstruction
  double venkatK_ = -1.0;     // >0: Venkatakrishnan limiter; <=0: Barth-Jespersen
  double uclip_ = 0.2;       // relative update clipping (0 disables; FV2D_UCLIP)
  bool bndFirstOrder_ = false;  // debug: FV2D_BND1=1 uses cell-center state at boundaries
  // limiter freezing for deep steady convergence (generic, residual-triggered)
  bool limFrozen_ = false;
  double freezeOrders_ = 1.5;  // freeze after this many orders of residual drop
  double resAtFreeze_ = 0.0;
  vector<Limiters> limFrozenStore_;
  // two-phase steady strategy: first-order startup, then 2nd-order with the
  // limiter frozen from the developed field (generic, residual-triggered)
  bool secondOrderEnabled_ = false;
  double switchOrders_ = 1.5;  // FV2D_SWITCH_ORDERS; 1e30 disables 2nd order
  long switchStep_ = 300;      // FV2D_SWITCH_STEP: earliest 2nd-order switch
  long freezeStep_ = 1000000000;  // FV2D_FREEZE_STEP: force limiter freeze
  bool freezeOnNextResidual_ = false;
  Prim fs_;  // freestream primitive state
  double mu_ = 0, kCond_ = 0;
  double rhoFloor_ = 1e-12, pFloor_ = 1e-12;
  vector<State> U_, Un_, Unm1_, R_;
  vector<Prim> W_;
  vector<Grads> grads_;
  vector<Limiters> lim_;
  vector<double> faceLam_;   // inviscid spectral radius * area per local face
  vector<double> faceLamV_;  // viscous spectral radius * area per local face
  vector<double> diag_;      // implicit diagonal per owned cell
  vector<double> dtau_;      // local pseudo time step per owned cell
  vector<State> lusgsTmp_;   // scratch for the LU-SGS forward state
  // halo machinery
  vector<vector<double>> sendBuf_, recvBuf_;
  void haloExchange(vector<double>& data, int stride);
  void syncU();
  void syncRecon();
  void syncDU(vector<State>& dU);
  // numerics
  GhostFn ghostFn();
  void updatePrimitives();
  void computeResidual(vector<State>& R, bool withTimeTerm, double bdfC0, double bdfC1,
                       double bdfC2, double dt);
  void computeLocalDt(double cfl);
  void buildDiag(double cfl, double bdfC0, double dt);
  void sgsSweepPair(vector<State>& dU, const vector<State>& rhs);
  // matrix-free GMRES solve of J*dU = rhs (J = dR/dU via Frechet derivatives),
  // right-preconditioned by scalar LU-SGS sweep pairs. Returns iterations used.
  long gmresSolve(const vector<State>& rhs, vector<State>& dU, double tol, long m, long maxIt);
  void applyPrec(const vector<State>& z, vector<State>& y, long sweepPairs);
  void frechetMatvec(const vector<State>& z, vector<State>& w);
  double dotGlobal(const vector<State>& a, const vector<State>& b) const;
  double normGlobal(const vector<State>& a) const;
  double lastGmresRelResid_ = 0.0;
  bool applyUpdate(const vector<State>& dU, vector<State>& U);
  ResidualNorms residualNorms(const vector<State>& R) const;
  ForceRecord computeForces() const;
  // run phases
  void steadyPhase(const string& tag, long maxSteps, double resTargetOrders, double cfl0,
                   double cfl1, long rampSteps, long minIt, long maxIt, double linTarget,
                   bool logCsv, long stepBase);
  void transientPhase();
  // per-step helpers
  double cflAt(long step, double cfl0, double cfl1, long ramp) const;
  void logResidualCsv(long step, double time, long inner, double cfl, double dt,
                      const ResidualNorms& rn);
  void logForcesCsv(long step, double time, const ForceRecord& f);
  // state
  long step_ = 0;
  double time_ = 0.0;
  double res0_ = 0.0;
  InnerStats inner_;
  string outdir_;
  FILE* fRes_ = nullptr;
  FILE* fForce_ = nullptr;
  FILE* fLog_ = nullptr;
  double wallStart_ = 0;
  double nextFieldTime_ = 0.0;
  bool converged_ = false;
  string convergenceStatus_ = "failed";
  string notes_;
  long finalStep_ = 0;
  string startTimeUtc_;
  double residualReductionOrders_ = 0.0;
  void logLine(const string& s);
  void writeIntermediateField();
  void finalizeOutputs();
  void writeRestart(const string& name);
  void readRestart(const string& path);
  // output helpers (defined in output.cpp)
  void writeSurfaceCsv();
  void writeFieldVtk(const string& path);
  void writePartitionDiag();
  void writeMetadataJson();
  void writeRunStatusJson();
  friend struct OutputHelper;
};

}  // namespace fv
