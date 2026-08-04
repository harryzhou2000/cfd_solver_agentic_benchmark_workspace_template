#pragma once

#include "cfd/mesh.hpp"

#include <mpi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

struct LocalCell {
  std::int64_t global_id = -1;
  Vec2 center{};
  double area = 0.0;
  int owner_rank = -1;
  std::vector<Vec2> vertices;
  std::vector<int> faces;
};

struct LocalFace {
  std::int64_t global_id = -1;
  std::array<std::int64_t, 2> global_nodes{{-1, -1}};
  Vec2 point0{};
  Vec2 point1{};
  Vec2 center{};
  Vec2 normal{};  // out of global left cell
  double length = 0.0;
  int left = -1;
  int right = -1;
  BoundaryType boundary_type = BoundaryType::interior;
  std::string boundary_tag;
};

struct HaloLink {
  int rank = -1;
  std::vector<int> send_cells;
  std::vector<int> receive_cells;
};

struct LocalMesh {
  int rank = 0;
  int rank_count = 1;
  std::int64_t global_cell_count = 0;
  std::int64_t global_face_count = 0;
  std::int64_t partition_edge_cut = 0;
  int owned_cell_count = 0;
  int boundary_face_count = 0;
  std::vector<LocalCell> cells;  // owned first, then one-ring ghosts
  std::vector<LocalFace> faces;
  std::vector<HaloLink> halo_links;

  [[nodiscard]] int ghost_cell_count() const { return static_cast<int>(cells.size()) - owned_cell_count; }
  [[nodiscard]] bool is_owned(int local_cell) const { return local_cell >= 0 && local_cell < owned_cell_count; }
  void rebuild_cell_faces();
};

struct PartitionResult {
  std::vector<int> owner;
  std::int64_t edge_cut = 0;
  std::vector<LocalMesh> local_meshes;
};

PartitionResult partition_global_mesh(const GlobalMesh& mesh, int rank_count);
LocalMesh distribute_local_mesh(const std::vector<LocalMesh>* root_meshes, MPI_Comm communicator);
void validate_distributed_mesh(const LocalMesh& mesh, MPI_Comm communicator);

}  // namespace cfd
