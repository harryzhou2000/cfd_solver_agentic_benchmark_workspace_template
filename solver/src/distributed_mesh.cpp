#include "distributed_mesh.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace cfd {

namespace {

constexpr int kGhostPayload = 12;  // 4 conservative + 8 gradient entries

void pack_payload(int cell, const std::vector<double>& U,
                  const std::vector<double>& grad, double* out) {
    for (int i = 0; i < kNC; ++i) out[i] = U[cell * kNC + i];
    for (int i = 0; i < 8; ++i) out[kNC + i] = grad[cell * 8 + i];
}

void unpack_payload(int cell, const double* in, std::vector<double>& U,
                    std::vector<double>& grad) {
    for (int i = 0; i < kNC; ++i) U[cell * kNC + i] = in[i];
    for (int i = 0; i < 8; ++i) grad[cell * 8 + i] = in[kNC + i];
}

}  // namespace

DistributedMesh build_distributed_mesh(const GlobalMesh& global,
                                       const PartitionResult& part,
                                       MPI_Comm comm) {
    int rank = 0, nranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);

    DistributedMesh mesh;
    mesh.rank = rank;
    mesh.nranks = nranks;
    mesh.n_cells_global = global.num_cells();
    mesh.n_faces_global = global.num_faces();
    mesh.n_boundary_faces_global = global.num_boundary_faces();
    mesh.boundary_names = global.boundary_names;
    mesh.partition_edge_cut = part.edge_cut;

    // ------------------------------------------------------------------
    // Rank 0: build per-rank payloads.
    // ------------------------------------------------------------------
    const int64_t nc = global.num_cells();

    // Precompute global cell -> face list on rank 0.
    std::vector<std::vector<int>> cell_face_idx(nc);
    for (int f = 0; f < global.num_faces(); ++f) {
        const auto& g = global.faces[f];
        if (g.left >= 0) cell_face_idx[g.left].push_back(f);
        if (g.right >= 0) cell_face_idx[g.right].push_back(f);
    }

    // Build the owned set per rank.
    std::vector<std::vector<int64_t>> owned_of(nranks);
    std::vector<std::vector<int64_t>> ghost_of(nranks);
    std::vector<std::vector<int>> ghost_owner_of(nranks);
    std::vector<std::vector<Vec2>> ghost_center_of(nranks);
    std::vector<std::vector<double>> ghost_volume_of(nranks);

    if (rank == 0) {
        for (int c = 0; c < static_cast<int>(nc); ++c) {
            const int r = part.cell_part[c];
            owned_of[r].push_back(c);
        }
        for (int r = 0; r < nranks; ++r) {
            std::map<int64_t, int> ghost_set;  // global id -> owner rank
            for (int64_t c : owned_of[r]) {
                for (int f : cell_face_idx[c]) {
                    const auto& g = global.faces[f];
                    if (g.bc != BcType::Interior) continue;
                    const int other = (g.left == c) ? g.right : g.left;
                    if (part.cell_part[other] != r) {
                        ghost_set.emplace(other, part.cell_part[other]);
                    }
                }
            }
            for (const auto& [gid, owner] : ghost_set) {
                ghost_of[r].push_back(gid);
                ghost_owner_of[r].push_back(owner);
                ghost_center_of[r].push_back(global.cells[gid].center);
                ghost_volume_of[r].push_back(global.cells[gid].volume);
            }
        }
    }

    // Scatter the sizes, then the payloads. For simplicity and clarity all
    // ranks get their owned/ghost/face data through point-to-point messages
    // from rank 0 (serial preprocessing is explicitly allowed).
    std::vector<int64_t> owned;
    std::vector<int64_t> ghosts;
    std::vector<int> ghost_owner;
    std::vector<Vec2> ghost_center;
    std::vector<double> ghost_volume;

    if (rank == 0) {
        for (int r = 1; r < nranks; ++r) {
            // owned cells
            int64_t n = owned_of[r].size();
            MPI_Send(&n, 1, MPI_INT64_T, r, 101, comm);
            if (n > 0)
                MPI_Send(owned_of[r].data(), n, MPI_INT64_T, r, 102, comm);
            // ghosts
            n = ghost_of[r].size();
            MPI_Send(&n, 1, MPI_INT64_T, r, 103, comm);
            if (n > 0) {
                MPI_Send(ghost_of[r].data(), n, MPI_INT64_T, r, 104, comm);
                MPI_Send(ghost_owner_of[r].data(), n, MPI_INT, r, 105, comm);
                MPI_Send(ghost_center_of[r].data(), 2 * n, MPI_DOUBLE, r, 106,
                         comm);
                MPI_Send(ghost_volume_of[r].data(), n, MPI_DOUBLE, r, 107, comm);
            }
            // faces of owned cells
            std::vector<double> fgeom;
            std::vector<int64_t> fglob;  // left_global, right_global, bc, name
            for (int64_t c : owned_of[r]) {
                for (int f : cell_face_idx[c]) {
                    const auto& g = global.faces[f];
                    fglob.push_back(g.left);
                    fglob.push_back(g.right);
                    fglob.push_back(static_cast<int64_t>(g.bc));
                    fglob.push_back(g.bc_name_id);
                    fglob.push_back(f);  // global face id for deduplication
                    fgeom.push_back(g.center.x);
                    fgeom.push_back(g.center.y);
                    // Global orientation left -> right (outward for BC faces).
                    fgeom.push_back(g.n.x);
                    fgeom.push_back(g.n.y);
                    fgeom.push_back(g.area);
                }
            }
            n = fglob.size() / 5;
            MPI_Send(&n, 1, MPI_INT64_T, r, 108, comm);
            if (n > 0) {
                MPI_Send(fglob.data(), fglob.size(), MPI_INT64_T, r, 109, comm);
                MPI_Send(fgeom.data(), fgeom.size(), MPI_DOUBLE, r, 110, comm);
            }
        }
        owned = owned_of[0];
        ghosts = ghost_of[0];
        ghost_owner = ghost_owner_of[0];
        ghost_center = ghost_center_of[0];
        ghost_volume = ghost_volume_of[0];
    } else {
        int64_t n = 0;
        MPI_Recv(&n, 1, MPI_INT64_T, 0, 101, comm, MPI_STATUS_IGNORE);
        owned.resize(n);
        if (n > 0)
            MPI_Recv(owned.data(), n, MPI_INT64_T, 0, 102, comm,
                     MPI_STATUS_IGNORE);
        MPI_Recv(&n, 1, MPI_INT64_T, 0, 103, comm, MPI_STATUS_IGNORE);
        ghosts.resize(n);
        ghost_owner.resize(n);
        ghost_center.resize(n);
        ghost_volume.resize(n);
        if (n > 0) {
            MPI_Recv(ghosts.data(), n, MPI_INT64_T, 0, 104, comm,
                     MPI_STATUS_IGNORE);
            MPI_Recv(ghost_owner.data(), n, MPI_INT, 0, 105, comm,
                     MPI_STATUS_IGNORE);
            MPI_Recv(ghost_center.data(), 2 * n, MPI_DOUBLE, 0, 106, comm,
                     MPI_STATUS_IGNORE);
            MPI_Recv(ghost_volume.data(), n, MPI_DOUBLE, 0, 107, comm,
                     MPI_STATUS_IGNORE);
        }
        MPI_Recv(&n, 1, MPI_INT64_T, 0, 108, comm, MPI_STATUS_IGNORE);
        std::vector<int64_t> fglob(5 * n);
        std::vector<double> fgeom(5 * n);
        if (n > 0) {
            MPI_Recv(fglob.data(), fglob.size(), MPI_INT64_T, 0, 109, comm,
                     MPI_STATUS_IGNORE);
            MPI_Recv(fgeom.data(), fgeom.size(), MPI_DOUBLE, 0, 110, comm,
                     MPI_STATUS_IGNORE);
        }
        // Build local faces now (owned ids are sent with tag 111 afterwards;
        // local id assignment uses the same ordering as `owned`).
        std::map<int64_t, int> local_of_global;
        for (size_t i = 0; i < owned.size(); ++i) local_of_global[owned[i]] = i;
        // Ghost local indices start at owned.size(); build that map too.
        for (size_t i = 0; i < ghosts.size(); ++i)
            local_of_global[ghosts[i]] = static_cast<int>(owned.size() + i);
        std::unordered_map<int64_t, bool> seen_faces;
        for (int64_t i = 0; i < n; ++i) {
            if (seen_faces[fglob[5 * i + 4]]) continue;
            seen_faces[fglob[5 * i + 4]] = true;
            LocalFace lf;
            auto lit = local_of_global.find(fglob[5 * i]);
            if (lit == local_of_global.end()) {
                fprintf(stderr,
                        "rank %d: face %lld left global %lld not local "
                        "(owned=%zu ghosts=%zu)\n",
                        rank, static_cast<long long>(i),
                        static_cast<long long>(fglob[5 * i]), owned.size(),
                        ghosts.size());
                throw std::runtime_error("face endpoint not in local map");
            }
            lf.left = lit->second;
            lf.right = fglob[5 * i + 1] >= 0
                           ? local_of_global.at(fglob[5 * i + 1])
                           : -1;
            lf.bc = static_cast<BcType>(fglob[5 * i + 2]);
            lf.bc_name_id = static_cast<int>(fglob[5 * i + 3]);
            lf.center = {fgeom[5 * i], fgeom[5 * i + 1]};
            lf.n = {fgeom[5 * i + 2], fgeom[5 * i + 3]};
            lf.area = fgeom[5 * i + 4];
            mesh.faces.push_back(std::move(lf));
        }
    }

    if (rank == 0) {
        // Build local faces for rank 0 as well.
        std::map<int64_t, int> local_of_global;
        for (size_t i = 0; i < owned.size(); ++i) local_of_global[owned[i]] = i;
        for (size_t i = 0; i < ghosts.size(); ++i)
            local_of_global[ghosts[i]] = static_cast<int>(owned.size() + i);
        std::unordered_map<int64_t, bool> seen_faces;
        for (int64_t c : owned) {
            for (int f : cell_face_idx[c]) {
                if (seen_faces[f]) continue;
                seen_faces[f] = true;
                const auto& g = global.faces[f];
                LocalFace lf;
                lf.left = local_of_global.at(g.left);
                lf.right = g.bc == BcType::Interior
                               ? local_of_global.at(g.right)
                               : -1;
                lf.bc = g.bc;
                lf.bc_name_id = g.bc_name_id;
                lf.center = g.center;
                lf.n = g.n;
                lf.area = g.area;
                mesh.faces.push_back(std::move(lf));
            }
        }
    }

    mesh.n_owned = static_cast<int>(owned.size());
    mesh.n_ghost = static_cast<int>(ghosts.size());
    mesh.n_local = mesh.n_owned + mesh.n_ghost;
    mesh.global_cell_id.resize(mesh.n_local);
    mesh.owner_rank.resize(mesh.n_local);
    mesh.cell_center.resize(mesh.n_local);
    mesh.cell_volume.resize(mesh.n_local);
    mesh.cell_faces.resize(mesh.n_local);
    for (int i = 0; i < mesh.n_owned; ++i) {
        mesh.global_cell_id[i] = owned[i];
        mesh.owner_rank[i] = rank;
    }
    for (int i = 0; i < mesh.n_ghost; ++i) {
        const int li = mesh.n_owned + i;
        mesh.global_cell_id[li] = ghosts[i];
        mesh.owner_rank[li] = ghost_owner[i];
        mesh.cell_center[li] = ghost_center[i];
        mesh.cell_volume[li] = ghost_volume[i];
    }

    // Rank 0 already has geometry; other ranks received ghost geometry but
    // not owned-cell geometry. Rank 0 must send owned cell centers/volumes to
    // the other ranks (or send everything during the face message). Send now.
    if (rank == 0) {
        for (int i = 0; i < mesh.n_owned; ++i) {
            mesh.cell_center[i] = global.cells[owned[i]].center;
            mesh.cell_volume[i] = global.cells[owned[i]].volume;
        }
        for (int r = 1; r < nranks; ++r) {
            std::vector<double> geom(3 * owned_of[r].size());
            for (size_t i = 0; i < owned_of[r].size(); ++i) {
                const auto& gc = global.cells[owned_of[r][i]];
                geom[3 * i] = gc.center.x;
                geom[3 * i + 1] = gc.center.y;
                geom[3 * i + 2] = gc.volume;
            }
            MPI_Send(geom.data(), geom.size(), MPI_DOUBLE, r, 112, comm);
            // Owned-cell corner coordinates (variable counts).
            size_t ncorner_entries = 0;
            for (int64_t c : owned_of[r])
                ncorner_entries += global.cells[c].nodes.size() * 2;
            std::vector<double> corners(ncorner_entries);
            std::vector<int64_t> corner_counts(owned_of[r].size());
            size_t pos = 0;
            for (size_t i = 0; i < owned_of[r].size(); ++i) {
                const auto& gc = global.cells[owned_of[r][i]];
                corner_counts[i] = gc.nodes.size();
                for (int64_t gid : gc.nodes) {
                    corners[pos++] = global.node_x[gid - 1];
                    corners[pos++] = global.node_y[gid - 1];
                }
            }
            int64_t nentries = ncorner_entries;
            MPI_Send(&nentries, 1, MPI_INT64_T, r, 113, comm);
            MPI_Send(corner_counts.data(), corner_counts.size(), MPI_INT64_T,
                     r, 114, comm);
            if (nentries > 0)
                MPI_Send(corners.data(), nentries, MPI_DOUBLE, r, 115, comm);
        }
    } else {
        std::vector<double> geom(3 * owned.size());
        MPI_Recv(geom.data(), geom.size(), MPI_DOUBLE, 0, 112, comm,
                 MPI_STATUS_IGNORE);
        for (size_t i = 0; i < owned.size(); ++i) {
            mesh.cell_center[i] = {geom[3 * i], geom[3 * i + 1]};
            mesh.cell_volume[i] = geom[3 * i + 2];
        }
        int64_t nentries = 0;
        MPI_Recv(&nentries, 1, MPI_INT64_T, 0, 113, comm, MPI_STATUS_IGNORE);
        std::vector<int64_t> corner_counts(owned.size());
        MPI_Recv(corner_counts.data(), corner_counts.size(), MPI_INT64_T, 0,
                 114, comm, MPI_STATUS_IGNORE);
        std::vector<double> corners(nentries);
        if (nentries > 0)
            MPI_Recv(corners.data(), nentries, MPI_DOUBLE, 0, 115, comm,
                     MPI_STATUS_IGNORE);
        size_t pos = 0;
        for (size_t i = 0; i < owned.size(); ++i) {
            std::vector<Vec2> pts(corner_counts[i]);
            for (auto& p : pts) {
                p = {corners[pos], corners[pos + 1]};
                pos += 2;
            }
            mesh.cell_corners.push_back(std::move(pts));
        }
    }

    if (rank == 0) {
        mesh.cell_corners.resize(mesh.n_owned);
        for (int i = 0; i < mesh.n_owned; ++i) {
            for (int64_t gid : global.cells[owned[i]].nodes) {
                mesh.cell_corners[i].push_back(
                    {global.node_x[gid - 1], global.node_y[gid - 1]});
            }
        }
    }

    // Link faces to cells.
    for (size_t f = 0; f < mesh.faces.size(); ++f) {
        mesh.cell_faces[mesh.faces[f].left].push_back(static_cast<int>(f));
        if (mesh.faces[f].right >= 0)
            mesh.cell_faces[mesh.faces[f].right].push_back(static_cast<int>(f));
    }

    // Reorder owned cells by centroid (x then y). The lower/upper split of
    // LU-SGS is defined by local index order, and a flow-aligned ordering
    // makes the asymmetric A^-/A^+ sweeps well-conditioned. Ghost cells keep
    // their indices (all > n_owned), which is the correct "upper" role.
    {
        std::vector<int> old_of_new(mesh.n_owned);
        std::iota(old_of_new.begin(), old_of_new.end(), 0);
        std::stable_sort(old_of_new.begin(), old_of_new.end(),
                         [&](int a, int b) {
                             if (mesh.cell_center[a].x !=
                                 mesh.cell_center[b].x)
                                 return mesh.cell_center[a].x <
                                        mesh.cell_center[b].x;
                             return mesh.cell_center[a].y <
                                    mesh.cell_center[b].y;
                         });
        std::vector<int> new_of_old(mesh.n_owned);
        for (int i = 0; i < mesh.n_owned; ++i)
            new_of_old[old_of_new[i]] = i;
        const auto remap = [&](int id) {
            if (id < 0) return -1;
            return id < mesh.n_owned ? new_of_old[id] : id;
        };
        for (auto& lf : mesh.faces) {
            lf.left = remap(lf.left);
            lf.right = remap(lf.right);
        }
        std::vector<Vec2> center(mesh.n_owned);
        std::vector<double> volume(mesh.n_owned);
        std::vector<int64_t> gcid(mesh.n_owned);
        std::vector<int> owner(mesh.n_owned);
        std::vector<std::vector<Vec2>> corners(mesh.n_owned);
        for (int i = 0; i < mesh.n_owned; ++i) {
            const int old = old_of_new[i];
            center[i] = mesh.cell_center[old];
            volume[i] = mesh.cell_volume[old];
            gcid[i] = mesh.global_cell_id[old];
            owner[i] = mesh.owner_rank[old];
            corners[i] = std::move(mesh.cell_corners[old]);
        }
        for (int i = 0; i < mesh.n_owned; ++i) {
            mesh.cell_center[i] = center[i];
            mesh.cell_volume[i] = volume[i];
            mesh.global_cell_id[i] = gcid[i];
            mesh.owner_rank[i] = owner[i];
            mesh.cell_corners[i] = std::move(corners[i]);
        }
        for (auto& cfl : mesh.cell_faces) cfl.clear();
        for (size_t f = 0; f < mesh.faces.size(); ++f) {
            mesh.cell_faces[mesh.faces[f].left].push_back(
                static_cast<int>(f));
            if (mesh.faces[f].right >= 0)
                mesh.cell_faces[mesh.faces[f].right].push_back(
                    static_cast<int>(f));
        }
        // Halo send lists reference owned indices; remap them.
        for (auto& list : mesh.halo.send_cells)
            for (int& id : list) id = new_of_old[id];
    }

    // ------------------------------------------------------------------
    // Halo request exchange. Each rank announces, to every owner of its ghost
    // cells, which global cell ids it needs. Owners build send lists.
    // ------------------------------------------------------------------
    std::vector<std::vector<int64_t>> requests(nranks);
    for (int i = 0; i < mesh.n_ghost; ++i) {
        requests[ghost_owner[i]].push_back(ghosts[i]);
    }
    // Every rank knows which ranks are its neighbors from the ghost owners.
    std::vector<int> neighbors;
    for (int r = 0; r < nranks; ++r)
        if (!requests[r].empty()) neighbors.push_back(r);
    mesh.halo.neighbor_ranks = neighbors;
    mesh.halo.recv_cells.assign(neighbors.size(), {});
    mesh.halo.send_cells.assign(neighbors.size(), {});
    mesh.halo.send_buf.assign(neighbors.size(), {});
    mesh.halo.recv_buf.assign(neighbors.size(), {});

    std::unordered_map<int64_t, int> owned_index;
    for (int i = 0; i < mesh.n_owned; ++i) owned_index[owned[i]] = i;
    for (size_t nb = 0; nb < neighbors.size(); ++nb) {
        const int neighbor = neighbors[nb];
        for (int i = 0; i < mesh.n_ghost; ++i) {
            if (ghost_owner[i] == neighbor) {
                const int li = mesh.n_owned + i;
                mesh.halo.recv_cells[nb].push_back(li);
            }
        }
        mesh.halo.recv_buf[nb].resize(mesh.halo.recv_cells[nb].size() *
                                      kGhostPayload);
    }

    // Exchange halo requests: each rank tells each owner which of its cells
    // are needed as ghosts; owners compile their send lists from the received
    // requests. Tags are symmetric on the (min,max) rank pair.
    for (size_t nb = 0; nb < neighbors.size(); ++nb) {
        const int neighbor = neighbors[nb];
        const int tag = 30000 + std::min(rank, neighbor) * nranks +
                        std::max(rank, neighbor);
        int n_send = static_cast<int>(requests[neighbor].size());
        int n_recv = 0;
        MPI_Sendrecv(&n_send, 1, MPI_INT, neighbor, tag, &n_recv, 1, MPI_INT,
                     neighbor, tag, comm, MPI_STATUS_IGNORE);
        std::vector<int64_t> want(n_recv);
        MPI_Sendrecv(requests[neighbor].data(), n_send, MPI_INT64_T, neighbor,
                     tag + 1, want.data(), n_recv, MPI_INT64_T, neighbor,
                     tag + 1, comm, MPI_STATUS_IGNORE);
        mesh.halo.send_cells[nb].reserve(n_recv);
        for (int64_t gid : want) {
            const auto it = owned_index.find(gid);
            if (it == owned_index.end()) {
                throw std::runtime_error(
                    "halo request for a cell not owned by this rank");
            }
            mesh.halo.send_cells[nb].push_back(it->second);
        }
        mesh.halo.send_buf[nb].resize(
            mesh.halo.send_cells[nb].size() * kGhostPayload);
    }

    // ------------------------------------------------------------------
    // Geometry-only spectral coefficients for local pseudo time stepping.
    // Convective part uses freestream sound speed; the factor is later
    // multiplied by cell volume to form lambda_i * V / lambda_total.
    // ------------------------------------------------------------------
    mesh.conv_radius.assign(mesh.n_local, 0.0);
    mesh.visc_radius.assign(mesh.n_local, 0.0);
    for (int c = 0; c < mesh.n_owned; ++c) {
        for (int f : mesh.cell_faces[c]) {
            mesh.conv_radius[c] += mesh.faces[f].area;
            mesh.visc_radius[c] +=
                mesh.faces[f].area * mesh.faces[f].area;
        }
    }
    // Ghost cells receive the same quantities for safety (not strictly
    // required because ghosts never own faces in the local list).

    mesh.num_boundary_faces_local = 0;
    for (const auto& f : mesh.faces)
        if (f.bc != BcType::Interior) ++mesh.num_boundary_faces_local;

    return mesh;
}

void begin_halo_exchange(DistributedMesh& mesh, const std::vector<double>& U,
                         const std::vector<double>& grad) {
    const int n_neighbors = static_cast<int>(mesh.halo.neighbor_ranks.size());
    mesh.halo.requests.assign(n_neighbors, MPI_REQUEST_NULL);
    // Symmetric message tags: both sides of a halo edge derive the tag from
    // the (min,max) rank pair so Irecv/Isend match regardless of the local
    // ordering of neighbor lists.
    const auto halo_tag = [&](int other) {
        const int lo = std::min(mesh.rank, other);
        const int hi = std::max(mesh.rank, other);
        return 20000 + lo * mesh.nranks + hi;
    };
    for (int nb = 0; nb < n_neighbors; ++nb) {
        auto& buf = mesh.halo.recv_buf[nb];
        if (!buf.empty()) {
            MPI_Irecv(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE,
                      mesh.halo.neighbor_ranks[nb],
                      halo_tag(mesh.halo.neighbor_ranks[nb]), MPI_COMM_WORLD,
                      &mesh.halo.requests[nb]);
        }
    }
    for (int nb = 0; nb < n_neighbors; ++nb) {
        auto& buf = mesh.halo.send_buf[nb];
        if (!buf.empty()) {
            for (size_t j = 0; j < mesh.halo.send_cells[nb].size(); ++j) {
                pack_payload(mesh.halo.send_cells[nb][j], U, grad,
                             buf.data() + j * kGhostPayload);
            }
            MPI_Isend(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE,
                      mesh.halo.neighbor_ranks[nb],
                      halo_tag(mesh.halo.neighbor_ranks[nb]), MPI_COMM_WORLD,
                      &mesh.halo.requests[nb]);
        }
    }
}

void end_halo_exchange(DistributedMesh& mesh, std::vector<double>& U,
                       std::vector<double>& grad) {
    const int n_neighbors = static_cast<int>(mesh.halo.neighbor_ranks.size());
    if (n_neighbors > 0) {
        MPI_Waitall(n_neighbors, mesh.halo.requests.data(), MPI_STATUSES_IGNORE);
    }
    for (int nb = 0; nb < n_neighbors; ++nb) {
        const auto& buf = mesh.halo.recv_buf[nb];
        for (size_t j = 0; j < mesh.halo.recv_cells[nb].size(); ++j) {
            unpack_payload(mesh.halo.recv_cells[nb][j],
                           buf.data() + j * kGhostPayload, U, grad);
        }
    }
}

}  // namespace cfd
