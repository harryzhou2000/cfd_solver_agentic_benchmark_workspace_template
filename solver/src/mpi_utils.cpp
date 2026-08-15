// MPI helpers, halo-exchange plan construction and ghost-cell exchange.
// Phase 2b: the halo plan is derived locally from the full mesh (identical
// on every rank); exchange_halo is fully implemented for Phase 3 use.

#include "mpi_utils.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cfd {
namespace mpi {

std::string error_string(int err) {
    char buf[MPI_MAX_ERROR_STRING];
    int len = 0;
    MPI_Error_string(err, buf, &len);
    return std::string(buf, static_cast<size_t>(len));
}

std::vector<double> allreduce_sum(const std::vector<double>& local,
                                  MPI_Comm comm) {
    std::vector<double> global(local.size(), 0.0);
    if (!local.empty()) {
        MPI_Allreduce(local.data(), global.data(),
                      static_cast<int>(local.size()), MPI_DOUBLE, MPI_SUM,
                      comm);
    }
    return global;
}

void global_minmax(double local, double& gmin, double& gmax, MPI_Comm comm) {
    MPI_Allreduce(&local, &gmin, 1, MPI_DOUBLE, MPI_MIN, comm);
    MPI_Allreduce(&local, &gmax, 1, MPI_DOUBLE, MPI_MAX, comm);
}

std::string bcast_string(const std::string& local, int root, MPI_Comm comm) {
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    int len = static_cast<int>(local.size());
    MPI_Bcast(&len, 1, MPI_INT, root, comm);
    std::string out(static_cast<size_t>(len), '\0');
    if (rank == root && len > 0) {
        std::memcpy(out.data(), local.data(), static_cast<size_t>(len));
    }
    MPI_Bcast(out.data(), len, MPI_CHAR, root, comm);
    return out;
}

}  // namespace mpi

namespace {

// Throws std::runtime_error unless `rc` is MPI_SUCCESS.
void check_mpi(int rc, const char* what) {
    if (rc != MPI_SUCCESS) {
        throw std::runtime_error(std::string(what) + ": " +
                                 cfd::mpi::error_string(rc));
    }
}

// Ghost cells of `my_rank` in the same order build_local_mesh_simple uses:
// owned cells' face neighbors on other ranks, sorted ascending, unique.
std::vector<cgsize_t> ghost_ids_ordered(const Mesh& mesh,
                                        const std::vector<idx_t>& partition,
                                        int my_rank) {
    std::vector<cgsize_t> ghosts;
    ghosts.reserve(static_cast<size_t>(mesh.n_faces));
    const auto owned = [&](cgsize_t c) {
        return c >= 0 && c < mesh.n_cells &&
               partition[static_cast<size_t>(c)] == my_rank;
    };
    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const cgsize_t l = mesh.face_left[static_cast<size_t>(f)];
        const cgsize_t r = mesh.face_right[static_cast<size_t>(f)];
        const bool l_owned = owned(l);
        const bool r_owned = owned(r);
        if (l_owned && !r_owned) ghosts.push_back(r);
        if (r_owned && !l_owned) ghosts.push_back(l);
    }
    std::sort(ghosts.begin(), ghosts.end());
    ghosts.erase(std::unique(ghosts.begin(), ghosts.end()), ghosts.end());
    return ghosts;
}

}  // namespace

HaloExchangePlan build_halo_plan(const Mesh& mesh,
                                 const std::vector<idx_t>& partition,
                                 std::vector<cgsize_t>& owned_global_ids,
                                 int my_rank, int n_parts, MPI_Comm comm) {
    (void)comm;  // Phase 2b: computed locally, no communication needed.
    const size_t n = static_cast<size_t>(mesh.n_cells);
    if (partition.size() != n) {
        throw std::runtime_error(
            "build_halo_plan: partition size does not match n_cells");
    }
    if (static_cast<size_t>(my_rank) >= static_cast<size_t>(n_parts)) {
        throw std::runtime_error("build_halo_plan: my_rank out of range");
    }

    HaloExchangePlan plan;
    if (n_parts <= 1) return plan;  // single rank: no halos.

    const cgsize_t n_owned = static_cast<cgsize_t>(owned_global_ids.size());

    // Local index of each owned cell (position in owned_global_ids).
    std::unordered_map<cgsize_t, cgsize_t> owned_local;
    owned_local.reserve(owned_global_ids.size());
    for (cgsize_t k = 0; k < n_owned; ++k) {
        owned_local[owned_global_ids[static_cast<size_t>(k)]] = k;
    }

    // Ghost cells, in the exact order build_local_mesh_simple assigns local
    // indices (n_owned + position in this list).
    const std::vector<cgsize_t> ghosts = ghost_ids_ordered(mesh, partition,
                                                           my_rank);

    // Neighbor rank -> index into `plan` (sorted rank order for determinism).
    std::map<int, size_t> rank_to_map;

    // --- Receive lists: ghost cells grouped by their owning rank. ---
    for (cgsize_t k = 0; k < static_cast<cgsize_t>(ghosts.size()); ++k) {
        const cgsize_t g = ghosts[static_cast<size_t>(k)];
        const int owner = partition[static_cast<size_t>(g)];
        if (owner == my_rank) {
            throw std::runtime_error(
                "build_halo_plan: ghost cell owned by this rank");
        }
        auto [it, inserted] =
            rank_to_map.emplace(owner, plan.size());
        if (inserted) plan.push_back(HaloMap{owner, {}, {}});
        plan[it->second].recv_cell_ids_local.push_back(
            static_cast<int>(n_owned + k));
    }

    // --- Send lists: owned cells adjacent to cells of each neighbor rank. ---
    std::vector<std::vector<int>> send_lists(plan.size());
    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const cgsize_t l = mesh.face_left[static_cast<size_t>(f)];
        const cgsize_t r = mesh.face_right[static_cast<size_t>(f)];
        if (l == r) continue;
        const int pl = partition[static_cast<size_t>(l)];
        const int pr = partition[static_cast<size_t>(r)];
        if (pl == my_rank && pr != my_rank) {
            const auto it = rank_to_map.find(pr);
            if (it != rank_to_map.end()) {
                send_lists[it->second].push_back(
                    static_cast<int>(owned_local.at(l)));
            }
        } else if (pr == my_rank && pl != my_rank) {
            const auto it = rank_to_map.find(pl);
            if (it != rank_to_map.end()) {
                send_lists[it->second].push_back(
                    static_cast<int>(owned_local.at(r)));
            }
        }
    }
    for (size_t i = 0; i < plan.size(); ++i) {
        auto& s = send_lists[i];
        // De-duplicate (a cell can touch a neighbor rank via several faces)
        // and sort for a deterministic send order.
        std::sort(s.begin(), s.end());
        s.erase(std::unique(s.begin(), s.end()), s.end());
        plan[i].send_cell_ids_local = std::move(s);
    }

    return plan;
}

void exchange_halo(const HaloExchangePlan& plan, std::vector<Vector4>& U_local,
                   MPI_Comm comm) {
    const size_t np = plan.size();
    if (np == 0) return;

    // Pack buffers per neighbor; kept alive until MPI_Waitall completes.
    std::vector<std::vector<double>> send_bufs(np), recv_bufs(np);
    std::vector<MPI_Request> reqs;
    reqs.reserve(2 * np);

    for (size_t i = 0; i < np; ++i) {
        const HaloMap& m = plan[i];
        const int n_send = static_cast<int>(m.send_cell_ids_local.size());
        const int n_recv = static_cast<int>(m.recv_cell_ids_local.size());

        // Pack: each Vector4 is 4 contiguous doubles.
        send_bufs[i].resize(static_cast<size_t>(4) * m.send_cell_ids_local.size());
        for (size_t j = 0; j < m.send_cell_ids_local.size(); ++j) {
            const Vector4& U =
                U_local[static_cast<size_t>(m.send_cell_ids_local[j])];
            double* p = send_bufs[i].data() + 4 * j;
            p[0] = U.r;
            p[1] = U.u;
            p[2] = U.v;
            p[3] = U.e;
        }
        recv_bufs[i].resize(static_cast<size_t>(4) * m.recv_cell_ids_local.size());

        // Post receives before sends so every send finds a matching receive.
        MPI_Request rr = MPI_REQUEST_NULL, sr = MPI_REQUEST_NULL;
        if (n_recv > 0) {
            check_mpi(MPI_Irecv(recv_bufs[i].data(), 4 * n_recv, MPI_DOUBLE,
                                m.rank, 0, comm, &rr),
                      "exchange_halo: MPI_Irecv");
        }
        if (n_send > 0) {
            check_mpi(MPI_Isend(send_bufs[i].data(), 4 * n_send, MPI_DOUBLE,
                                m.rank, 0, comm, &sr),
                      "exchange_halo: MPI_Isend");
        }
        reqs.push_back(rr);
        reqs.push_back(sr);
    }

    if (!reqs.empty()) {
        check_mpi(MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(),
                              MPI_STATUSES_IGNORE),
                  "exchange_halo: MPI_Waitall");
    }

    // Unpack received states into the ghost slots.
    for (size_t i = 0; i < np; ++i) {
        const HaloMap& m = plan[i];
        for (size_t j = 0; j < m.recv_cell_ids_local.size(); ++j) {
            const double* p = recv_bufs[i].data() + 4 * j;
            Vector4& U = U_local[static_cast<size_t>(m.recv_cell_ids_local[j])];
            U.r = p[0];
            U.u = p[1];
            U.v = p[2];
            U.e = p[3];
        }
    }
}

}  // namespace cfd
