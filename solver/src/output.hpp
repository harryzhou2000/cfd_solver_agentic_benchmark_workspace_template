#pragma once

#include <string>
#include <vector>

#include "case.hpp"
#include "common.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace cfd {

struct ForceRow {
  long step;
  double time;
  double cl, cd, cmz;
  double pressureDrag, viscousDrag, pressureLift, viscousLift;
};

struct ResidualRow {
  long step;
  double time;
  int innerIter;
  double cfl, dt;
  double rho, rhou, rhov, rhoE, l2, linf;
};

struct WallRow {
  double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
  std::string tag;
};

struct OutputData {
  std::vector<ResidualRow> residuals;
  std::vector<ForceRow> forces;
  std::vector<WallRow> wall;
  // field data (owned cells)
  std::vector<int> cellGlobalId;
  std::vector<int> cellRank;
  std::vector<std::vector<Vec2>> cellPoints;  // per owned cell: vertex coords
  std::vector<std::vector<int>> cellPointIds; // per owned cell: global vertex ids
  std::vector<State> U;
  // partition info
  std::vector<std::array<long, 2>> ownedGhost;  // per rank
  std::vector<std::array<long, 2>> boundarySend;  // per rank: (send cells, recv cells)
  std::vector<std::vector<int>> neighborRanksAll; // per rank
  std::vector<long> boundaryFacesPerRank;
  long edgeCut = 0;
  long numCellsGlobal = 0, numFacesGlobal = 0;
};

// Writers (rank 0 assembles; all ranks participate in gather).
void writeOutputs(const Case& c, const OutputData& out, int rank, int nRanks,
                  const std::string& outDir, const SolveStats& stats,
                  const std::string& commandLine, const std::string& fieldFileName,
                  bool writeIntermediateField);

void writeRestartFile(const Case& c, const OutputData& out, int rank, int nRanks,
                      const std::string& outDir, const std::string& name,
                      long step, double time);

// Write an unstructured VTU file with density, velocity, pressure, Mach,
// total energy, rank id and global cell id (rank 0 only).
void writeVtu(const std::string& path, const OutputData& out, const GasModel& gas);

// Gather per-rank field data into rank-0-held OutputData (owned cells only).
void gatherFieldData(const LocalMesh& mesh, const std::vector<State>& U, int rank,
                     int nRanks, OutputData& out);

// Collective: gather per-rank partition diagnostics into out (rank 0).
void gatherPartitionStats(const LocalMesh& mesh, int rank, int nRanks, OutputData& out);

}  // namespace cfd
