// Global (serial preprocessing) unstructured mesh.
//
// This object is built on rank 0 only, used to derive the cell-adjacency graph
// for METIS and to cut the rank-local partitions, and is then destroyed before
// the solver iteration loop starts.  No rank -- including rank 0 -- holds the
// global mesh or a global conservative state while the solver iterates.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace cfd {

// A named boundary region coming from a CGNS boundary condition / family.
struct BoundaryPatch {
  std::string name;        // mesh family name (e.g. "bc-4", "WALL")
  std::string bc_name;     // CGNS BC_t node name
  Index num_faces = 0;
};

class GlobalMesh {
 public:
  // ---- nodes ----
  std::vector<Real> x, y;

  // ---- cells (mixed polygons, CSR) ----
  std::vector<Index> cell_node_offset;   // size num_cells+1
  std::vector<Index> cell_nodes;         // concatenated, counter-clockwise
  std::vector<std::int8_t> cell_zone;    // originating CGNS zone (diagnostics)

  // ---- boundary patches ----
  std::vector<BoundaryPatch> patches;

  // ---- faces (edges) ----
  std::vector<Index> face_n0, face_n1;   // oriented so that the normal leaves cell_l
  std::vector<Index> face_cell_l;        // always valid
  std::vector<Index> face_cell_r;        // -1 on boundary faces
  std::vector<Index> face_patch;         // -1 on interior faces

  // ---- cell adjacency graph (CSR, interior faces only) ----
  std::vector<Index> adj_offset;
  std::vector<Index> adj_cells;
  std::vector<Index> adj_faces;

  Index numNodes() const { return static_cast<Index>(x.size()); }
  Index numCells() const { return static_cast<Index>(cell_node_offset.size()) - 1; }
  Index numFaces() const { return static_cast<Index>(face_n0.size()); }
  int cellSize(Index c) const { return cell_node_offset[c + 1] - cell_node_offset[c]; }
  const Index* cellNodePtr(Index c) const { return &cell_nodes[cell_node_offset[c]]; }

  // Build faces + adjacency from cells and the boundary-edge lists.
  // `patch_edges[p]` holds the (n0,n1) node pairs of patch p.
  void buildTopology(const std::vector<std::vector<std::pair<Index, Index>>>& patch_edges);

  // Enforce counter-clockwise node ordering (positive area) for every cell.
  void orientCells();

  // Free everything (called after the rank-local meshes are distributed).
  void release();

  // Diagnostic summary written into the log.
  std::string summary() const;
};

}  // namespace cfd
