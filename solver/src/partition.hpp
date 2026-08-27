#pragma once
#include "types.hpp"
#include <mpi.h>

struct PartitionInfo {
    std::vector<int> cell_partition;
    int edge_cut = 0;
};

PartitionInfo partition_mesh(const Mesh& mesh, int nparts);

struct LocalMesh {
    Mesh mesh;
    int rank = 0;
    int nranks = 1;
    int num_owned = 0;
    int num_ghost = 0;
    std::vector<int> local_to_global_cell;
    std::vector<int> local_to_global_node;
    std::vector<int> neighbor_ranks;
    struct HaloExchange {
        int neighbor_rank;
        std::vector<int> send_cells;
        std::vector<int> recv_cells;
    };
    std::vector<HaloExchange> halos;
    int edge_cut = 0;
};

LocalMesh build_local_mesh(const Mesh& global_mesh,
                           const PartitionInfo& pinfo,
                           int rank, int nranks);
