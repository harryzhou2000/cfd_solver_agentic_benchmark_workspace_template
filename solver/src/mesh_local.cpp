#include "mesh_local.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

namespace cfd {

namespace {

class BinReader {
   public:
    explicit BinReader(const std::string& path) : fp_(std::fopen(path.c_str(), "rb")) {
        if (!fp_) throw std::runtime_error("cannot open partition file: " + path);
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

LocalMesh load_local_mesh(const std::string& partition_file) {
    BinReader r(partition_file);
    std::string magic = r.str();
    if (magic != "CFDPART1") throw std::runtime_error("bad partition file magic");

    LocalMesh m;
    m.n_owned = r.get<int>();
    m.n_ghost = r.get<int>();
    m.num_cells_global = r.get<int>();
    m.num_faces_global = r.get<int>();
    m.num_nodes_global = r.get<int>();
    m.edge_cut = r.get<int>();

    int nnodes = r.get<int>();
    m.node_x.resize(nnodes);
    m.node_y.resize(nnodes);
    m.node_global_id.resize(nnodes);
    for (int i = 0; i < nnodes; ++i) {
        m.node_global_id[i] = r.get<int>();
        m.node_x[i] = r.get<double>();
        m.node_y[i] = r.get<double>();
    }

    m.owned_global_id.resize(m.n_owned);
    m.cell_cx.resize(m.n_owned);
    m.cell_cy.resize(m.n_owned);
    m.cell_vol.resize(m.n_owned);
    m.cell_nodes_local.resize(m.n_owned);
    m.cell_type.resize(m.n_owned);
    for (int c = 0; c < m.n_owned; ++c) {
        m.owned_global_id[c] = r.get<int>();
        m.cell_cx[c] = r.get<double>();
        m.cell_cy[c] = r.get<double>();
        m.cell_vol[c] = r.get<double>();
        int nn = r.get<int>();
        m.cell_type[c] = nn;
        m.cell_nodes_local[c].resize(nn);
        for (int k = 0; k < nn; ++k) m.cell_nodes_local[c][k] = r.get<int>();
    }

    m.ghost_global_id.resize(m.n_ghost);
    m.ghost_owner.resize(m.n_ghost);
    m.ghost_cx.resize(m.n_ghost);
    m.ghost_cy.resize(m.n_ghost);
    for (int g = 0; g < m.n_ghost; ++g) {
        m.ghost_global_id[g] = r.get<int>();
        m.ghost_owner[g] = r.get<int>();
        m.ghost_cx[g] = r.get<double>();
        m.ghost_cy[g] = r.get<double>();
    }

    int nfaces = r.get<int>();
    m.faces.resize(nfaces);
    for (int i = 0; i < nfaces; ++i) {
        auto& f = m.faces[i];
        f.c0 = r.get<int>();
        f.c1 = r.get<int>();
        f.nx = r.get<double>();
        f.ny = r.get<double>();
        f.len = r.get<double>();
        f.fx = r.get<double>();
        f.fy = r.get<double>();
    }

    int nbfaces = r.get<int>();
    m.bfaces.resize(nbfaces);
    for (int i = 0; i < nbfaces; ++i) {
        auto& b = m.bfaces[i];
        b.c = r.get<int>();
        b.b0 = r.get<int>();
        b.b1 = r.get<int>();
        b.nx = r.get<double>();
        b.ny = r.get<double>();
        b.len = r.get<double>();
        b.fx = r.get<double>();
        b.fy = r.get<double>();
        b.bc = (BCType)r.get<int>();
        b.global_face_id = r.get<int>();
        b.family = r.str();
    }

    int nnbr = r.get<int>();
    m.neighbor_ranks.resize(nnbr);
    m.send_cells.resize(nnbr);
    m.recv_ghosts.resize(nnbr);
    for (int k = 0; k < nnbr; ++k) {
        m.neighbor_ranks[k] = r.get<int>();
        int ns = r.get<int>();
        m.send_cells[k].resize(ns);
        for (int i = 0; i < ns; ++i) m.send_cells[k][i] = r.get<int>();
        int nr = r.get<int>();
        m.recv_ghosts[k].resize(nr);
        for (int i = 0; i < nr; ++i) m.recv_ghosts[k][i] = r.get<int>();
    }

    m.cell_faces.resize(m.n_owned);
    for (int c = 0; c < m.n_owned; ++c) {
        int nf = r.get<int>();
        m.cell_faces[c].resize(nf);
        for (int k = 0; k < nf; ++k) m.cell_faces[c][k] = r.get<int>();
    }

    // Build neighbor lists for reconstruction.
    m.cell_neighbors.resize(m.n_owned);
    for (int c = 0; c < m.n_owned; ++c) {
        std::vector<int> nbrs;
        for (int fid : m.cell_faces[c]) {
            if (fid < m.n_faces_internal()) {
                const auto& f = m.faces[fid];
                nbrs.push_back((f.c0 == c) ? f.c1 : f.c0);
            }
        }
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
        m.cell_neighbors[c] = std::move(nbrs);
    }

    return m;
}

}  // namespace cfd
