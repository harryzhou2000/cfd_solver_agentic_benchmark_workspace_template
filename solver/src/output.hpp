#ifndef CFD2D_OUTPUT_HPP
#define CFD2D_OUTPUT_HPP

#include "types.hpp"
#include "mesh.hpp"
#include "solver.hpp"
#include <string>

namespace cfd2d {

CaseConfig parseCaseConfig(const std::string& jsonPath);
void writeMetadata(const std::string& path, const Solver& solver, const CaseConfig& cfg,
                   const LocalMesh& mesh, int rank, int nprocs, const std::string& gitRev);
void writeRunStatus(const std::string& path, const Solver& solver, const CaseConfig& cfg,
                    int nprocs, const std::string& command);
void writePartitionDiagnostics(const std::string& path, const LocalMesh& mesh, int rank, int nprocs);
void writeFieldVTK(const std::string& path, const GlobalMesh& gm, const Solver& solver,
                   const LocalMesh& mesh, int rank, int nprocs);
void writeRestart(const std::string& path, const Solver& solver, const LocalMesh& mesh, int rank, int nprocs);
void writeResidualsCSV(const std::string& path, const Solver& solver);
void writeForcesCSV(const std::string& path, const Solver& solver);

}
#endif
