#ifndef CFD2D_SOLVER_HPP
#define CFD2D_SOLVER_HPP

#include "types.hpp"
#include "mesh.hpp"
#include <mpi.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdlib>
#include <array>

namespace cfd2d {

struct RunStats {
  int finalStep = 0;
  double finalPhysicalTime = 0.0;
  std::string convergenceStatus = "failed";
  double residualReduction = 0.0;
  double wallTime = 0.0;
  int obsMinInner = 0, obsMaxInner = 0;
  double meanInner = 0;
  int innerTargetMisses = 0;
  double innerConvergedFraction = 1.0;
  double lastInnerRatio = 0.0;
};

struct ForceData {
  double cl, cd, cmz;
  double pressureDrag, viscousDrag, pressureLift, viscousLift;
};

struct StepRecord {
  int step, innerIter;
  double physicalTime, cfl, dt;
  double resRho, resRhou, resRhov, resRhoE, resL2, resLinf;
  double cl, cd, cmz, pressureDrag, viscousDrag, pressureLift, viscousLift;
};

struct ResComps {
  double rho, rhou, rhov, rhoE, l2, linf;
};

class Solver {
public:
  Solver(const LocalMesh& mesh, const CaseConfig& config, int rank, int nprocs, MPI_Comm comm);
  void initialize();
  void runSteady();
  void runTransient();

  const LocalMesh& mesh;
  const CaseConfig& cfg;
  GasModel gas;
  int rank, nprocs;
  MPI_Comm comm;

  std::vector<Cons> U;
  std::vector<Prim> W;

  RunStats stats;
  double mu = 0;

  std::vector<StepRecord> history;

  void prepareForOutput() { computePrimitive(); computeGradients(); }
  ForceData computeForces();
  ResComps residualComps(const std::vector<Cons>& R);
  double residualL2(const std::vector<Cons>& R);
  double residualLinf(const std::vector<Cons>& R);
  void writeSurface(const std::string& path);

private:
  void computePrimitive();
  void computeGradients();
  void computeResidual(std::vector<Cons>& R, double dtPhys,
                       const std::vector<Cons>* Un, const std::vector<Cons>* Unm1);
  void computeSpectralRadius();
  void jacobiSolve(std::vector<Cons>& R, std::vector<Cons>& dU, double dtPhys,
                     const std::vector<Cons>* Un, const std::vector<Cons>* Unm1);
  void luSGS(std::vector<Cons>& R, std::vector<Cons>& dU, double dtPhys,
             const std::vector<Cons>* Un, const std::vector<Cons>* Unm1);
  void applyBC(Prim& ghost, const Prim& cellW, int faceIdx);

  std::vector<double> faceSigma;
  std::vector<std::array<double,2>> gRho, gU, gV, gP, gT;
  std::vector<double> limiter;
  double currentCFL = 1.0;
  bool firstOrderMode = false;
  double limiterRamp = 1.0;
  bool reuseGradients = false;
};

}
#endif
