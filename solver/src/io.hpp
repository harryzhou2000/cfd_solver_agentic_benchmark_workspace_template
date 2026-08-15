#pragma once
// Output writers: residuals/forces/surface CSV, VTU field, restart, metadata,
// run_status, partition diagnostics.
#include <string>
#include "solver.hpp"

namespace cfd {

// Create output dir (rank 0) and write CSV headers.
void initOutputFiles(const std::string& outDir, const std::string& case_id, int rank);

void writeResidualRow(const std::string& outDir, int step, double t, int inner,
                      double cfl, double dt, const double compL2[4], double linf,
                      double l2, int rank);
void writeForceRow(const std::string& outDir, int step, double t, double cl,
                   double cd, double cmz, double pdrag, double vdrag,
                   double plift, double vlift, int rank);

// Gather owned solution to rank 0 and write a VTU unstructured-grid file using
// the global mesh (stored on rank 0).
void writeFieldVTU(const std::string& path, const Solver& s, int rank);

// Wall surface distribution CSV.
void writeSurfaceCSV(const std::string& outDir, const Solver& s, int rank);

// Binary restart (rank 0 writes a single file with the global state).
void writeRestart(const std::string& outDir, const Solver& s, int rank);

// metadata.json, run_status.json, partition_diagnostics.csv
void writeMetadata(const std::string& outDir, const Solver& s,
                   const std::string& convergenceStatus, bool completed,
                   double wallTime, int finalStep, double finalTime,
                   double residualReductionOrders, int rank, int nranks);
void writeRunStatus(const std::string& outDir, const Solver& s,
                    const std::string& command, const std::string& status,
                    double wallTime, int finalStep, double finalTime,
                    double residualReduction, const std::string& notes,
                    int rank);
void writePartitionDiagnostics(const std::string& outDir, const LocalMesh& lm,
                               const std::vector<int>& partGlobal,
                               const Mesh& global, int rank, int nranks);

}  // namespace cfd
