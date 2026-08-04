#pragma once

#include "cfd/mesh.hpp"

#include <mpi.h>

#include <array>
#include <unordered_map>
#include <vector>

namespace cfd {

struct PartitionResult {
    std::vector<int> cell_owner; // indexed by global cell id
    int edge_cut = 0;
};

/// METIS graph partitioning of the face-adjacent cell graph.
PartitionResult partition_cells_metis(const Mesh& mesh, int parts);

struct LocalFace {
    int global_id = -1;
    std::array<int, 2> nodes{{-1, -1}};
    int left_cell = -1;  // local cell index
    int right_cell = -1; // local cell index, -1 for physical boundary
    Vec2 centroid;
    Vec2 normal;      // length-weighted outward normal from left_cell
    Vec2 unit_normal; // outward unit normal from left_cell
    double length = 0.0;
    BoundaryType boundary_type = BoundaryType::interior;
    std::string boundary_name;
};

struct NeighborPlan {
    int rank = -1;
    std::vector<int> send_owned_local_indices;
    std::vector<int> recv_ghost_local_indices;
};

struct LocalMesh {
    int rank = 0;
    int size = 1;
    int global_cell_count = 0;
    int global_face_count = 0;
    int partition_edge_cut = 0;
    int owned_cell_count = 0;
    std::vector<int> local_to_global_cell; // owned cells first, then ghosts
    std::unordered_map<int, int> global_to_local_cell;
    std::vector<int> local_to_global_node;
    std::vector<Vec2> nodes;               // only nodes referenced by local cells
    std::vector<Cell> cells;               // node ids are local-node indices
    std::vector<LocalFace> faces;          // faces adjacent to an owned cell
    std::vector<NeighborPlan> neighbors;

    bool is_owned(int local_cell) const noexcept { return local_cell >= 0 && local_cell < owned_cell_count; }
    int ghost_cell_count() const noexcept { return static_cast<int>(cells.size()) - owned_cell_count; }
};

/// Owns only neighbor-scoped MPI requests and buffers. State is laid out as
/// [local-cell][component], with owned cells followed by ghosts.
class HaloExchange {
public:
    HaloExchange() = default;
    HaloExchange(const LocalMesh& mesh, MPI_Comm comm);

    const std::vector<NeighborPlan>& neighbors() const noexcept { return neighbors_; }
    void begin(const std::vector<double>& values, int components);
    void finish(std::vector<double>& values, int components);
    void exchange(std::vector<double>& values, int components);

private:
    MPI_Comm comm_ = MPI_COMM_NULL;
    std::size_t local_cell_count_ = 0;
    std::vector<NeighborPlan> neighbors_;
    std::vector<std::vector<double>> send_buffers_, recv_buffers_;
    std::vector<MPI_Request> requests_;
    bool active_ = false;
};

/// Collective construction: rank zero reads and partitions, then sends each
/// rank its compact owned+ghost mesh.  No global mesh is retained on non-root.
LocalMesh read_partition_distribute(const std::string& cgns_path, const BoundaryMap& boundary_map,
                                    MPI_Comm comm, double merge_tolerance = 1.0e-10);

} // namespace cfd
