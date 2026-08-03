#include "solver/partition.hpp"

#include <metis.h>
#include <fmt/core.h>
#include <mpi.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace solver {

namespace {

// ---------------------------------------------------------------------------
// METIS-based partitioning
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Per-rank local mesh data. Built on rank 0 for every rank (including rank 0
// itself), then either assembled directly (rank 0) or serialized over MPI
// (ranks > 0).
// ---------------------------------------------------------------------------
struct RankMeshData {
    int num_global = 0;

    std::vector<int> owned_gids;  // global cell ids, ascending
    std::vector<Vec2> owned_centroids;
    std::vector<double> owned_volumes;
    std::vector<std::vector<int>> owned_face_ids;  // local face indices

    std::vector<int> ghost_gids;  // global cell ids, ascending
    std::vector<Vec2> ghost_centroids;
    std::vector<double> ghost_volumes;

    std::vector<int> face_left;   // global cell id (or -1 for boundary)
    std::vector<int> face_right;  // global cell id (or -1 for boundary)
    std::vector<Vec2> face_normals;
    std::vector<double> face_areas;
    std::vector<Vec2> face_centroids;
    std::vector<int> face_bc_type;  // local family index or -1

    std::vector<BCFamily> bc_families;  // face_ids are LOCAL face indices
    std::vector<NeighborInfo> neighbors;
};

// Build the complete local mesh data for one rank from the global mesh and
// the partition assignment. Faces are processed in global order so that the
// send/recv index lists of neighboring ranks stay positionally aligned.
RankMeshData build_rank_mesh(const Mesh& mesh,
                             const std::vector<int>& partition, int rank) {
    const int num_cells = mesh.num_cells;
    RankMeshData d;
    d.num_global = num_cells;

    // 1. Owned cells (ascending global id order).
    std::vector<int> owned_local_of_global(num_cells, -1);
    for (int c = 0; c < num_cells; ++c) {
        if (partition[c] == rank) {
            owned_local_of_global[c] = static_cast<int>(d.owned_gids.size());
            d.owned_gids.push_back(c);
            d.owned_centroids.push_back(mesh.cells[c].centroid);
            d.owned_volumes.push_back(mesh.cells[c].volume);
        }
    }

    // 2. Ghost cells: unique cells of other ranks adjacent to owned cells.
    std::set<int> ghost_set;
    for (int c = 0; c < num_cells; ++c) {
        if (partition[c] != rank) continue;
        for (int fid : mesh.cells[c].face_ids) {
            const Face& f = mesh.faces[fid];
            const int o = (f.cell_ids[0] == c) ? f.cell_ids[1] : f.cell_ids[0];
            if (o >= 0 && partition[o] != rank) ghost_set.insert(o);
        }
    }
    std::map<int, int> ghost_local_of_global;
    d.ghost_gids.assign(ghost_set.begin(), ghost_set.end());
    for (size_t i = 0; i < d.ghost_gids.size(); ++i) {
        const int g = d.ghost_gids[i];
        ghost_local_of_global[g] = static_cast<int>(i);
        d.ghost_centroids.push_back(mesh.cells[g].centroid);
        d.ghost_volumes.push_back(mesh.cells[g].volume);
    }

    // 3. Interface faces: iterate all global interior faces in order. For
    //    each face spanning two ranks, BOTH cells cross the boundary:
    //      - cell a is sent by its owner (pa) and received as a ghost by pb
    //      - cell b is sent by its owner (pb) and received as a ghost by pa
    //    All index lists are built in the same global face order, so the
    //    k-th send of one rank always matches the k-th recv of the other.
    std::map<int, std::vector<int>> send_of_neigh;  // neighbor -> owned local idx
    std::map<int, std::vector<int>> recv_of_neigh;  // neighbor -> ghost idx
    for (const Face& f : mesh.faces) {
        const int a = f.cell_ids[0];
        const int b = f.cell_ids[1];
        if (a < 0 || b < 0) continue;
        const int pa = partition[a];
        const int pb = partition[b];
        if (pa == pb) continue;
        if (pa == rank) send_of_neigh[pb].push_back(owned_local_of_global[a]);
        if (pb == rank) recv_of_neigh[pa].push_back(ghost_local_of_global[a]);
        if (pb == rank) send_of_neigh[pa].push_back(owned_local_of_global[b]);
        if (pa == rank) recv_of_neigh[pb].push_back(ghost_local_of_global[b]);
    }

    // 4. Local faces: every global face touching at least one owned cell.
    std::map<int, int> local_face_of_global;
    for (int fid = 0; fid < mesh.num_faces; ++fid) {
        const Face& f = mesh.faces[fid];
        const int a = f.cell_ids[0];
        const int b = f.cell_ids[1];
        const bool touches = (partition[a] == rank) ||
                             (b >= 0 && partition[b] == rank);
        if (!touches) continue;
        local_face_of_global[fid] = static_cast<int>(d.face_left.size());
        d.face_left.push_back(a);
        d.face_right.push_back(b);
        d.face_normals.push_back(f.normal);
        d.face_areas.push_back(f.area);
        d.face_centroids.push_back(f.centroid);
        d.face_bc_type.push_back(f.bc_type);  // global family index for now
    }

    // 5. Owned cell face lists, remapped to local face indices.
    d.owned_face_ids.resize(d.owned_gids.size());
    for (int c = 0; c < num_cells; ++c) {
        if (partition[c] != rank) continue;
        const int li = owned_local_of_global[c];
        for (int fid : mesh.cells[c].face_ids) {
            d.owned_face_ids[li].push_back(local_face_of_global[fid]);
        }
    }

    // 6. BC families: keep only those touching owned cells; remap face ids
    //    and face bc_type to local family indices.
    std::map<int, int> fam_local_of_global;
    for (size_t gi = 0; gi < mesh.bc_families.size(); ++gi) {
        const BCFamily& fam = mesh.bc_families[gi];
        BCFamily lf;
        lf.name = fam.name;
        lf.bc_type = fam.bc_type;
        for (int fid : fam.face_ids) {
            auto it = local_face_of_global.find(fid);
            if (it != local_face_of_global.end()) {
                lf.face_ids.push_back(it->second);
            }
        }
        if (!lf.face_ids.empty()) {
            fam_local_of_global[gi] = static_cast<int>(d.bc_families.size());
            d.bc_families.push_back(std::move(lf));
        }
    }
    for (int& bt : d.face_bc_type) {
        if (bt >= 0) {
            auto it = fam_local_of_global.find(bt);
            bt = (it != fam_local_of_global.end()) ? it->second : -1;
        }
    }

    // 7. Neighbors, ordered by rank for determinism.
    for (auto& kv : send_of_neigh) {
        const int nr = kv.first;
        NeighborInfo ni;
        ni.rank = nr;
        ni.send_indices = std::move(kv.second);
        auto rit = recv_of_neigh.find(nr);
        ni.recv_indices = (rit != recv_of_neigh.end())
                              ? std::move(rit->second)
                              : std::vector<int>();
        d.neighbors.push_back(std::move(ni));
    }
    return d;
}

// Validate that send/recv index lists are consistent across each rank pair:
// rank r must send exactly as many cells to rank nr as rank nr expects to
// receive from r, and vice versa. The lists themselves stay aligned because
// they are built from the same global face iteration.
void validate_neighbors(const std::vector<RankMeshData>& all) {
    for (size_t r = 0; r < all.size(); ++r) {
        for (const NeighborInfo& ni : all[r].neighbors) {
            const int nr = ni.rank;
            const NeighborInfo* other = nullptr;
            for (const NeighborInfo& oni : all[nr].neighbors) {
                if (oni.rank == static_cast<int>(r)) {
                    other = &oni;
                    break;
                }
            }
            if (other == nullptr) {
                throw std::runtime_error(
                    "build_distributed_mesh: rank " + std::to_string(r) +
                    " lists rank " + std::to_string(nr) +
                    " as neighbor but not vice versa");
            }
            if (ni.send_indices.size() != other->recv_indices.size() ||
                ni.recv_indices.size() != other->send_indices.size()) {
                throw std::runtime_error(
                    "build_distributed_mesh: inconsistent neighbor index "
                    "lists between rank " +
                    std::to_string(r) + " and rank " + std::to_string(nr));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MPI serialization helpers (tag is passed by reference and incremented).
// ---------------------------------------------------------------------------
void send_ints(MPI_Comm comm, int dest, int& tag, const std::vector<int>& v) {
    const int n = static_cast<int>(v.size());
    MPI_Send(&n, 1, MPI_INT, dest, tag++, comm);
    if (n > 0) MPI_Send(v.data(), n, MPI_INT, dest, tag++, comm);
}

std::vector<int> recv_ints(MPI_Comm comm, int src, int& tag) {
    int n = 0;
    MPI_Recv(&n, 1, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
    std::vector<int> v(n);
    if (n > 0) {
        MPI_Recv(v.data(), n, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
    }
    return v;
}

void send_doubles(MPI_Comm comm, int dest, int& tag, const std::vector<double>& v) {
    const int n = static_cast<int>(v.size());
    MPI_Send(&n, 1, MPI_INT, dest, tag++, comm);
    if (n > 0) MPI_Send(v.data(), n, MPI_DOUBLE, dest, tag++, comm);
}

std::vector<double> recv_doubles(MPI_Comm comm, int src, int& tag) {
    int n = 0;
    MPI_Recv(&n, 1, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
    std::vector<double> v(n);
    if (n > 0) {
        MPI_Recv(v.data(), n, MPI_DOUBLE, src, tag++, comm, MPI_STATUS_IGNORE);
    }
    return v;
}

std::vector<double> flatten_vec2(const std::vector<Vec2>& v) {
    std::vector<double> out;
    out.reserve(2 * v.size());
    for (const Vec2& p : v) {
        out.push_back(p.x());
        out.push_back(p.y());
    }
    return out;
}

std::vector<Vec2> unflatten_vec2(const std::vector<double>& v) {
    std::vector<Vec2> out;
    out.reserve(v.size() / 2);
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
        out.emplace_back(v[i], v[i + 1]);
    }
    return out;
}

void send_strings(MPI_Comm comm, int dest, int& tag,
                  const std::vector<std::string>& ss) {
    const int n = static_cast<int>(ss.size());
    MPI_Send(&n, 1, MPI_INT, dest, tag++, comm);
    for (const std::string& s : ss) {
        const int len = static_cast<int>(s.size());
        MPI_Send(&len, 1, MPI_INT, dest, tag++, comm);
        if (len > 0) MPI_Send(s.data(), len, MPI_CHAR, dest, tag++, comm);
    }
}

std::vector<std::string> recv_strings(MPI_Comm comm, int src, int& tag) {
    int n = 0;
    MPI_Recv(&n, 1, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
    std::vector<std::string> ss(n);
    for (int i = 0; i < n; ++i) {
        int len = 0;
        MPI_Recv(&len, 1, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
        ss[i].resize(len);
        if (len > 0) {
            MPI_Recv(ss[i].data(), len, MPI_CHAR, src, tag++, comm,
                     MPI_STATUS_IGNORE);
        }
    }
    return ss;
}

// Send one rank's local mesh data (rank 0 -> dest).
void send_rank_mesh(MPI_Comm comm, int dest, const RankMeshData& d) {
    int tag = 0;
    // Header: global cell count + all section sizes.
    int nface_ids = 0;
    for (const auto& ids : d.owned_face_ids) nface_ids += ids.size();
    int nfam_faces = 0;
    for (const BCFamily& f : d.bc_families) nfam_faces += f.face_ids.size();
    int nsend = 0;
    int nrecv = 0;
    for (const NeighborInfo& ni : d.neighbors) {
        nsend += ni.send_indices.size();
        nrecv += ni.recv_indices.size();
    }
    const std::vector<int> header = {
        d.num_global, static_cast<int>(d.owned_gids.size()),
        static_cast<int>(d.ghost_gids.size()),
        static_cast<int>(d.face_left.size()),
        static_cast<int>(d.bc_families.size()),
        static_cast<int>(d.neighbors.size()),
        nface_ids,
        nfam_faces,
        nsend,
        nrecv};
    MPI_Send(header.data(), static_cast<int>(header.size()), MPI_INT, dest,
             tag++, comm);

    send_ints(comm, dest, tag, d.owned_gids);
    send_doubles(comm, dest, tag, flatten_vec2(d.owned_centroids));
    send_doubles(comm, dest, tag, d.owned_volumes);
    {
        std::vector<int> counts;
        std::vector<int> ids;
        counts.reserve(d.owned_face_ids.size());
        for (const auto& cids : d.owned_face_ids) {
            counts.push_back(static_cast<int>(cids.size()));
            ids.insert(ids.end(), cids.begin(), cids.end());
        }
        send_ints(comm, dest, tag, counts);
        send_ints(comm, dest, tag, ids);
    }
    send_ints(comm, dest, tag, d.ghost_gids);
    send_doubles(comm, dest, tag, flatten_vec2(d.ghost_centroids));
    send_doubles(comm, dest, tag, d.ghost_volumes);
    send_ints(comm, dest, tag, d.face_left);
    send_ints(comm, dest, tag, d.face_right);
    send_doubles(comm, dest, tag, flatten_vec2(d.face_normals));
    send_doubles(comm, dest, tag, d.face_areas);
    send_doubles(comm, dest, tag, flatten_vec2(d.face_centroids));
    send_ints(comm, dest, tag, d.face_bc_type);
    {
        std::vector<std::string> names;
        std::vector<std::string> types;
        names.reserve(d.bc_families.size());
        types.reserve(d.bc_families.size());
        for (const BCFamily& f : d.bc_families) {
            names.push_back(f.name);
            types.push_back(f.bc_type);
        }
        send_strings(comm, dest, tag, names);
        send_strings(comm, dest, tag, types);
    }
    {
        std::vector<int> counts;
        std::vector<int> ids;
        counts.reserve(d.bc_families.size());
        for (const BCFamily& f : d.bc_families) {
            counts.push_back(static_cast<int>(f.face_ids.size()));
            ids.insert(ids.end(), f.face_ids.begin(), f.face_ids.end());
        }
        send_ints(comm, dest, tag, counts);
        send_ints(comm, dest, tag, ids);
    }
    {
        std::vector<int> ranks;
        std::vector<int> scounts;
        std::vector<int> sends;
        std::vector<int> rcounts;
        std::vector<int> recvs;
        ranks.reserve(d.neighbors.size());
        for (const NeighborInfo& ni : d.neighbors) {
            ranks.push_back(ni.rank);
            scounts.push_back(static_cast<int>(ni.send_indices.size()));
            sends.insert(sends.end(), ni.send_indices.begin(),
                         ni.send_indices.end());
            rcounts.push_back(static_cast<int>(ni.recv_indices.size()));
            recvs.insert(recvs.end(), ni.recv_indices.begin(),
                         ni.recv_indices.end());
        }
        send_ints(comm, dest, tag, ranks);
        send_ints(comm, dest, tag, scounts);
        send_ints(comm, dest, tag, sends);
        send_ints(comm, dest, tag, rcounts);
        send_ints(comm, dest, tag, recvs);
    }
}

// Receive one rank's local mesh data (from rank 0).
RankMeshData recv_rank_mesh(MPI_Comm comm, int src) {
    int tag = 0;
    int header[10];
    MPI_Recv(header, 10, MPI_INT, src, tag++, comm, MPI_STATUS_IGNORE);
    RankMeshData d;
    d.num_global = header[0];
    const int num_owned = header[1];
    const int num_ghost = header[2];
    const int num_faces = header[3];
    const int num_fams = header[4];
    const int num_neigh = header[5];

    d.owned_gids = recv_ints(comm, src, tag);
    d.owned_centroids = unflatten_vec2(recv_doubles(comm, src, tag));
    d.owned_volumes = recv_doubles(comm, src, tag);
    {
        const std::vector<int> counts = recv_ints(comm, src, tag);
        const std::vector<int> ids = recv_ints(comm, src, tag);
        d.owned_face_ids.resize(num_owned);
        size_t off = 0;
        for (int i = 0; i < num_owned; ++i) {
            d.owned_face_ids[i].assign(ids.begin() + off,
                                       ids.begin() + off + counts[i]);
            off += counts[i];
        }
    }
    d.ghost_gids = recv_ints(comm, src, tag);
    d.ghost_centroids = unflatten_vec2(recv_doubles(comm, src, tag));
    d.ghost_volumes = recv_doubles(comm, src, tag);
    d.face_left = recv_ints(comm, src, tag);
    d.face_right = recv_ints(comm, src, tag);
    d.face_normals = unflatten_vec2(recv_doubles(comm, src, tag));
    d.face_areas = recv_doubles(comm, src, tag);
    d.face_centroids = unflatten_vec2(recv_doubles(comm, src, tag));
    d.face_bc_type = recv_ints(comm, src, tag);
    {
        const std::vector<std::string> names = recv_strings(comm, src, tag);
        const std::vector<std::string> types = recv_strings(comm, src, tag);
        d.bc_families.resize(num_fams);
        for (int i = 0; i < num_fams; ++i) {
            d.bc_families[i].name = names[i];
            d.bc_families[i].bc_type = types[i];
        }
    }
    {
        const std::vector<int> counts = recv_ints(comm, src, tag);
        const std::vector<int> ids = recv_ints(comm, src, tag);
        size_t off = 0;
        for (int i = 0; i < num_fams; ++i) {
            d.bc_families[i].face_ids.assign(ids.begin() + off,
                                             ids.begin() + off + counts[i]);
            off += counts[i];
        }
    }
    {
        const std::vector<int> ranks = recv_ints(comm, src, tag);
        const std::vector<int> scounts = recv_ints(comm, src, tag);
        const std::vector<int> sends = recv_ints(comm, src, tag);
        const std::vector<int> rcounts = recv_ints(comm, src, tag);
        const std::vector<int> recvs = recv_ints(comm, src, tag);
        d.neighbors.resize(num_neigh);
        size_t so = 0;
        size_t ro = 0;
        for (int i = 0; i < num_neigh; ++i) {
            d.neighbors[i].rank = ranks[i];
            d.neighbors[i].send_indices.assign(
                sends.begin() + so, sends.begin() + so + scounts[i]);
            so += scounts[i];
            d.neighbors[i].recv_indices.assign(
                recvs.begin() + ro, recvs.begin() + ro + rcounts[i]);
            ro += rcounts[i];
        }
    }
    if (d.owned_gids.size() != static_cast<size_t>(num_owned) ||
        d.ghost_gids.size() != static_cast<size_t>(num_ghost) ||
        d.face_left.size() != static_cast<size_t>(num_faces)) {
        throw std::runtime_error(
            "build_distributed_mesh: received mesh data size mismatch");
    }
    return d;
}

// Assemble a DistributedMesh from local mesh data (used on every rank).
DistributedMesh assemble_distributed(const RankMeshData& d, int rank,
                                     int num_ranks, MPI_Comm comm) {
    DistributedMesh dm;
    dm.rank = rank;
    dm.num_ranks = num_ranks;
    dm.comm = comm;
    dm.global_to_local.assign(d.num_global, -1);

    // Owned cells.
    const int num_owned = static_cast<int>(d.owned_gids.size());
    dm.owned_cells.resize(num_owned);
    for (int i = 0; i < num_owned; ++i) {
        dm.owned_cells[i].face_ids = d.owned_face_ids[i];
        dm.owned_cells[i].centroid = d.owned_centroids[i];
        dm.owned_cells[i].volume = d.owned_volumes[i];
        dm.global_to_local[d.owned_gids[i]] = i;
    }

    // Ghost cells.
    const int num_ghost = static_cast<int>(d.ghost_gids.size());
    dm.ghost_cells.resize(num_ghost);
    for (int j = 0; j < num_ghost; ++j) {
        dm.ghost_cells[j].centroid = d.ghost_centroids[j];
        dm.ghost_cells[j].volume = d.ghost_volumes[j];
        dm.global_to_local[d.ghost_gids[j]] = num_owned + j;
    }

    // Local faces: convert global cell ids to local (owned+ghost) indices.
    const int num_faces = static_cast<int>(d.face_left.size());
    dm.local_faces.resize(num_faces);
    dm.face_left.resize(num_faces);
    dm.face_right.resize(num_faces);
    auto to_local = [&dm](int gid) {
        return (gid >= 0 && gid < static_cast<int>(dm.global_to_local.size()))
                   ? dm.global_to_local[gid]
                   : -1;
    };
    for (int i = 0; i < num_faces; ++i) {
        const int ll = to_local(d.face_left[i]);
        const int lr = to_local(d.face_right[i]);
        dm.face_left[i] = ll;
        dm.face_right[i] = lr;
        dm.local_faces[i].cell_ids = {ll, lr};
        dm.local_faces[i].normal = d.face_normals[i];
        dm.local_faces[i].area = d.face_areas[i];
        dm.local_faces[i].centroid = d.face_centroids[i];
        dm.local_faces[i].bc_type = d.face_bc_type[i];
    }

    dm.bc_families = d.bc_families;
    dm.neighbors = d.neighbors;
    return dm;
}

} // namespace

std::vector<int> partition_mesh(const CellGraph& graph, int num_cells,
                                int nparts) {
    if (nparts <= 1) {
        // METIS_PartGraphKway divides by (nparts - 1); single-partition
        // cases are handled directly.
        return std::vector<int>(num_cells, 0);
    }
    idx_t nvtxs = static_cast<idx_t>(num_cells);
    idx_t ncon = 1;
    idx_t nparts_ = static_cast<idx_t>(nparts);

    // METIS wants non-const pointers; copy input arrays.
    std::vector<idx_t> xadj(graph.xadj.begin(), graph.xadj.end());
    std::vector<idx_t> adjncy(graph.adjncy.begin(), graph.adjncy.end());

    std::vector<idx_t> options(METIS_NOPTIONS);
    METIS_SetDefaultOptions(options.data());
    options[METIS_OPTION_UFACTOR] = 30;
    options[METIS_OPTION_NCUTS] = 5;
    options[METIS_OPTION_SEED] = 42;

    std::vector<idx_t> part(num_cells);
    idx_t edgecut = 0;

    const int status =
        METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr,
                            nullptr, nullptr, &nparts_, nullptr, nullptr,
                            options.data(), &edgecut, part.data());

    if (status != METIS_OK) {
        fmt::print(stderr, "METIS_PartGraphKway failed: {}\n", status);
        std::exit(1);
    }

    return std::vector<int>(part.begin(), part.end());
}

DistributedMesh build_distributed_mesh(const Mesh& global_mesh,
                                       const std::vector<int>& partition,
                                       MPI_Comm comm) {
    int rank = 0;
    int num_ranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &num_ranks);

    if (rank == 0) {
        if (global_mesh.num_cells == 0) {
            throw std::runtime_error(
                "build_distributed_mesh: empty global mesh on rank 0");
        }
        // Build the local mesh data for every rank, send to ranks > 0.
        std::vector<RankMeshData> all(num_ranks);
        for (int r = 0; r < num_ranks; ++r) {
            all[r] = build_rank_mesh(global_mesh, partition, r);
        }
        validate_neighbors(all);
        for (int r = 1; r < num_ranks; ++r) {
            send_rank_mesh(comm, r, all[r]);
        }
        return assemble_distributed(all[0], rank, num_ranks, comm);
    }

    RankMeshData d = recv_rank_mesh(comm, 0);
    return assemble_distributed(d, rank, num_ranks, comm);
}

} // namespace solver
