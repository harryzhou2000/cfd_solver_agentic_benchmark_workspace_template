#include "partition.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "metis.h"

namespace cfd {

namespace {

// ---------------------------------------------------------------------------
// Minimal binary writer for partition files.
// ---------------------------------------------------------------------------
class BinWriter {
   public:
    explicit BinWriter(const std::string& path) : fp_(std::fopen(path.c_str(), "wb")) {
        if (!fp_) throw std::runtime_error("cannot open partition file for writing: " + path);
    }
    ~BinWriter() {
        if (fp_) std::fclose(fp_);
    }
    void raw(const void* p, size_t n) {
        if (std::fwrite(p, 1, n, fp_) != n)
            throw std::runtime_error("partition file write failed");
    }
    template <typename T>
    void put(const T& v) {
        raw(&v, sizeof(T));
    }
    void str(const std::string& s) {
        int n = (int)s.size();
        put(n);
        raw(s.data(), n);
    }

   private:
    FILE* fp_;
};

class BinReader {
   public:
    explicit BinReader(const std::string& path) : fp_(std::fopen(path.c_str(), "rb")) {
        if (!fp_) throw std::runtime_error("cannot open partition file for reading: " + path);
    }
    ~BinReader() {
        if (fp_) std::fclose(fp_);
    }
    void raw(void* p, size_t n) {
        if (std::fread(p, 1, n, fp_) != n)
            throw std::runtime_error("partition file read failed (truncated)");
    }
    template <typename T>
    T get() {
        T v;
        raw(&v, sizeof(T));
        return v;
    }
    std::string str() {
        int n = get<int>();
        std::string s(n, '\0');
        raw(s.data(), n);
        return s;
    }

   private:
    FILE* fp_;
};

}  // namespace

PartitionInfo write_partition_files(const GlobalMesh& gm, int n_ranks,
                                    const std::string& outdir) {
    int ncells = gm.num_cells_global;

    // ------------------------------------------------------------------
    // 1. Build METIS cell adjacency graph from shared faces.
    // ------------------------------------------------------------------
    std::vector<idx_t> xadj(ncells + 1, 0);
    std::vector<idx_t> adjncy;
    adjncy.reserve(2 * (size_t)gm.faces.size());
    for (int c = 0; c < ncells; ++c) {
        for (int fid : gm.cell_faces[c]) {
            const auto& f = gm.faces[fid];
            int nb = (f.c0 == c) ? f.c1 : f.c0;
            if (nb >= 0) adjncy.push_back(nb);
        }
        xadj[c + 1] = (idx_t)adjncy.size();
    }

    std::vector<idx_t> part(ncells, 0);
    idx_t edge_cut = 0;
    if (n_ranks > 1) {
        idx_t nvtx = ncells;
        idx_t ncon = 1;
        idx_t nparts = n_ranks;
        idx_t objval = 0;
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_NUMBERING] = 0;  // 0-based
        options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
        options[METIS_OPTION_CONTIG] = 1;
        int ret = METIS_PartGraphKway(&nvtx, &ncon, xadj.data(), adjncy.data(),
                                      nullptr, nullptr, nullptr, &nparts,
                                      nullptr, nullptr, options, &objval, part.data());
        if (ret != METIS_OK) {
            throw std::runtime_error("METIS_PartGraphKway failed");
        }
        edge_cut = objval;
    }

    // ------------------------------------------------------------------
    // 2. Compute ghosts and send lists per rank.
    // ------------------------------------------------------------------
    // ghost[rank] = list of (global cell id, owner rank)
    std::vector<std::vector<std::pair<int, int>>> ghosts(n_ranks);
    // send[rank][other] = global ids of owned cells that `other` needs as ghosts
    std::vector<std::vector<std::set<int>>> send(n_ranks);
    for (int r = 0; r < n_ranks; ++r) send[r].resize(n_ranks);

    for (const auto& f : gm.faces) {
        if (f.c1 < 0) continue;
        int r0 = part[f.c0], r1 = part[f.c1];
        if (r0 == r1) continue;
        ghosts[r0].emplace_back(f.c1, r1);
        ghosts[r1].emplace_back(f.c0, r0);
        send[r1][r0].insert(f.c0);
        send[r0][r1].insert(f.c1);
    }

    // ------------------------------------------------------------------
    // 3. Write per-rank files.
    // ------------------------------------------------------------------
    for (int r = 0; r < n_ranks; ++r) {
        // Owned cells of rank r (sorted by global id).
        std::vector<int> owned;
        for (int c = 0; c < ncells; ++c)
            if (part[c] == r) owned.push_back(c);
        std::sort(owned.begin(), owned.end());

        // Ghost cells sorted by global id; assign local indices after owned.
        std::sort(ghosts[r].begin(), ghosts[r].end());
        std::unordered_map<int, int> ghost_local;  // global id -> local ghost index
        for (size_t g = 0; g < ghosts[r].size(); ++g) {
            ghost_local[ghosts[r][g].first] = (int)g;
        }
        int n_owned = (int)owned.size();
        int n_ghost = (int)ghosts[r].size();

        // Local node numbering for owned cells.
        std::unordered_map<int, int> local_node;  // global node id -> local id
        std::vector<int> node_global;
        std::vector<double> node_x, node_y;
        for (int c : owned) {
            for (int g : gm.cell_nodes[c]) {
                if (local_node.find(g) == local_node.end()) {
                    local_node[g] = (int)node_global.size();
                    node_global.push_back(g);
                    node_x.push_back(gm.node_x[g]);
                    node_y.push_back(gm.node_y[g]);
                }
            }
        }

        // Faces.
        struct FaceRec {
            int c0, c1;
            double nx, ny, len, fx, fy;
        };
        struct BFaceRec {
            int c;
            int b0, b1;
            double nx, ny, len, fx, fy;
            int bc;
            int global_face_id;
            std::string family;
        };
        std::vector<FaceRec> faces;
        std::vector<BFaceRec> bfaces;
        std::unordered_map<int, int> face_local;  // global face id -> local face id
        // Pass 1: internal faces.
        for (const auto& f : gm.faces) {
            if (f.global_face_id == 10337) {
            }
            int pr0 = part[f.c0];
            int pr1 = (f.c1 >= 0) ? part[f.c1] : -1;
            if (f.c1 < 0 || (pr0 != r && pr1 != r)) continue;
            if (f.global_face_id == 10337) {
            }
            FaceRec fc;
            if (pr0 == r) {
                fc.c0 = (int)(std::lower_bound(owned.begin(), owned.end(), f.c0) - owned.begin());
                fc.c1 = (pr1 == r)
                            ? (int)(std::lower_bound(owned.begin(), owned.end(), f.c1) -
                                    owned.begin())
                            : n_owned + ghost_local.at(f.c1);
            } else {
                fc.c0 = n_owned + ghost_local.at(f.c0);
                fc.c1 = (int)(std::lower_bound(owned.begin(), owned.end(), f.c1) -
                              owned.begin());
            }
            fc.nx = f.nx;
            fc.ny = f.ny;
            fc.len = f.len;
            fc.fx = 0.5 * (gm.node_x[f.b0] + gm.node_x[f.b1]);
            fc.fy = 0.5 * (gm.node_y[f.b0] + gm.node_y[f.b1]);
            int local_fid = (int)faces.size();
            face_local[f.global_face_id] = local_fid;
            faces.push_back(fc);
        }
        // Pass 2: boundary faces.
        for (const auto& f : gm.faces) {
            if (f.c1 >= 0) continue;
            if (part[f.c0] != r) continue;
            BFaceRec b;
            b.c = (int)(std::lower_bound(owned.begin(), owned.end(), f.c0) - owned.begin());
            b.b0 = local_node.at(f.b0);
            b.b1 = local_node.at(f.b1);
            b.nx = f.nx;
            b.ny = f.ny;
            b.len = f.len;
            b.fx = 0.5 * (gm.node_x[f.b0] + gm.node_x[f.b1]);
            b.fy = 0.5 * (gm.node_y[f.b0] + gm.node_y[f.b1]);
            b.bc = (int)f.bc;
            b.global_face_id = f.global_face_id;
            b.family = f.family;
            int local_bfid = (int)faces.size() + (int)bfaces.size();
            face_local[f.global_face_id] = local_bfid;
            bfaces.push_back(b);
        }

        // Neighbor ranks and send/recv lists.
        std::vector<int> nbrs;
        std::vector<std::vector<int>> send_ids, recv_ghost_idx;
        for (int s = 0; s < n_ranks; ++s) {
            bool has_send = !send[r][s].empty();
            bool has_recv = std::any_of(ghosts[r].begin(), ghosts[r].end(),
                                        [s](auto& p) { return p.second == s; });
            if (!has_send && !has_recv) continue;
            nbrs.push_back(s);
            send_ids.emplace_back(send[r][s].begin(), send[r][s].end());
            std::sort(send_ids.back().begin(), send_ids.back().end());
            recv_ghost_idx.emplace_back();
            for (size_t g = 0; g < ghosts[r].size(); ++g) {
                if (ghosts[r][g].second == s) recv_ghost_idx.back().push_back((int)g);
            }
        }

        // ------------------------------------------------------------------
        // Write file.
        // ------------------------------------------------------------------
        char fname[512];
        std::snprintf(fname, sizeof(fname), "%s/partition_rank_%d.bin", outdir.c_str(), r);
        BinWriter w(fname);
        w.str("CFDPART1");
        w.put(n_owned);
        w.put(n_ghost);
        w.put(gm.num_cells_global);
        w.put(gm.num_faces_global);
        w.put(gm.num_nodes_global);
        w.put((int)edge_cut);
        w.put((int)node_global.size());
        for (size_t i = 0; i < node_global.size(); ++i) {
            w.put(node_global[i]);
            w.put(node_x[i]);
            w.put(node_y[i]);
        }
        for (int c : owned) {
            w.put(c);
            w.put(gm.cell_cx[c]);
            w.put(gm.cell_cy[c]);
            w.put(gm.cell_vol[c]);
            w.put((int)gm.cell_nodes[c].size());
            for (int n : gm.cell_nodes[c]) w.put(local_node.at(n));
        }
        for (size_t g = 0; g < ghosts[r].size(); ++g) {
            int gid = ghosts[r][g].first;
            w.put(gid);
            w.put(ghosts[r][g].second);
            w.put(gm.cell_cx[gid]);
            w.put(gm.cell_cy[gid]);
        }
        w.put((int)faces.size());
        for (auto& f : faces) {
            w.put(f.c0);
            w.put(f.c1);
            w.put(f.nx);
            w.put(f.ny);
            w.put(f.len);
            w.put(f.fx);
            w.put(f.fy);
        }
        w.put((int)bfaces.size());
        for (auto& b : bfaces) {
            w.put(b.c);
            w.put(b.b0);
            w.put(b.b1);
            w.put(b.nx);
            w.put(b.ny);
            w.put(b.len);
            w.put(b.fx);
            w.put(b.fy);
            w.put(b.bc);
            w.put(b.global_face_id);
            w.str(b.family);
        }
        w.put((int)nbrs.size());
        for (size_t k = 0; k < nbrs.size(); ++k) {
            w.put(nbrs[k]);
            w.put((int)send_ids[k].size());
            for (int gid : send_ids[k]) w.put(gid);
            w.put((int)recv_ghost_idx[k].size());
            for (int gi : recv_ghost_idx[k]) w.put(gi);
        }
    // cell -> face lists for owned cells
    for (int c : owned) {
        w.put((int)gm.cell_faces[c].size());
        for (int fid : gm.cell_faces[c]) {
            auto it = face_local.find(fid);
            if (it == face_local.end()) {
                const auto& f = gm.faces[fid];
                std::fprintf(stderr, "ERROR: face %d c0=%d c1=%d bc=%d\n", fid, f.c0, f.c1, (int)f.bc);
                std::fprintf(stderr, "ERROR: face %d not found in face_local for cell %d\n", fid, c);
                throw std::runtime_error("face_local missing");
            }
            w.put(it->second);
        }
    }
    }

    PartitionInfo info;
    int owned_rank0 = 0;
    for (int c = 0; c < ncells; ++c)
        if (part[c] == 0) owned_rank0++;
    info.n_owned = owned_rank0;
    info.num_cells_global = gm.num_cells_global;
    info.num_faces_global = gm.num_faces_global;
    info.num_nodes_global = gm.num_nodes_global;
    info.edge_cut = (int)edge_cut;
    return info;
}

}  // namespace cfd
