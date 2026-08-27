#include "parallel/partitioner.h"

#include <algorithm>
#include <numeric>

#include <metis.h>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {

void buildDualGraph(const GlobalMesh &mesh, std::vector<GlobalIndex> &xadj,
                    std::vector<Index> &adjncy) {
  const Index num_cells = mesh.numCells();
  std::vector<Index> degree(static_cast<std::size_t>(num_cells), 0);
  for (const GlobalFace &face : mesh.faces) {
    if (face.right_cell < 0) continue;  // boundary face: no graph edge
    ++degree[static_cast<std::size_t>(face.left_cell)];
    ++degree[static_cast<std::size_t>(face.right_cell)];
  }

  xadj.assign(static_cast<std::size_t>(num_cells) + 1, 0);
  for (Index c = 0; c < num_cells; ++c) {
    xadj[static_cast<std::size_t>(c) + 1] =
        xadj[static_cast<std::size_t>(c)] + degree[static_cast<std::size_t>(c)];
  }
  adjncy.assign(static_cast<std::size_t>(xadj.back()), -1);

  std::vector<Index> cursor(static_cast<std::size_t>(num_cells), 0);
  for (const GlobalFace &face : mesh.faces) {
    if (face.right_cell < 0) continue;
    const Index l = face.left_cell;
    const Index r = face.right_cell;
    adjncy[static_cast<std::size_t>(xadj[static_cast<std::size_t>(l)] +
                                    cursor[static_cast<std::size_t>(l)]++)] = r;
    adjncy[static_cast<std::size_t>(xadj[static_cast<std::size_t>(r)] +
                                    cursor[static_cast<std::size_t>(r)]++)] = l;
  }
}

PartitionResult partitionMesh(const GlobalMesh &mesh, int num_parts) {
  PartitionResult result;
  const Index num_cells = mesh.numCells();
  result.num_parts = num_parts;
  result.cell_rank.assign(static_cast<std::size_t>(num_cells), 0);

  if (num_parts <= 1) {
    result.partitioner = "metis_kway_single_part";
    result.edge_cut = 0;
    return result;
  }
  if (num_parts > num_cells) {
    throw CnsError("requested " + std::to_string(num_parts) + " MPI ranks but the mesh has only " +
                   std::to_string(num_cells) + " cells");
  }

  std::vector<GlobalIndex> xadj_big;
  std::vector<Index> adjncy_small;
  buildDualGraph(mesh, xadj_big, adjncy_small);

  // METIS uses its own index width (idx_t); convert explicitly.
  std::vector<idx_t> xadj(xadj_big.size());
  std::vector<idx_t> adjncy(adjncy_small.size());
  for (std::size_t i = 0; i < xadj_big.size(); ++i) xadj[i] = static_cast<idx_t>(xadj_big[i]);
  for (std::size_t i = 0; i < adjncy_small.size(); ++i) adjncy[i] = static_cast<idx_t>(adjncy_small[i]);

  idx_t nvtxs = static_cast<idx_t>(num_cells);
  idx_t ncon = 1;
  idx_t nparts = static_cast<idx_t>(num_parts);
  idx_t objval = 0;
  std::vector<idx_t> part(static_cast<std::size_t>(num_cells), 0);

  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 0;  // 0-based
  // A fixed seed makes the partition reproducible from a clean checkout, which
  // the benchmark requires for regenerable results.
  options[METIS_OPTION_SEED] = 12345;
  options[METIS_OPTION_CONTIG] = 1;  // prefer contiguous parts

  int status = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr,
                                  nullptr, &nparts, nullptr, nullptr, options, &objval, part.data());
  if (status != METIS_OK) {
    // Contiguity is a preference, not a requirement; retry without it before
    // reporting a hard failure.
    logWarn("METIS_PartGraphKway with CONTIG=1 failed (status " + std::to_string(status) +
            "); retrying without the contiguity constraint");
    options[METIS_OPTION_CONTIG] = 0;
    status = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr, nullptr,
                                &nparts, nullptr, nullptr, options, &objval, part.data());
  }
  if (status != METIS_OK) {
    throw CnsError("METIS_PartGraphKway failed with status " + std::to_string(status));
  }

  for (Index c = 0; c < num_cells; ++c) {
    result.cell_rank[static_cast<std::size_t>(c)] = static_cast<int>(part[static_cast<std::size_t>(c)]);
  }
  result.edge_cut = static_cast<GlobalIndex>(objval);
  result.partitioner = "metis_kway";
  return result;
}

}  // namespace cns2d
