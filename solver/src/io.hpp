#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include "physics.hpp"
#include "solver.hpp"
#include <mpi.h>
#include <string>
#include <vector>

namespace cfd {

struct FieldSnapshot {
    std::vector<Real> rho, u, v, p, mach, T;
    std::vector<int> ownerRank;
};

// Collect primitive field from local owned cells into a snapshot.
FieldSnapshot collectField(const LocalMesh& lm, const std::vector<Cons>& U,
                           const CasePhysics& phys, int rank);

// Write a legacy VTK unstructured-grid field file (gathered to rank 0).
void writeFieldVTK(const std::string& path, const LocalMesh& lm,
                   const std::vector<Cons>& U, const CasePhysics& phys,
                   MPI_Comm comm);

// Write wall surface CSV.
void writeSurface(const std::string& path, const LocalMesh& lm,
                  const std::vector<Cons>& U, const CasePhysics& phys,
                  const CaseInput& input, MPI_Comm comm);

// Write partition diagnostics CSV (per-rank, gathered to rank 0).
void writePartitionDiagnostics(const std::string& path, const LocalMesh& lm,
                               MPI_Comm comm);

// Write restart file (binary, rank-local owned state).
void writeRestart(const std::string& path, const LocalMesh& lm,
                  const std::vector<Cons>& U);
bool readRestart(const std::string& path, LocalMesh& lm, std::vector<Cons>& U);

// Write a CSV file from a list of preformatted rows (rank 0 only).
void writeCSV(const std::string& path, const std::string& header,
              const std::vector<std::string>& rows);

} // namespace cfd
