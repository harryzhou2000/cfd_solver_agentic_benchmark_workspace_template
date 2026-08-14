#pragma once

#include "types.h"
#include "mesh.h"
#include "partition.h"
#include "fluxes.h"
#include "case_io.h"
#include <vector>
#include <string>
#include <mpi.h>

namespace cfd2d {

struct SolverState; // forward

// Write partition diagnostics CSV
void writePartitionDiagnostics(const std::string& dir, const LocalMesh& lm,
                                int rank, int nranks);

// Write residuals CSV (append mode)
void writeResidualsHeader(const std::string& dir);
void writeResidualRow(const std::string& dir, int step, double physTime,
                      int innerIter, double cfl, double dt,
                      const ConsState& res);

// Write forces CSV (append mode)
void writeForcesHeader(const std::string& dir);
void writeForcesRow(const std::string& dir, int step, double physTime,
                    double cl, double cd, double cmz,
                    double pDrag, double vDrag, double pLift, double vLift);

// Write surface CSV
void writeSurface(const std::string& dir, const LocalMesh& lm,
                  const std::vector<PrimState>& P, const GasPhysics& gas,
                  const CaseInput& ci, int rank, int nranks);

// Write field VTU
void writeFieldVTU(const std::string& dir, const LocalMesh& lm,
                   const std::vector<PrimState>& P, const GasPhysics& gas,
                   int rank, int nranks);

// Write restart file (binary)
void writeRestart(const std::string& dir, const std::vector<ConsState>& U,
                  int numOwned, int step, double physTime);

// Write metadata.json
void writeMetadata(const std::string& dir, const CaseInput& ci,
                   const LocalMesh& lm, const GasPhysics& gas,
                   int nranks, int numCellsGlobal, int numFacesGlobal,
                   bool completed, const std::string& convStatus,
                   const std::string& gitRev,
                   int observedMinInner, int observedMaxInner,
                   double innerTargetMisses, double innerConvergedFrac,
                   double lastInnerRatio,
                   const std::string& startTime, const std::string& endTime);

// Write run_status.json
void writeRunStatus(const std::string& dir, const CaseInput& ci,
                    const std::string& command, int nranks, double wallTime,
                    int finalStep, double finalPhysTime,
                    const std::string& convStatus, double resReduction,
                    const std::string& notes);

} // namespace cfd2d
