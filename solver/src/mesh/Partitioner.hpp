// METIS k-way partitioning of the cell adjacency (dual) graph, plus a
// reverse Cuthill-McKee renumbering of each part.  Runs on rank 0 only.
#pragma once

#include <string>
#include <vector>

#include "mesh/GlobalMesh.hpp"

namespace cfd {

struct PartitionResult {
  std::vector<int> part;          // owning rank of each global cell
  GlobalIndex edge_cut = 0;
  std::string method;             // "metis_kway_contiguous", "metis_kway_noncontiguous",
                                  // or "single_rank_no_partitioning"
  // Cells of each rank in reverse Cuthill-McKee order (bandwidth reduction for
  // the Gauss-Seidel sweeps of the implicit solver).
  std::vector<std::vector<Index>> ordered_cells;
};

PartitionResult partitionMesh(const GlobalMesh& mesh, int nparts);

}  // namespace cfd
