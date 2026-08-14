#pragma once

#include "types.h"
#include "mesh.h"
#include "partition.h"
#include "fluxes.h"
#include "reconstruction.h"
#include "implicit.h"
#include "case_io.h"
#include <vector>
#include <string>
#include <mpi.h>

namespace cfd2d {

struct Solver {
  const LocalMesh* lm = nullptr;
  GasPhysics gas;
  const CaseInput* ci = nullptr;
  int rank = 0, nranks = 1;

  // State
  std::vector<ConsState> U;     // conservative (owned + ghost)
  std::vector<PrimState> P;    // primitive (owned + ghost)

  // Gradients and reconstruction
  std::vector<std::array<double,4>> gradX, gradY;
  std::vector<FaceRecon> recon;

  // Residual
  std::vector<ConsState> R;

  // Implicit solver
  LUSGSSolver implicit;

  // BDF2 history (for transient)
  std::vector<ConsState> Un, Unm1; // U^n, U^{n-1}

  // Initialize state to freestream
  void initialize();
  // Halo exchange of ghost cell states
  void exchangeHalo();
  // Compute residual R from current state
  void computeResidual(double dt, bool transient, double physTime);
  // Apply boundary conditions to ghost cells
  void applyBCs();
  // Compute forces (cl, cd, cmz, etc.)
  void computeForces(double& cl, double& cd, double& cmz,
                     double& pDrag, double& vDrag, double& pLift, double& vLift);
  // Compute global residual norm
  double residualNorm() const;
  // Local time step
  double computeDt(double cfl) const;
};

} // namespace cfd2d
