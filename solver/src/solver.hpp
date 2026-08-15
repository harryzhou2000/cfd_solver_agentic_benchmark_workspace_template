#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "case_file.hpp"
#include "flux.hpp"
#include "mesh.hpp"

namespace fv {

struct ForceRow {
  double cl = 0, cd = 0, cmz = 0;
  double pressureDrag = 0, viscousDrag = 0;
  double pressureLift = 0, viscousLift = 0;
};

struct SurfaceRow {
  double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
  std::string tag;
};

struct RunStats {
  long finalStep = 0;
  double finalTime = 0.0;
  double residualReductionOrders = 0.0;
  std::string convergenceStatus = "converged";
  std::string convergenceNotes;
  double initialResidual = 0.0;
  double finalResidual = 0.0;
  ForceRow finalForces;
  // inner iteration statistics
  long innerSamples = 0;
  long innerMin = 0, innerMax = 0;
  double innerMean = 0.0;
  long innerTargetMisses = 0;
  double innerConvergedFraction = 1.0;
  double lastInnerResidualRatio = 0.0;
  int typicalInnerIterations = 0;
  double wallTimeSeconds = 0.0;
};

class Solver {
 public:
  static int overrideInnerSweeps;
  Solver(LocalMesh lm, const CaseConfig& cfg, MPI_Comm comm, InviscidFluxType fluxType);

  // Runs steady or transient driver; writes contract files into outDir.
  // globalMesh is non-null only on rank 0 (used for field/restart assembly).
  RunStats run(const std::string& outDir, const std::string& restartFile,
               const GlobalMesh* globalMesh, long edgeCut,
               const std::string& commandLine, const std::string& gitRev,
               double cflOverride = -1.0);

 private:
  LocalMesh m;
  CaseConfig cfg;
  MPI_Comm comm;
  int rank = 0, size = 1;
  InviscidFluxType fluxType;
  bool firstOrder_ = false;

  // state fields (flat arrays, index cell*width + component)
  std::vector<double> U;     // nCells*4 conservative
  std::vector<double> W;     // nCells*4 primitive (rho,u,v,p)
  std::vector<double> T;     // nCells temperature
  std::vector<double> gradW; // nCells*8 (4 vars x 2 comps)
  std::vector<double> gradT; // nCells*2
  std::vector<double> psi;   // nCells*4 limiter
  std::vector<double> Res;   // nOwned*4 residual (surface-integral form)
  std::vector<double> rhoF;  // nFaces spectral radius (incl. viscous)
  std::vector<double> rhoFc; // nFaces convective-only spectral radius
  std::vector<double> dU;    // nCells*4 update vector
  std::vector<double> dtLoc; // nOwned local pseudo time step
  std::vector<double> diag;  // nOwned implicit diagonal
  std::vector<double> Un;    // nOwned*4 BDF history n
  std::vector<double> Unm1;  // nOwned*4 BDF history n-1

  std::vector<int> bcType;   // nFaces: -1 interior else BCType ordinal
  double mu = 0.0, kCond = 0.0;
  double rhoFloor = 1e-8, pFloor = 1e-10;

  Vec4 Uinf{};
  double Winf[4] = {1, 1, 0, 1};

  // per-wall-face cached quantities (filled by computeWallQuantities)
  std::vector<int> wallFaces;
  std::vector<double> wallP, wallTau, wallUn, wallUt, wallRhoB, wallUb, wallVb, wallMach;

  std::string outDir;

  // ---- core steps ----
  void initFields(const std::string& restartFile, const GlobalMesh* gm);
  void exchange(std::vector<double>& a, int width);
  void computePrimitives();
  void computeGradientsAndLimiter(bool freezeLimiter = false);
  bool limiterFrozen = false;
  double limiterFreezeOrders = 1.5;  // freeze BJ limiter past this reduction
  double bcGhostValue(int bc, int var, const double* Wi, double nx, double ny) const;
  void computeResidual(bool withPhysicalTerm, double physCoef,
                       const std::vector<double>* physRhs);
  void computeLocalTimeStep(double cfl, bool withPhysicalTerm, double physDt);
  void buildDiagonal(bool withPhysicalTerm, double physCoefDt);
  void lusgsSweeps(int nSweeps);
  void applyUpdate();
  double residualNorms(double* perEq, double& linf) const;  // global reductions
  ForceRow computeForces();
  void computeWallQuantities();
  std::vector<SurfaceRow> collectSurfaceRows() const;

  // ---- drivers ----
  RunStats runSteady(const GlobalMesh* gm, long edgeCut, double cflOverride);
  RunStats runTransient(const GlobalMesh* gm, long edgeCut);

  // ---- output helpers ----
  void appendResidualRow(std::ofstream& os, long step, double time, int inner,
                         double cfl, double dt, const double* perEq, double l2,
                         double linf) const;
  void appendForceRow(std::ofstream& os, long step, double time, const ForceRow& f) const;
  void gatherGlobalField(const std::vector<double>& local, int width,
                         std::vector<double>& global) const;  // rank 0
  void writeVtk(const std::string& path, const GlobalMesh* gm, int rankField);
  void writeRestart(const std::string& path);
  void writeSurfaceCsv(const std::string& path);
  void writePartitionDiagnostics(const std::string& stem, long edgeCut) const;

  double cflForStep(long step) const;
  double lusgsSpectralRadiusFace(int f) const;
};

}  // namespace fv
