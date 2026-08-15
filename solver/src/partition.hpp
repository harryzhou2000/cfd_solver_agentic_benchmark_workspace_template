#pragma once

/// @file partition.hpp
/// METIS-based domain decomposition for MPI parallelism.
///
/// Takes a full serial Mesh and partitions it into `nparts` sub-meshes,
/// each with its owned cells and one layer of ghost cells.

#include "common.hpp"

#include <mpi.h>

#include <vector>

namespace cfd {

/// Partition the full serial mesh into `nparts` sub-meshes using METIS
/// k-way partitioning. Each sub-mesh contains:
///   - all cells assigned to that rank (owned cells), at the front of cells[]
///   - ghost cells (cells neighboring owned cells but owned by other ranks)
///     appended after the owned cells
///   - one layer of ghost cells only
///
/// Cell geometry (centroid, volume), face definitions (including boundary
/// faces whose interior cell is owned), and cell-face-neighbor adjacency
/// are fully rebuilt for each local mesh.  Local node indices are
/// renumbered to include only nodes used by local cells.
///
/// @param  full_mesh  the complete serial mesh with geometry already computed
/// @param  nparts     number of partitions (= MPI size)
/// @param  comm       MPI communicator (used for METIS index type compatibility)
/// @return            one Mesh per rank (indexed by MPI rank)
std::vector<Mesh> partition_mesh(const Mesh& full_mesh, int nparts, MPI_Comm comm);

/// Send a Mesh to a specific MPI rank (blocking send).
/// Serializes all mesh fields into a contiguous buffer.
void send_mesh(const Mesh& mesh, int dest_rank, MPI_Comm comm);

/// Receive a Mesh from a specific MPI rank (blocking receive).
/// Deserializes from a contiguous buffer.
Mesh recv_mesh(int src_rank, MPI_Comm comm);

} // namespace cfd
