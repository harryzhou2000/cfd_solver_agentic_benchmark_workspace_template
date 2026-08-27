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
  std::string method;             // METIS k-way, annotated with whether the
                                  // contiguity constraint had to be relaxed and
                                  // whether METIS ran at all (single rank)
  // Cells of each rank in reverse Cuthill-McKee order (bandwidth reduction for
  // the Gauss-Seidel sweeps of the implicit solver).
  std::vector<std::vector<Index>> ordered_cells;
};

PartitionResult partitionMesh(const GlobalMesh& mesh, int nparts);

}  // namespace cfd
