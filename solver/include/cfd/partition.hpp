#pragma once

#include "cfd/mesh.hpp"

#include <mpi.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cfd {

struct PartitionResult {
    std::vector<int> cell_owner; // indexed by global cell id
    int edge_cut{};
};

PartitionResult partition_metis(const Mesh& mesh, int partitions);

struct LocalCell {
    Cell cell{};
    bool owned{};
};

struct LocalFace {
    Face face{};
    LocalIndex owner_local{-1};
    LocalIndex neighbor_local{-1}; // -1 at physical boundary
};

struct HaloPeer {
    int rank{};
    // send entries are owned cells and recv entries are ghosts.  Each list is
    // independently sorted by the global id transmitted in that direction;
    // this makes matching peer send/receive order deterministic.
    std::vector<LocalIndex> send_owned;
    std::vector<LocalIndex> recv_ghost;
};

struct PartitionDiagnostics {
    int rank{};
    std::int64_t num_cells_owned{};
    std::int64_t num_cells_ghost{};
    std::int64_t num_boundary_faces{};
    std::vector<int> neighbor_ranks;
    std::vector<std::int64_t> send_cells;
    std::vector<std::int64_t> recv_cells;
};

// A distributed rank-local mesh.  Cells are ordered as owned then ghosts.  It
// contains no global mesh or global conservative-state storage.
struct DistributedMesh {
    int rank{};
    int size{};
    std::int64_t global_cell_count{};
    std::int64_t global_face_count{};
    int partition_edge_cut{};
    std::vector<Node> nodes;
    std::vector<LocalCell> cells;
    std::size_t owned_count{};
    std::vector<LocalFace> faces;
    std::vector<std::vector<LocalIndex>> cell_faces;
    std::vector<std::vector<LocalIndex>> adjacency;
    std::vector<HaloPeer> halo;
    PartitionDiagnostics diagnostics;

    [[nodiscard]] bool has_global_mesh() const noexcept { return false; }
    [[nodiscard]] bool is_owned(std::size_t i) const noexcept { return i < owned_count; }
};

// On rank 0, `root_mesh` must contain the full preprocessed mesh.  All other
// ranks must pass an empty Mesh.  Distribution uses rank-0 point-to-point sends
// only; no mesh/state broadcast or allgather is performed.  root_mesh is cleared
// before this function returns on rank 0.
DistributedMesh distribute_mesh(Mesh& root_mesh, const PartitionResult& partition,
                                MPI_Comm comm);

// Exchanges a fixed-width, trivially-copyable cell packet through the deterministic
// neighbor plan.  `values` is indexed by local cell index and therefore includes
// owned values followed by ghost destinations.  Use distinct tags for state,
// gradient, limiter, etc. if calls may overlap.
template <class Packet>
void exchange_halo_packets(const std::vector<HaloPeer>& peers,
                           std::vector<Packet>& values, MPI_Comm comm, int tag) {
    static_assert(std::is_trivially_copyable_v<Packet>,
                  "halo packets must be fixed-width trivially-copyable values");
    std::vector<std::vector<Packet>> send(peers.size()), recv(peers.size());
    std::vector<MPI_Request> requests;
    requests.reserve(peers.size() * 2);
    for (std::size_t p = 0; p < peers.size(); ++p) {
        recv[p].resize(peers[p].recv_ghost.size());
        if (!recv[p].empty()) {
            MPI_Request req{};
            MPI_Irecv(recv[p].data(), static_cast<int>(recv[p].size() * sizeof(Packet)),
                      MPI_BYTE, peers[p].rank, tag, comm, &req);
            requests.push_back(req);
        }
        send[p].reserve(peers[p].send_owned.size());
        for (const auto local : peers[p].send_owned) {
            if (local < 0 || static_cast<std::size_t>(local) >= values.size()) {
                throw std::out_of_range("halo send local cell index");
            }
            send[p].push_back(values[static_cast<std::size_t>(local)]);
        }
        if (!send[p].empty()) {
            MPI_Request req{};
            MPI_Isend(send[p].data(), static_cast<int>(send[p].size() * sizeof(Packet)),
                      MPI_BYTE, peers[p].rank, tag, comm, &req);
            requests.push_back(req);
        }
    }
    if (!requests.empty()) {
        MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE);
    }
    for (std::size_t p = 0; p < peers.size(); ++p) {
        for (std::size_t i = 0; i < recv[p].size(); ++i) {
            const auto local = peers[p].recv_ghost[i];
            if (local < 0 || static_cast<std::size_t>(local) >= values.size()) {
                throw std::out_of_range("halo receive local cell index");
            }
            values[static_cast<std::size_t>(local)] = recv[p][i];
        }
    }
}

} // namespace cfd
