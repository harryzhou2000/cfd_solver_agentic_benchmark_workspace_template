#include "distribute.hpp"

#include <metis.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace {

struct RankMeshData {
    std::vector<int> owned;               // global cell ids
    std::vector<int> ghosts;              // global cell ids
    std::unordered_map<int, int> g2l;     // global cell -> local cell
    std::vector<int> nodes;               // global node ids used locally
    std::unordered_map<int, int> gn2l;    // global node -> local node
    std::vector<int> face_ids;            // global face ids included locally
    std::map<int, std::vector<int>> halo_send; // neighbor rank -> local owned cell ids
    std::map<int, std::vector<int>> halo_recv; // neighbor rank -> local ghost cell ids
};

void pack_string(std::vector<char>& buf, const std::string& s) {
    int n = (int)s.size();
    size_t off = buf.size();
    buf.resize(off + sizeof(int) + n);
    std::memcpy(buf.data() + off, &n, sizeof(int));
    std::memcpy(buf.data() + off + sizeof(int), s.data(), n);
}

template <typename T>
void pack_vec(std::vector<char>& buf, const std::vector<T>& v) {
    long long n = (long long)v.size();
    size_t off = buf.size();
    buf.resize(off + sizeof(long long) + n * sizeof(T));
    std::memcpy(buf.data() + off, &n, sizeof(long long));
    if (n) std::memcpy(buf.data() + off + sizeof(long long), v.data(), n * sizeof(T));
}

struct Unpacker {
    const char* p;
    template <typename T>
    void vec(std::vector<T>& v) {
        long long n;
        std::memcpy(&n, p, sizeof(long long));
        p += sizeof(long long);
        v.resize(n);
        if (n) std::memcpy(v.data(), p, n * sizeof(T));
        p += n * sizeof(T);
    }
    std::string str() {
        int n;
        std::memcpy(&n, p, sizeof(int));
        p += sizeof(int);
        std::string s(p, n);
        p += n;
        return s;
    }
};

} // namespace

LocalMesh distribute_mesh(MPI_Comm comm, const SerialMesh& gm,
                          const std::map<std::string, BCType>& bc_map,
                          PartitionInfo& pinfo) {
    int rank, nranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);

    // broadcast family names so every rank can map bc_id -> name
    std::vector<char> fam_buf;
    if (rank == 0) {
        long long nf = (long long)gm.bc_names.size();
        fam_buf.resize(sizeof(long long));
        std::memcpy(fam_buf.data(), &nf, sizeof(long long));
        for (const auto& s : gm.bc_names) pack_string(fam_buf, s);
    }
    long long fam_size = (long long)fam_buf.size();
    MPI_Bcast(&fam_size, 1, MPI_LONG_LONG, 0, comm);
    fam_buf.resize(fam_size);
    MPI_Bcast(fam_buf.data(), (int)fam_size, MPI_CHAR, 0, comm);
    std::vector<std::string> bc_names;
    {
        Unpacker u{fam_buf.data()};
        long long nf;
        std::memcpy(&nf, u.p, sizeof(long long));
        u.p += sizeof(long long);
        for (long long i = 0; i < nf; i++) bc_names.push_back(u.str());
    }
    // validate the bc map on every rank
    for (const auto& name : bc_names) {
        if (bc_map.find(name) == bc_map.end()) {
            throw std::runtime_error("mesh boundary family '" + name +
                                     "' has no entry in the case boundary_conditions map");
        }
    }

    std::vector<char> my_buf;
    long edge_cut = 0;

    if (rank == 0) {
        const int nc = gm.num_cells;
        std::vector<int> part(nc, 0);
        if (nranks > 1) {
            idx_t nvtxs = nc, ncon = 1, nparts = nranks, objval = 0;
            std::vector<idx_t> xadj(gm.xadj.begin(), gm.xadj.end());
            std::vector<idx_t> adjncy(gm.adjncy.begin(), gm.adjncy.end());
            std::vector<idx_t> mp(nc);
            idx_t options[METIS_NOPTIONS];
            METIS_SetDefaultOptions(options);
            options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
            int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                         nullptr, nullptr, nullptr, &nparts, nullptr,
                                         nullptr, options, &objval, mp.data());
            if (rc != METIS_OK)
                throw std::runtime_error("METIS_PartGraphKway failed with code " +
                                         std::to_string(rc));
            for (int i = 0; i < nc; i++) part[i] = (int)mp[i];
            edge_cut = objval;
        }

        // owner rank per cell for ghost lookup
        std::vector<RankMeshData> rmd(nranks);
        for (int c = 0; c < nc; c++) rmd[part[c]].owned.push_back(c);

        // ghost layer: any face-neighbor of an owned cell owned by another rank
        for (int r = 0; r < nranks; r++) {
            auto& D = rmd[r];
            std::set<int> ghost_set;
            std::set<int> owned_set(D.owned.begin(), D.owned.end());
            for (const auto& f : gm.faces) {
                if (f.c1 < 0) continue;
                bool o0 = owned_set.count(f.c0), o1 = owned_set.count(f.c1);
                if (o0 && !o1) ghost_set.insert(f.c1);
                if (o1 && !o0) ghost_set.insert(f.c0);
            }
            D.ghosts.assign(ghost_set.begin(), ghost_set.end());
            int lid = 0;
            for (int g : D.owned) D.g2l[g] = lid++;
            for (int g : D.ghosts) D.g2l[g] = lid++;

            // local node set
            std::set<int> node_set;
            for (int g : D.owned)
                for (int k = 0; k < gm.cell_nnodes[g]; k++) node_set.insert(gm.cell_nodes[g][k]);
            for (int g : D.ghosts)
                for (int k = 0; k < gm.cell_nnodes[g]; k++) node_set.insert(gm.cell_nodes[g][k]);
            D.nodes.assign(node_set.begin(), node_set.end());
            for (size_t i = 0; i < D.nodes.size(); i++) D.gn2l[D.nodes[i]] = (int)i;

            // faces touching at least one owned cell
            for (int fi = 0; fi < gm.num_faces; fi++) {
                const auto& f = gm.faces[fi];
                bool o0 = f.c0 >= 0 && owned_set.count(f.c0);
                bool o1 = f.c1 >= 0 && owned_set.count(f.c1);
                if (o0 || o1) D.face_ids.push_back(fi);
            }

            // halo maps
            std::map<int, std::vector<int>> send_g, recv_g;
            for (const auto& f : gm.faces) {
                if (f.c1 < 0) continue;
                bool o0 = owned_set.count(f.c0), o1 = owned_set.count(f.c1);
                if (o0 && !o1 && f.c1 >= 0) send_g[part[f.c1]].push_back(D.g2l[f.c0]);
                if (o1 && !o0) send_g[part[f.c0]].push_back(D.g2l[f.c1]);
            }
            for (int g : D.ghosts) recv_g[part[g]].push_back(D.g2l[g]);
            // deduplicate sends (a cell may neighbor the same rank twice)
            for (auto& [q, v] : send_g) {
                std::sort(v.begin(), v.end());
                v.erase(std::unique(v.begin(), v.end()), v.end());
                // order send list to match the receiver's recv list: sort by
                // global cell id on both sides
                std::sort(v.begin(), v.end(), [&](int a, int b) {
                    return D.owned[a] < D.owned[b];
                });
            }
            for (auto& [q, v] : recv_g) {
                std::sort(v.begin(), v.end(), [&](int a, int b) {
                    return D.ghosts[a - (int)D.owned.size()] < D.ghosts[b - (int)D.owned.size()];
                });
            }
            D.halo_send = std::move(send_g);
            D.halo_recv = std::move(recv_g);
        }

        // sanity: send/recv counts must agree pairwise
        for (int r = 0; r < nranks; r++) {
            for (auto& [q, v] : rmd[r].halo_send) {
                auto it = rmd[q].halo_recv.find(r);
                if (it == rmd[q].halo_recv.end() || it->second.size() != v.size())
                    throw std::runtime_error("halo send/recv mismatch between ranks " +
                                             std::to_string(r) + " and " + std::to_string(q));
            }
        }

        pinfo.edge_cut = edge_cut;
        pinfo.owned_per_rank.resize(nranks);
        pinfo.ghost_per_rank.resize(nranks);
        for (int r = 0; r < nranks; r++) {
            pinfo.owned_per_rank[r] = (int)rmd[r].owned.size();
            pinfo.ghost_per_rank[r] = (int)rmd[r].ghosts.size();
        }

        // serialize each rank's mesh
        for (int r = 0; r < nranks; r++) {
            auto& D = rmd[r];
            std::vector<char> buf;
            int n_own = (int)D.owned.size();
            int n_cell = n_own + (int)D.ghosts.size();
            int n_node = (int)D.nodes.size();
            int n_face = (int)D.face_ids.size();
            std::vector<int> header{n_own, n_cell, n_node, n_face};
            pack_vec(buf, header);

            std::vector<long long> cg(n_cell);
            for (int i = 0; i < n_own; i++) cg[i] = D.owned[i];
            for (size_t i = 0; i < D.ghosts.size(); i++) cg[n_own + i] = D.ghosts[i];
            pack_vec(buf, cg);

            std::vector<int> cnn(n_cell), cn(n_cell * 4);
            for (int i = 0; i < n_cell; i++) {
                int g = (int)cg[i];
                cnn[i] = gm.cell_nnodes[g];
                for (int k = 0; k < 4; k++)
                    cn[i * 4 + k] = (k < cnn[i]) ? D.gn2l[gm.cell_nodes[g][k]] : -1;
            }
            pack_vec(buf, cnn);
            pack_vec(buf, cn);

            std::vector<double> xs(n_node), ys(n_node);
            std::vector<long long> ng(n_node);
            for (int i = 0; i < n_node; i++) {
                xs[i] = gm.node_x[D.nodes[i]];
                ys[i] = gm.node_y[D.nodes[i]];
                ng[i] = D.nodes[i];
            }
            pack_vec(buf, xs);
            pack_vec(buf, ys);
            pack_vec(buf, ng);

            // faces: c0, c1 (local), n1, n2 (local), bc_id
            std::vector<int> fc0(n_face), fc1(n_face), fn1(n_face), fn2(n_face), fbc(n_face);
            for (int i = 0; i < n_face; i++) {
                const auto& f = gm.faces[D.face_ids[i]];
                fc0[i] = D.g2l[f.c0];
                fc1[i] = (f.c1 >= 0) ? D.g2l[f.c1] : -1;
                fn1[i] = D.gn2l[f.n1];
                fn2[i] = D.gn2l[f.n2];
                fbc[i] = f.bc_id;
            }
            pack_vec(buf, fc0);
            pack_vec(buf, fc1);
            pack_vec(buf, fn1);
            pack_vec(buf, fn2);
            pack_vec(buf, fbc);

            // halos
            int n_halo = (int)D.halo_send.size();
            std::vector<int> hh{n_halo};
            pack_vec(buf, hh);
            for (auto& [q, sv] : D.halo_send) {
                std::vector<int> hhead{q, (int)sv.size(), (int)D.halo_recv[q].size()};
                pack_vec(buf, hhead);
                pack_vec(buf, sv);
                pack_vec(buf, D.halo_recv[q]);
            }

            if (r == 0) {
                my_buf = std::move(buf);
            } else {
                long long sz = (long long)buf.size();
                MPI_Send(&sz, 1, MPI_LONG_LONG, r, 0, comm);
                MPI_Send(buf.data(), (int)sz, MPI_CHAR, r, 1, comm);
            }
        }
    } else {
        long long sz;
        MPI_Recv(&sz, 1, MPI_LONG_LONG, 0, 0, comm, MPI_STATUS_IGNORE);
        my_buf.resize(sz);
        MPI_Recv(my_buf.data(), (int)sz, MPI_CHAR, 0, 1, comm, MPI_STATUS_IGNORE);
    }
    MPI_Bcast(&edge_cut, 1, MPI_LONG, 0, comm);

    // unpack local mesh
    LocalMesh lm;
    lm.rank = rank;
    lm.nranks = nranks;
    Unpacker u{my_buf.data()};
    std::vector<int> header;
    u.vec(header);
    lm.n_own = header[0];
    lm.n_cell = header[1];
    lm.n_node = header[2];
    int n_face = header[3];
    u.vec(lm.cell_global);
    u.vec(lm.cell_nnodes);
    std::vector<int> cn;
    u.vec(cn);
    lm.cell_nodes.resize(lm.n_cell);
    for (int i = 0; i < lm.n_cell; i++)
        for (int k = 0; k < 4; k++) lm.cell_nodes[i][k] = cn[i * 4 + k];
    u.vec(lm.node_x);
    u.vec(lm.node_y);
    u.vec(lm.node_global);
    std::vector<int> fc0, fc1, fn1, fn2, fbc;
    u.vec(fc0);
    u.vec(fc1);
    u.vec(fn1);
    u.vec(fn2);
    u.vec(fbc);
    std::vector<int> hh;
    u.vec(hh);
    int n_halo = hh[0];
    lm.halos.resize(n_halo);
    for (int h = 0; h < n_halo; h++) {
        std::vector<int> hhead;
        u.vec(hhead);
        lm.halos[h].neighbor = hhead[0];
        u.vec(lm.halos[h].send_cells);
        u.vec(lm.halos[h].recv_cells);
    }

    // cell geometry
    lm.cell_cx.resize(lm.n_cell);
    lm.cell_cy.resize(lm.n_cell);
    lm.cell_vol.resize(lm.n_cell);
    for (int i = 0; i < lm.n_cell; i++) {
        int nn = lm.cell_nnodes[i];
        double cx = 0, cy = 0, area = 0;
        for (int k = 0; k < nn; k++) {
            int a = lm.cell_nodes[i][k];
            int b = lm.cell_nodes[i][(k + 1) % nn];
            double xa = lm.node_x[a], ya = lm.node_y[a];
            double xb = lm.node_x[b], yb = lm.node_y[b];
            cx += xa; cy += ya;
            area += 0.5 * (xa * yb - xb * ya);
        }
        lm.cell_cx[i] = cx / nn;
        lm.cell_cy[i] = cy / nn;
        lm.cell_vol[i] = std::abs(area);
        if (lm.cell_vol[i] <= 0.0)
            throw std::runtime_error("degenerate cell volume on rank " + std::to_string(rank));
    }

    // face geometry (normal from c0 toward c1 / outward for boundary faces)
    lm.faces.resize(n_face);
    lm.n_bface = 0;
    for (int i = 0; i < n_face; i++) {
        auto& f = lm.faces[i];
        f.c0 = fc0[i];
        f.c1 = fc1[i];
        f.n1 = fn1[i];
        f.n2 = fn2[i];
        double x1 = lm.node_x[f.n1], y1 = lm.node_y[f.n1];
        double x2 = lm.node_x[f.n2], y2 = lm.node_y[f.n2];
        double dx = x2 - x1, dy = y2 - y1;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0) throw std::runtime_error("degenerate face on rank " + std::to_string(rank));
        double nx = dy / len, ny = -dx / len;
        double fx = 0.5 * (x1 + x2), fy = 0.5 * (y1 + y2);
        // orient from c0 to c1 (or outward for boundary)
        double tx, ty;
        if (f.c1 >= 0) {
            tx = lm.cell_cx[f.c1];
            ty = lm.cell_cy[f.c1];
        } else {
            tx = fx + (fx - lm.cell_cx[f.c0]);
            ty = fy + (fy - lm.cell_cy[f.c0]);
        }
        double dot = nx * (tx - lm.cell_cx[f.c0]) + ny * (ty - lm.cell_cy[f.c0]);
        if (dot < 0.0) { nx = -nx; ny = -ny; }
        f.nx = nx; f.ny = ny;
        f.area = len;
        f.cx = fx; f.cy = fy;
        if (f.c1 < 0) {
            int bc_id = fbc[i];
            f.bc_name = (bc_id >= 0 && bc_id < (int)bc_names.size()) ? bc_names[bc_id] : "";
            auto it = bc_map.find(f.bc_name);
            if (it == bc_map.end())
                throw std::runtime_error("boundary family '" + f.bc_name +
                                         "' missing from case boundary_conditions");
            f.bc = it->second;
            lm.n_bface++;
        } else {
            f.bc = BCType::Interior;
        }
    }

    // verify boundary face normals point out of the fluid domain
    {
        double min_dot = 1e300;
        for (auto& f : lm.faces) {
            if (f.c1 >= 0) continue;
            double d = (f.cx - lm.cell_cx[f.c0]) * f.nx + (f.cy - lm.cell_cy[f.c0]) * f.ny;
            min_dot = std::min(min_dot, d);
        }
        double gmin;
        MPI_Allreduce(&min_dot, &gmin, 1, MPI_DOUBLE, MPI_MIN, comm);
        if (rank == 0)
            std::printf("[mesh] min boundary face outward distance: %.3e\n", gmin);
        if (gmin <= 0.0)
            throw std::runtime_error("boundary face normal points into the fluid");
    }

    // gather per-rank diagnostics on rank 0
    {
        int my_vals[4] = {lm.n_own, lm.n_cell - lm.n_own, lm.n_bface, (int)lm.halos.size()};
        std::vector<int> all;
        if (rank == 0) all.resize(nranks * 4);
        MPI_Gather(my_vals, 4, MPI_INT, all.data(), 4, MPI_INT, 0, comm);
        int send_total = 0, recv_total = 0;
        for (auto& h : lm.halos) {
            send_total += (int)h.send_cells.size();
            recv_total += (int)h.recv_cells.size();
        }
        int my_sr[2] = {send_total, recv_total};
        std::vector<int> all_sr;
        if (rank == 0) all_sr.resize(nranks * 2);
        MPI_Gather(my_sr, 2, MPI_INT, all_sr.data(), 2, MPI_INT, 0, comm);

        // neighbor lists: variable length; gather counts then data
        std::vector<int> my_neighbors;
        for (auto& h : lm.halos) my_neighbors.push_back(h.neighbor);
        int my_nn = (int)my_neighbors.size();
        std::vector<int> nn_counts;
        if (rank == 0) nn_counts.resize(nranks);
        MPI_Gather(&my_nn, 1, MPI_INT, nn_counts.data(), 1, MPI_INT, 0, comm);
        std::vector<int> displs, all_neighbors;
        if (rank == 0) {
            displs.resize(nranks + 1, 0);
            for (int r = 0; r < nranks; r++) displs[r + 1] = displs[r] + nn_counts[r];
            all_neighbors.resize(displs[nranks]);
        }
        MPI_Gatherv(my_neighbors.data(), my_nn, MPI_INT, all_neighbors.data(),
                    nn_counts.data(), displs.data(), MPI_INT, 0, comm);

        if (rank == 0) {
            pinfo.edge_cut = edge_cut;
            pinfo.owned_per_rank.resize(nranks);
            pinfo.ghost_per_rank.resize(nranks);
            pinfo.bface_per_rank.resize(nranks);
            pinfo.num_neighbors.resize(nranks);
            pinfo.neighbor_ranks.resize(nranks);
            pinfo.send_cells.resize(nranks);
            pinfo.recv_cells.resize(nranks);
            for (int r = 0; r < nranks; r++) {
                pinfo.owned_per_rank[r] = all[r * 4 + 0];
                pinfo.ghost_per_rank[r] = all[r * 4 + 1];
                pinfo.bface_per_rank[r] = all[r * 4 + 2];
                pinfo.num_neighbors[r] = all[r * 4 + 3];
                pinfo.send_cells[r] = all_sr[r * 2 + 0];
                pinfo.recv_cells[r] = all_sr[r * 2 + 1];
                for (int k = displs[r]; k < displs[r + 1]; k++)
                    pinfo.neighbor_ranks[r].push_back(all_neighbors[k]);
            }
        }
    }
    return lm;
}

// defined in local_mesh.hpp
std::vector<int> LocalMesh::wall_faces(const std::string& family) const {
    std::vector<int> out;
    for (int i = 0; i < (int)faces.size(); i++)
        if (faces[i].c1 < 0 && faces[i].bc_name == family) out.push_back(i);
    return out;
}
