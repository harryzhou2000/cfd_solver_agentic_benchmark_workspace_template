#pragma once

#include "common.hpp"
#include "case_file.hpp"
#include "mesh.hpp"
#include "solver_core.hpp"
#include <mpi.h>

namespace fv {

// Rank-0 CSV file with header; append rows from rank 0.
class CsvWriter {
public:
    void open(const std::string& path, const std::string& header, int rank, bool append = false);
    void row(const std::string& line);  // rank 0 only
    void flush();
private:
    std::ofstream f_;
};

// Gather owned-cell field data to rank 0 and write an ASCII VTU unstructured grid.
void write_field_vtu(const std::string& path, const LocalMesh& m,
                     std::vector<Vec4>& U, Solver& solver,
                     MPI_Comm comm);

// Gather wall surface rows and write surface.csv on rank 0.
void write_surface_csv(const std::string& path, const CaseConfig& cfg, const LocalMesh& m,
                       const std::vector<SurfaceRow>& rows, MPI_Comm comm);

// Restart: rank 0 gathers U (+ histories) in global-cell order and writes one file.
void write_restart(const std::string& path, const LocalMesh& m,
                   const std::vector<Vec4>& U, const std::vector<Vec4>& Un,
                   const std::vector<Vec4>& Unm1, long step, double time, MPI_Comm comm);
// Each rank reads the restart file and fills owned cells of U/Un/Unm1.
bool read_restart(const std::string& path, const LocalMesh& m,
                  std::vector<Vec4>& U, std::vector<Vec4>& Un, std::vector<Vec4>& Unm1,
                  long& step, double& time, MPI_Comm comm);

void write_partition_diagnostics(const std::string& path, const LocalMesh& m, MPI_Comm comm);

void write_json_file(const std::string& path, const std::string& jsonText);

// On restart, drop CSV rows beyond the restart step so continued appends stay consistent.
void truncate_csv_to_step(const std::string& path, long startStep);

std::string iso_time_now();

} // namespace fv
