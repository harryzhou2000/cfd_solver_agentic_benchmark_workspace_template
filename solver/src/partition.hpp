#pragma once

#include <string>
#include <vector>

#include "mesh_global.hpp"

namespace cfd {

// Per-rank partition description produced by METIS-based preprocessing.
struct Partition {
  int nranks = 0;
  std::vector<int> cell_to_rank;              // global cell -> rank
  std::vector<std::vector<int>> owned;        // per rank, sorted global ids
  std::vector<std::vector<int>> ghosts;       // per rank, sorted global ids
  std::vector<std::vector<int>> ghost_owner;  // per rank, owner rank per ghost
  int edge_cut = 0;
};

// Runs METIS k-way partitioning of the cell adjacency graph.
// Only meaningful on rank 0 (or any single rank); all ranks call it with the
// same input so the result is identical.
Partition partition_cells(const GlobalMesh& m, int nranks);

// Writes per-rank binary partition files into <outdir>/partition/rank_<r>.bin
// and <outdir>/partition/global_mesh.bin (only rank 0 calls this; it blocks
// until files are flushed).
void write_partition_files(const GlobalMesh& m, const Partition& p,
                           int nranks, const std::string& outdir);

}  // namespace cfd
