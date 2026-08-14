#pragma once
// Output layer: CSV histories, surface files, VTU flow fields, binary
// restart files, metadata/run-status JSON, and partition diagnostics.
// All files are written by rank 0; per-cell data is gathered by global cell
// id. Residual and force histories are globally reduced in the solver core.

#include <fstream>
#include <string>
#include <vector>

#include "mesh.hpp"
#include "partition.hpp"
#include "residual.hpp"

namespace cfd2d {

// Gather a per-owned-cell field (width doubles per cell) to a global array
// indexed by global cell id. Returns the full array on rank 0, empty elsewhere.
std::vector<double> gatherCellField(const LocalMesh& lm,
                                    const std::vector<double>& localVals,
                                    int width, int nCellsGlobal, int rank,
                                    int nRanks);

// Inverse of gatherCellField: rank 0 holds the global array, every rank
// receives the values for its owned cells (local ordering).
std::vector<double> scatterCellField(const LocalMesh& lm,
                                     const std::vector<double>& globalVals,
                                     int width, int nCellsGlobal, int rank,
                                     int nRanks);

class CsvWriters {
 public:
  void open(const std::string& outDir);
  void logResidual(int step, double time, int innerIter, double cfl, double dt,
                   const ResidualNorms& n);
  void logForces(int step, double time, const ForceCoefficients& c);
  void flush();

 private:
  std::ofstream res_, force_;
};

void writeSurfaceCsv(const std::string& path,
                     const std::vector<SurfaceRow>& localRows, int rank,
                     int nRanks);

// Field data bundled for VTU output: name -> per-cell values (global order).
struct FieldSet {
  std::vector<std::pair<std::string, std::vector<double>>> cellScalars;
};

void writeVtu(const std::string& path, const GlobalMesh& gm,
              const FieldSet& fields);

// Binary restart: magic + header + U (and optionally Uprev) in global order.
void writeRestart(const std::string& path, const std::string& caseId,
                  int64_t step, double time, int nCellsGlobal,
                  const std::vector<double>& U,
                  const std::vector<double>* Uprev);

struct RestartData {
  int64_t step = 0;
  double time = 0.0;
  std::vector<double> U;
  std::vector<double> Uprev;
};
RestartData readRestart(const std::string& path, const std::string& caseId,
                        int nCellsGlobal);

struct PartitionRow {
  int rank, nOwned, nGhost, nBoundaryFaces, nNeighbors;
  std::string neighborList;
  int sendCells, recvCells;
};
void writePartitionDiagnostics(const std::string& path,
                               const std::vector<PartitionRow>& rows);

}  // namespace cfd2d
