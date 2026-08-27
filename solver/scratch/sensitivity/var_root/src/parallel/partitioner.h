// cns2d -- METIS k-way partitioning of the dual (cell adjacency) graph.
//
// The graph is built from the merged face list: one vertex per cell, one edge
// per interior face.  This is done once on the preprocessing rank; the result
// is a cell -> rank map that drives the rank-local mesh construction.
#pragma once

#include <string>
#include <vector>

#include "core/types.h"
#include "mesh/mesh_types.h"

namespace cns2d {

struct PartitionResult {
  std::vector<int> cell_rank;  // size numCells(); owning rank of each cell
  int num_parts{1};
  GlobalIndex edge_cut{0};
  std::string partitioner;  // "metis_kway", "single_rank", ...
};

// Build the CSR dual graph of the mesh (interior faces only).
void buildDualGraph(const GlobalMesh &mesh, std::vector<GlobalIndex> &xadj,
                    std::vector<Index> &adjncy);

// Partition into 'num_parts' parts.  For num_parts == 1 this returns the
// trivial map without calling METIS.  Throws CnsError if METIS fails.
PartitionResult partitionMesh(const GlobalMesh &mesh, int num_parts);

}  // namespace cns2d
