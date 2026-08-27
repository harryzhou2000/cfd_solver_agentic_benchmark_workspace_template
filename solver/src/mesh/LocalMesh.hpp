// Rank-local unstructured mesh.
//
// Cell indices are laid out as
//     [0, num_owned)                          cells owned by this rank
//     [num_owned, num_owned + num_ghost)      halo cells owned by neighbour ranks
// Only these cells and the faces touching an owned cell exist on the rank; the
// global mesh is destroyed once distribution finishes.
#pragma once

#include <string>
#include <vector>

#include "core/Types.hpp"

namespace cfd {

class LocalMesh {
 public:
  // ---- cells ----
  Index num_owned = 0;
  Index num_ghost = 0;
  std::vector<GlobalIndex> cell_gid;    // global cell id, size num_total
  std::vector<int> cell_owner;          // owning rank, size num_total
  std::vector<Real> cell_volume;        // size num_total
  std::vector<Vec2> cell_center;        // size num_total

  // Local polygon connectivity (needed for geometry and field output).
  std::vector<Index> cell_node_offset;  // size num_total+1
  std::vector<Index> cell_nodes;

  // ---- nodes ----
  std::vector<Real> x, y;
  std::vector<GlobalIndex> node_gid;

  // ---- interior faces (both sides local, at least one owned) ----
  std::vector<Index> face_l, face_r;
  std::vector<Vec2> face_normal;        // unit, oriented from l to r
  std::vector<Real> face_area;          // edge length (2-D "area")
  std::vector<Vec2> face_center;

  // ---- boundary faces (owner cell is always owned by this rank) ----
  std::vector<Index> bface_cell;
  std::vector<Index> bface_patch;
  std::vector<Vec2> bface_normal;       // unit outward normal
  std::vector<Real> bface_area;
  std::vector<Vec2> bface_center;
  std::vector<Index> bface_n0, bface_n1;  // local node ids (for surface output ordering)

  // ---- patch metadata (identical on all ranks) ----
  std::vector<std::string> patch_names;

  // ---- halo exchange descriptors ----
  std::vector<int> neighbors;                 // neighbour rank ids (ascending)
  std::vector<std::vector<Index>> send_cells; // owned local cell ids to send per neighbour
  std::vector<std::vector<Index>> recv_cells; // ghost local cell ids to receive per neighbour

  // ---- global counters (for metadata) ----
  GlobalIndex global_num_cells = 0;
  GlobalIndex global_num_faces = 0;
  GlobalIndex global_num_boundary_faces = 0;
  GlobalIndex partition_edge_cut = 0;

  Index numTotalCells() const { return num_owned + num_ghost; }
  Index numFaces() const { return static_cast<Index>(face_l.size()); }
  Index numBoundaryFaces() const { return static_cast<Index>(bface_cell.size()); }
  Index numNodes() const { return static_cast<Index>(x.size()); }
  int cellSize(Index c) const { return cell_node_offset[c + 1] - cell_node_offset[c]; }
  const Index* cellNodePtr(Index c) const { return &cell_nodes[cell_node_offset[c]]; }

  // Rebuild faces from the local cell list and compute all geometric metrics.
  // `boundary_edges` maps an undirected local node pair to its patch index.
  void buildTopologyAndGeometry(const std::vector<std::array<Index, 3>>& boundary_edges);

  // Divergence-theorem consistency check between face metrics and cell
  // volumes.  Returns the worst relative volume error over owned cells.
  Real checkGeometry() const;
};

}  // namespace cfd
