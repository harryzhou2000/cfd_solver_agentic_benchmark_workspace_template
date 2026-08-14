#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "local_mesh.hpp"
#include "solver.hpp"

// Gathered (rank-0) global view of the solution used for field/restart I/O.
struct GlobalField {
    long long num_cells = 0;
    long long num_nodes = 0;
    std::vector<double> node_x, node_y;   // by global node id
    std::vector<int> cell_nnodes;         // by global cell id
    std::vector<std::array<int, 4>> cell_nodes; // global node ids
    std::vector<double> rho, u, v, p, mach, temp, rankid;
};

// Gather owned-cell solution to rank 0 (valid on rank 0 only afterwards).
GlobalField gather_global_field(MPI_Comm comm, const LocalMesh& lm,
                                const std::vector<double>& U, const Gas& gas);

// Gather an extra per-cell scalar (owned cells) to rank 0 in global cell order.
std::vector<double> gather_cell_scalar(MPI_Comm comm, const LocalMesh& lm,
                                       const std::vector<double>& local_owned);

// Write an unstructured VTU (appended binary) field file on rank 0.
void write_vtu(const std::string& path, const GlobalField& gf);

// Write/read restart files (rank 0 writes/reads; scatter/gather with MPI).
void write_restart(MPI_Comm comm, const std::string& path, const LocalMesh& lm,
                   const std::vector<double>& U, long step, double time);
void read_restart(MPI_Comm comm, const std::string& path, const LocalMesh& lm,
                  std::vector<double>& global_U, long& step, double& time);

// Gather surface rows and write surface.csv on rank 0.
void write_surface_csv(MPI_Comm comm, const std::string& path,
                       const std::vector<SurfaceRow>& local_rows);

// Partition diagnostics CSV on rank 0.
void write_partition_csv(const std::string& path, const PartitionInfo& pinfo);
