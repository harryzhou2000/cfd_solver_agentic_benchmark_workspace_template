#pragma once

#include "mesh.hpp"

#include <mpi.h>

#include <cstdint>
#include <vector>

namespace aerofv {

struct NeighborExchange {
  int rank{-1};
  // Local indices: send only owned cells and receive only ghost cells.
  std::vector<int> send_owned_local;
  std::vector<int> receive_ghost_local;
};

// A solve-rank view.  Cells [0, owned_cell_count) are owned; all following
// cells are one-layer ghosts.  faces contains exactly faces incident on an
// owned cell, while vertices contains only vertices used by local cells.
struct LocalMesh {
  std::vector<Vec2> vertices;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<std::int64_t> global_cell_ids;
  std::vector<std::int64_t> global_face_ids;
  std::vector<std::int64_t> global_vertex_ids;
  std::vector<NeighborExchange> exchanges;
  int owned_cell_count{0};
  std::int64_t global_cell_count{0};
  std::int64_t global_face_count{0};
  std::int64_t global_vertex_count{0};
  std::int64_t edge_cut{0};

  [[nodiscard]] bool is_owned_cell(int local_cell) const {
    return local_cell >= 0 && local_cell < owned_cell_count;
  }
};

struct CellPartition {
  std::vector<int> owner_by_cell;
  std::int64_t edge_cut{0};
};

// METIS K-way partitioning of the GlobalMesh cell-adjacency graph.  The graph
// includes only internal faces.  This is intended to run on rank zero only.
CellPartition partition_cells_metis(const GlobalMesh &mesh, int ranks);

// Construct one rank-local mesh.  It is public for deterministic diagnostics
// and unit tests; production callers should normally use partition_and_distribute.
LocalMesh build_local_mesh(const GlobalMesh &mesh, const CellPartition &partition,
                           int rank);

// Collective distribution entry point. global_on_root must point to the full
// mesh on rank zero and must be null on all other ranks.  Only rank zero builds
// and holds GlobalMesh; all other ranks receive a compact LocalMesh payload.
LocalMesh partition_and_distribute(const GlobalMesh *global_on_root,
                                   MPI_Comm communicator);

} // namespace aerofv
