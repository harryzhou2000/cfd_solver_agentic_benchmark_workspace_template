// cns2d -- rank-local mesh.
//
// This is the ONLY mesh representation that exists during solver iterations.
// Each rank stores:
//   * its owned cells (geometry + connectivity), numbered [0, num_owned);
//   * ghost cells mirroring neighbour-rank cells that its stencil touches,
//     numbered [num_owned, num_owned + num_ghost);
//   * the faces it must integrate: interior faces between two local cells,
//     shared faces between an owned and a ghost cell, and boundary faces;
//   * neighbour lists and the send/receive index maps used by the halo
//     exchange.
//
// No rank ever holds the global cell array, the global node array, or the
// global conservative state during iterations.  The full mesh exists only
// transiently on the preprocessing rank and is freed before the solve starts.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// The deprecated MPI-2 C++ bindings are not used; skipping them avoids pulling
// in a header that triggers function-cast warnings on modern compilers.
#ifndef OMPI_SKIP_MPICXX
#define OMPI_SKIP_MPICXX 1
#endif
#ifndef MPICH_SKIP_MPICXX
#define MPICH_SKIP_MPICXX 1
#endif
#include <mpi.h>

#include "core/case_input.h"
#include "mesh/geometry.h"
#include "mesh/mesh_types.h"
#include "parallel/partitioner.h"

namespace cns2d {

// Classification of a local face.
enum class FaceKind : std::uint8_t {
  kInterior,  // both sides are local cells (owned-owned or owned-ghost)
  kBoundary,  // outer boundary; right side is a boundary state
};

struct LocalFace {
  Index left{-1};   // always an owned or ghost local cell index
  Index right{-1};  // local cell index, or -1 for a boundary face
  FaceKind kind{FaceKind::kInterior};
  int boundary_tag{-1};  // index into DistributedMesh::boundary_names
  FaceGeometry geom{};
  // Endpoint node coordinates, kept for surface output and vorticity recovery.
  Vec2 node0{};
  Vec2 node1{};
};

// Halo communication plan with one neighbour rank.
struct NeighborPlan {
  int rank{-1};
  // Local indices of OWNED cells whose state must be sent to 'rank'.
  std::vector<Index> send_cells;
  // Local indices of GHOST cells that receive state from 'rank'.
  // Ordered to match the sender's send_cells ordering.
  std::vector<Index> recv_cells;
};

struct PartitionDiagnostics {
  int rank{0};
  Index num_cells_owned{0};
  Index num_cells_ghost{0};
  Index num_boundary_faces{0};
  int num_neighbor_ranks{0};
  std::vector<int> neighbor_ranks;
  Index send_cells{0};
  Index recv_cells{0};
};

class DistributedMesh {
 public:
  // Build the rank-local mesh.  Rank 0 reads the CGNS file, builds the merged
  // global mesh, partitions it with METIS and scatters rank-local pieces; every
  // other rank receives only its own piece.  The global mesh is released before
  // this function returns.
  static std::unique_ptr<DistributedMesh> build(const CaseInput &input, MPI_Comm comm);

  // --- sizes -------------------------------------------------------------
  Index numOwned() const { return num_owned_; }
  Index numGhost() const { return num_ghost_; }
  Index numLocal() const { return num_owned_ + num_ghost_; }
  Index numFaces() const { return static_cast<Index>(faces_.size()); }
  GlobalIndex numCellsGlobal() const { return num_cells_global_; }
  GlobalIndex numFacesGlobal() const { return num_faces_global_; }

  // --- geometry ----------------------------------------------------------
  const std::vector<CellGeometry> &cells() const { return cells_; }
  const std::vector<LocalFace> &faces() const { return faces_; }
  // Global id of each local cell (owned first, then ghosts).
  const std::vector<GlobalIndex> &globalCellId() const { return global_cell_id_; }

  // Cell node coordinates, used for field output and cell-shape diagnostics.
  const std::vector<std::array<Vec2, 4>> &cellNodes() const { return cell_nodes_; }
  const std::vector<int> &cellNumNodes() const { return cell_num_nodes_; }

  // --- boundaries --------------------------------------------------------
  const std::vector<std::string> &boundaryNames() const { return boundary_names_; }
  const std::vector<BCType> &boundaryTypes() const { return boundary_types_; }
  BCType boundaryTypeOfTag(int tag) const { return boundary_types_[static_cast<std::size_t>(tag)]; }

  // --- parallel layout ---------------------------------------------------
  const std::vector<NeighborPlan> &neighbors() const { return neighbors_; }
  MPI_Comm comm() const { return comm_; }
  int rank() const { return rank_; }
  int size() const { return size_; }

  const PartitionDiagnostics &diagnostics() const { return diagnostics_; }
  GlobalIndex edgeCut() const { return edge_cut_; }
  const std::string &partitionerName() const { return partitioner_name_; }

  // Least-squares gradient stencil: for each owned cell, the local indices of
  // its face neighbours and the precomputed pseudo-inverse weights.
  //
  // A stencil entry refers either to a neighbouring cell (kCell) or to a
  // boundary face (kBoundaryFace).  Including boundary faces is what gives
  // wall-adjacent cells a correct wall-normal gradient: without the imposed
  // boundary state the no-slip velocity gradient, and hence skin friction,
  // would be systematically underestimated.
  enum class StencilKind : std::uint8_t { kCell = 0, kBoundaryFace = 1 };

  struct StencilEntry {
    Index index{-1};       // local cell index, or local face index
    StencilKind kind{StencilKind::kCell};
    Vec2 weight{};         // least-squares pseudo-inverse row
  };

  struct GradientStencil {
    Index begin{0};
    Index count{0};
  };
  const std::vector<GradientStencil> &gradientStencils() const { return grad_stencil_; }
  const std::vector<StencilEntry> &gradientEntries() const { return grad_entries_; }

  // Boundary faces owned by this rank, as indices into faces().
  const std::vector<Index> &boundaryFaces() const { return boundary_faces_; }
  // Position of a boundary face within boundaryFaces(), or -1.
  Index boundaryFaceSlot(Index face) const { return boundary_face_slot_[static_cast<std::size_t>(face)]; }
  Index numBoundaryFaces() const { return static_cast<Index>(boundary_faces_.size()); }

  // Faces incident on each cell (used by LU-SGS and the limiter).
  struct CellFaceRange {
    Index begin{0};
    Index count{0};
  };
  const std::vector<CellFaceRange> &cellFaceRanges() const { return cell_face_range_; }
  const std::vector<Index> &cellFaces() const { return cell_faces_; }
  // +1 if the cell is the face's left side, -1 if it is the right side.
  const std::vector<signed char> &cellFaceSign() const { return cell_face_sign_; }

  // Total wall length on this rank (diagnostic).
  Real localWallLength() const { return local_wall_length_; }

  std::string describe() const;

 private:
  DistributedMesh() = default;

  void finalizeGeometry();
  void buildGradientStencils();
  void buildCellFaceMaps();
  void fillDiagnostics();

  MPI_Comm comm_{MPI_COMM_NULL};
  int rank_{0};
  int size_{1};

  Index num_owned_{0};
  Index num_ghost_{0};
  GlobalIndex num_cells_global_{0};
  GlobalIndex num_faces_global_{0};

  std::vector<CellGeometry> cells_;              // size numLocal()
  std::vector<std::array<Vec2, 4>> cell_nodes_;  // size numLocal()
  std::vector<int> cell_num_nodes_;              // size numLocal()
  std::vector<GlobalIndex> global_cell_id_;      // size numLocal()
  std::vector<LocalFace> faces_;

  std::vector<std::string> boundary_names_;
  std::vector<BCType> boundary_types_;

  std::vector<NeighborPlan> neighbors_;
  PartitionDiagnostics diagnostics_;
  GlobalIndex edge_cut_{0};
  std::string partitioner_name_{"metis_kway"};

  std::vector<GradientStencil> grad_stencil_;
  std::vector<StencilEntry> grad_entries_;

  std::vector<Index> boundary_faces_;
  std::vector<Index> boundary_face_slot_;

  std::vector<CellFaceRange> cell_face_range_;
  std::vector<Index> cell_faces_;
  std::vector<signed char> cell_face_sign_;

  Real local_wall_length_{0.0};
};

}  // namespace cns2d
