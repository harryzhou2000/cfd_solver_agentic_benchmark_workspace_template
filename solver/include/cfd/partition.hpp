#pragma once

#include "cfd/mesh.hpp"

#include <mpi.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace cfd {

struct Partitioning {
  std::vector<int> cell_owner;
  std::int64_t edge_cut{};
};

struct NeighborSchedule {
  int rank{};
  std::vector<GlobalId> send_global_cells;
  std::vector<GlobalId> receive_global_cells;
  std::vector<LocalIndex> send_local_cells;
  std::vector<LocalIndex> receive_local_cells;
};

struct DistributedMesh;

struct HaloExchangeWorkspaceStatistics {
  std::size_t cached_widths{};
  std::size_t neighbor_buffer_capacity{};
  std::size_t send_value_capacity{};
  std::size_t receive_value_capacity{};
  std::size_t request_capacity{};
  std::size_t growth_events{};
};

// Per-mesh scratch storage. Completed MPI requests are never retained for reuse;
// only their vector storage and the width-specific message buffers are cached.
class HaloExchangeWorkspace {
 public:
  HaloExchangeWorkspaceStatistics statistics() const noexcept;

 private:
  struct NeighborBuffers {
    std::vector<double> send;
    std::vector<double> receive;
  };
  struct WidthBuffers {
    std::size_t width{};
    std::vector<NeighborBuffers> neighbors;
    std::vector<MPI_Request> requests;
  };

  std::vector<WidthBuffers> widths_;
  std::size_t growth_events_{};

  friend void exchange_halo(const DistributedMesh&, std::vector<double>&,
                            std::size_t, MPI_Comm);
};

struct RankPartitionDiagnostics {
  int rank{};
  std::size_t owned_cells{};
  std::size_t ghost_cells{};
  std::vector<int> neighbor_ids;
  std::vector<std::size_t> send_counts;
  std::vector<std::size_t> receive_counts;
};

struct GlobalPartitionDiagnostics {
  std::int64_t edge_cut{};
  std::size_t total_owned_cells{};
  std::size_t total_ghost_cells{};
  std::vector<RankPartitionDiagnostics> ranks;
};

struct LocalVertex {
  GlobalId global_id{invalid_global_id};
  Vec2 position{};
};

struct LocalCell {
  GlobalId global_id{invalid_global_id};
  int owner{};
  bool owned{};
  std::vector<LocalIndex> vertices;
  std::vector<LocalIndex> faces;
  Vec2 center{};
  double area{};
};

struct LocalFace {
  GlobalId global_id{invalid_global_id};
  std::array<LocalIndex, 2> vertices{{invalid_local_index, invalid_local_index}};
  LocalIndex left_cell{invalid_local_index};
  LocalIndex right_cell{invalid_local_index};
  Vec2 center{};
  double length{};
  Vec2 normal{};
  std::string boundary;
};

struct DistributedMesh {
  int rank{};
  int size{};
  std::size_t global_vertex_count{};
  std::size_t global_cell_count{};
  std::size_t global_face_count{};
  std::size_t owned_cell_count{};
  std::vector<LocalVertex> vertices;
  std::vector<LocalCell> cells;  // Owned cells first, followed by one-ring ghosts.
  std::vector<LocalFace> faces;  // Exactly the faces needed by owned residuals.
  std::vector<NeighborSchedule> halo;
  RankPartitionDiagnostics diagnostics;
  GlobalPartitionDiagnostics global_diagnostics;
  // Mutable scratch preserves exchange_halo's API. MPI_THREAD_FUNNELED callers
  // naturally serialize access on the MPI thread; distinct meshes do not share it.
  mutable HaloExchangeWorkspace halo_exchange_workspace;
};

Partitioning partition_cells(const Mesh& mesh, int number_of_parts);

// Only root supplies a mesh. Ownership is consumed so the full mesh is released
// before this function returns on root; other ranks pass nullptr.
DistributedMesh partition_and_distribute(std::unique_ptr<Mesh> root_mesh,
                                         MPI_Comm communicator,
                                         int root = 0);

void validate_distributed_mesh(const DistributedMesh& mesh);

// values is cell-major and must contain cells.size() * width doubles. Only
// neighbor-scoped nonblocking point-to-point messages are issued.
void exchange_halo(const DistributedMesh& mesh,
                   std::vector<double>& values,
                   std::size_t width,
                   MPI_Comm communicator);

}  // namespace cfd
