#include "partition.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>

#include <metis.h>
#include <nlohmann/json.hpp>

namespace cfd {

namespace {

std::string part_dir(const std::string& out_dir, int n_ranks) {
  return out_dir + "/partitions/np" + std::to_string(n_ranks);
}

bool file_exists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}

void make_dirs(const std::string& p) {
  // Simple recursive mkdir -p.
  std::string cur;
  for (char ch : p) {
    cur += ch;
    if (ch == '/') mkdir(cur.c_str(), 0755);
  }
  mkdir(p.c_str(), 0755);
}

template <typename T>
void write_vec(std::ofstream& f, const std::vector<T>& v) {
  f.write(reinterpret_cast<const char*>(v.data()),
          static_cast<std::streamsize>(v.size() * sizeof(T)));
}
template <typename T>
void read_vec(std::ifstream& f, std::vector<T>& v, size_t n) {
  v.resize(n);
  f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T)));
  if (!f)
    throw std::runtime_error("truncated or corrupt partition file (read_vec)");
}

struct PartHeader {
  char magic[16];
  int32_t rank, n_ranks, n_owned, n_ghost, n_nodes, n_faces;
  int32_t n_cell_node_list, n_cells_global, n_faces_global, n_nodes_global;
  int32_t edge_cut, n_bc_names, n_nbr, n_cf;
};

}  // namespace

bool preprocess_partition(const std::string& mesh_file, int n_ranks,
                          const std::string& out_dir, std::string* error) {
  const std::string dir = part_dir(out_dir, n_ranks);
  if (file_exists(dir + "/DONE")) {
    // Cache hit: validate that the cached partition matches the requested
    // mesh and rank count before reusing it. A stale cache (e.g. the same
    // output directory reused with a different mesh) must be rebuilt,
    // otherwise ranks would silently load the wrong mesh partition.
    bool valid = false;
    try {
      std::ifstream gf(dir + "/global.json");
      nlohmann::json g;
      gf >> g;
      valid = g.value("mesh_file", std::string()) == mesh_file &&
              g.value("n_ranks", -1) == n_ranks;
    } catch (...) {
      valid = false;
    }
    if (valid) return false;
    // Stale cache: remove and fall through to a full rebuild.
    std::filesystem::remove_all(dir);
  }

  try {
    GlobalMesh gm = read_cgns_mesh(mesh_file);

    // ---- METIS cell-graph partitioning ----
    std::vector<int> part(gm.n_cells, 0);
    int edge_cut = 0;
    if (n_ranks > 1) {
      idx_t nvtxs = gm.n_cells, ncon = 1, nparts = n_ranks, objval = 0;
      std::vector<idx_t> xadj(gm.adj_start.begin(), gm.adj_start.end());
      std::vector<idx_t> adjncy(gm.adj_list.begin(), gm.adj_list.end());
      std::vector<idx_t> opts(METIS_NOPTIONS);
      METIS_SetDefaultOptions(opts.data());
      opts[METIS_OPTION_NUMBERING] = 0;
      std::vector<idx_t> ppart(gm.n_cells);
      int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr,
                                   nullptr, nullptr, &nparts, nullptr, nullptr,
                                   opts.data(), &objval, ppart.data());
      if (rc != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
      for (int i = 0; i < gm.n_cells; ++i) part[i] = ppart[i];
      edge_cut = objval;
    }

    // ---- Per-rank extraction ----
    // Global cell -> part lookups and per-rank owned/ghost sets.
    std::vector<std::vector<int>> owned_of(n_ranks);
    for (int c = 0; c < gm.n_cells; ++c) owned_of[part[c]].push_back(c);
    std::vector<std::map<int, std::set<int>>> ghost_of(n_ranks);  // owner rank -> gids
    for (int f = 0; f < gm.n_faces_internal; ++f) {
      int l = gm.face_cell_l[f], r = gm.face_cell_r[f];
      if (part[l] != part[r]) {
        ghost_of[part[l]][part[r]].insert(r);
        ghost_of[part[r]][part[l]].insert(l);
      }
    }

    make_dirs(dir);
    nlohmann::json ginfo;
    ginfo["mesh_file"] = mesh_file;
    ginfo["n_ranks"] = n_ranks;
    ginfo["num_cells_global"] = gm.n_cells;
    ginfo["num_nodes_global"] = gm.n_nodes;
    ginfo["num_faces_global"] = gm.n_faces_internal + gm.n_faces_boundary;
    ginfo["num_internal_faces_global"] = gm.n_faces_internal;
    ginfo["num_boundary_faces_global"] = gm.n_faces_boundary;
    ginfo["partitioner"] = n_ranks > 1 ? "metis_kway" : "metis_kway (single part)";
    ginfo["edge_cut"] = edge_cut;
    ginfo["bc_families"] = gm.bc_family_names;

    for (int r = 0; r < n_ranks; ++r) {
      LocalMesh lm;
      lm.rank = r;
      lm.n_ranks = n_ranks;
      lm.bc_names = gm.bc_family_names;
      lm.n_owned = static_cast<int>(owned_of[r].size());

      // Global -> local cell numbering: owned ascending, ghosts by (rank, gid).
      std::unordered_map<int, int> g2l;
      for (int i = 0; i < lm.n_owned; ++i) g2l[owned_of[r][i]] = i;
      std::vector<int> ghost_gid;
      std::vector<int> ghost_owner;
      for (const auto& kv : ghost_of[r])
        for (int g : kv.second) {
          g2l[g] = lm.n_owned + static_cast<int>(ghost_gid.size());
          ghost_gid.push_back(g);
          ghost_owner.push_back(kv.first);
        }
      lm.n_ghost = static_cast<int>(ghost_gid.size());
      lm.n_cells = lm.n_owned + lm.n_ghost;
      lm.cell_global.resize(lm.n_cells);
      for (int i = 0; i < lm.n_owned; ++i) lm.cell_global[i] = owned_of[r][i];
      for (int i = 0; i < lm.n_ghost; ++i) lm.cell_global[lm.n_owned + i] = ghost_gid[i];

      // Cell geometry and connectivity.
      lm.cx.resize(lm.n_cells); lm.cy.resize(lm.n_cells); lm.vol.resize(lm.n_cells);
      for (int i = 0; i < lm.n_cells; ++i) {
        int g = lm.cell_global[i];
        lm.cx[i] = gm.cell_cx[g]; lm.cy[i] = gm.cell_cy[g]; lm.vol[i] = gm.cell_vol[g];
      }

      // Local nodes.
      std::unordered_map<int, int> gn2l;
      for (int i = 0; i < lm.n_cells; ++i) {
        int g = lm.cell_global[i];
        int nn = gm.cell_nnodes[g];
        for (int k = 0; k < nn; ++k) {
          int gn = gm.cell_nodes[g][k];
          if (gn2l.find(gn) == gn2l.end()) {
            int lid = static_cast<int>(gn2l.size());
            gn2l[gn] = lid;
            lm.node_x.push_back(gm.node_x[gn]);
            lm.node_y.push_back(gm.node_y[gn]);
            lm.node_global.push_back(gn);
          }
        }
      }
      lm.n_nodes = static_cast<int>(lm.node_x.size());
      lm.cell_node_start.assign(lm.n_cells + 1, 0);
      for (int i = 0; i < lm.n_cells; ++i)
        lm.cell_node_start[i + 1] = lm.cell_node_start[i] + gm.cell_nnodes[lm.cell_global[i]];
      lm.cell_node_list.resize(lm.cell_node_start[lm.n_cells]);
      for (int i = 0; i < lm.n_cells; ++i) {
        int g = lm.cell_global[i];
        int off = lm.cell_node_start[i];
        for (int k = 0; k < gm.cell_nnodes[g]; ++k)
          lm.cell_node_list[off + k] = gn2l[gm.cell_nodes[g][k]];
      }

      // Faces: internal faces incident to owned cells + boundary faces of owned.
      struct LF { int l, r, bc; double nx, ny, a, fx, fy; };
      std::vector<LF> lf;
      for (int f = 0; f < gm.n_faces_internal; ++f) {
        int gl = gm.face_cell_l[f], gr = gm.face_cell_r[f];
        bool ownl = part[gl] == r, ownr = part[gr] == r;
        if (!ownl && !ownr) continue;
        LF e;
        e.bc = -1; e.a = gm.face_area[f]; e.fx = gm.face_cx[f]; e.fy = gm.face_cy[f];
        if (ownl) {
          e.l = g2l[gl]; e.r = g2l[gr];
          e.nx = gm.face_nx[f]; e.ny = gm.face_ny[f];
        } else {
          e.l = g2l[gr]; e.r = g2l[gl];
          e.nx = -gm.face_nx[f]; e.ny = -gm.face_ny[f];
        }
        lf.push_back(e);
      }
      for (int f = gm.n_faces_internal;
           f < gm.n_faces_internal + gm.n_faces_boundary; ++f) {
        int gl = gm.face_cell_l[f];
        if (part[gl] != r) continue;
        LF e;
        e.l = g2l[gl]; e.r = -1; e.bc = gm.face_bc_family[f];
        e.nx = gm.face_nx[f]; e.ny = gm.face_ny[f]; e.a = gm.face_area[f];
        e.fx = gm.face_cx[f]; e.fy = gm.face_cy[f];
        lf.push_back(e);
      }
      lm.n_faces = static_cast<int>(lf.size());
      lm.face_l.resize(lm.n_faces); lm.face_r.resize(lm.n_faces);
      lm.face_nx.resize(lm.n_faces); lm.face_ny.resize(lm.n_faces);
      lm.face_area.resize(lm.n_faces); lm.face_cx.resize(lm.n_faces);
      lm.face_cy.resize(lm.n_faces); lm.face_bc.resize(lm.n_faces);
      for (int f = 0; f < lm.n_faces; ++f) {
        lm.face_l[f] = lf[f].l; lm.face_r[f] = lf[f].r; lm.face_bc[f] = lf[f].bc;
        lm.face_nx[f] = lf[f].nx; lm.face_ny[f] = lf[f].ny; lm.face_area[f] = lf[f].a;
        lm.face_cx[f] = lf[f].fx; lm.face_cy[f] = lf[f].fy;
      }

      // Cell -> face CSR + LSQ geometry.
      lm.cell_face_start.assign(lm.n_owned + 1, 0);
      {
        std::vector<int> cnt(lm.n_owned, 0);
        for (int f = 0; f < lm.n_faces; ++f) {
          ++cnt[lm.face_l[f]];
          if (lm.face_r[f] >= 0 && lm.face_r[f] < lm.n_owned) ++cnt[lm.face_r[f]];
        }
        for (int i = 0; i < lm.n_owned; ++i) lm.cell_face_start[i + 1] = lm.cell_face_start[i] + cnt[i];
        lm.cell_face_list.resize(lm.cell_face_start[lm.n_owned]);
        std::vector<int> pos = lm.cell_face_start;
        for (int f = 0; f < lm.n_faces; ++f) {
          lm.cell_face_list[pos[lm.face_l[f]]++] = f;
          if (lm.face_r[f] >= 0 && lm.face_r[f] < lm.n_owned)
            lm.cell_face_list[pos[lm.face_r[f]]++] = f;
        }
      }
      const int ncf = lm.cell_face_start[lm.n_owned];
      lm.cf_dx.resize(ncf); lm.cf_dy.resize(ncf); lm.cf_w.resize(ncf);
      lm.lsq_m00.assign(lm.n_owned, 0.0);
      lm.lsq_m01.assign(lm.n_owned, 0.0);
      lm.lsq_m11.assign(lm.n_owned, 0.0);
      for (int i = 0; i < lm.n_owned; ++i) {
        double m00 = 0.0, m01 = 0.0, m11 = 0.0;
        for (int k = lm.cell_face_start[i]; k < lm.cell_face_start[i + 1]; ++k) {
          int f = lm.cell_face_list[k];
          double dx, dy;
          if (lm.face_r[f] >= 0) {
            int o = (lm.face_l[f] == i) ? lm.face_r[f] : lm.face_l[f];
            dx = lm.cx[o] - lm.cx[i];
            dy = lm.cy[o] - lm.cy[i];
          } else {
            // Boundary virtual point at the face center.
            dx = lm.face_cx[f] - lm.cx[i];
            dy = lm.face_cy[f] - lm.cy[i];
          }
          double d2 = dx * dx + dy * dy;
          double w = 1.0 / std::max(d2, 1e-30);
          lm.cf_dx[k] = dx; lm.cf_dy[k] = dy; lm.cf_w[k] = w;
          m00 += w * dx * dx; m01 += w * dx * dy; m11 += w * dy * dy;
        }
        double det = m00 * m11 - m01 * m01;
        if (det <= 0.0) {
          // Regularize a degenerate stencil (should not happen on valid meshes).
          m00 += 1e-12; m11 += 1e-12;
          det = m00 * m11 - m01 * m01;
        }
        lm.lsq_m00[i] = m11 / det;
        lm.lsq_m01[i] = -m01 / det;
        lm.lsq_m11[i] = m00 / det;
      }

      // Halo description per neighbor rank.
      std::map<int, std::set<int>> send_set, recv_set;
      for (int f = 0; f < lm.n_faces; ++f) {
        int rcell = lm.face_r[f];
        if (rcell >= lm.n_owned) {
          int q = ghost_owner[rcell - lm.n_owned];
          send_set[q].insert(lm.face_l[f]);   // my owned cell the neighbor needs
          recv_set[q].insert(rcell);          // ghost cell I receive from it
        }
      }
      for (const auto& kv : send_set) lm.nbr_rank.push_back(kv.first);
      lm.send_start.assign(lm.nbr_rank.size() + 1, 0);
      lm.recv_start.assign(lm.nbr_rank.size() + 1, 0);
      for (size_t k = 0; k < lm.nbr_rank.size(); ++k) {
        int q = lm.nbr_rank[k];
        // Sort by global id so send/recv buffers match the neighbor's order.
        std::vector<int> s(send_set[q].begin(), send_set[q].end());
        std::vector<int> rr(recv_set[q].begin(), recv_set[q].end());
        auto by_gid = [&](int a, int b) { return lm.cell_global[a] < lm.cell_global[b]; };
        std::sort(s.begin(), s.end(), by_gid);
        std::sort(rr.begin(), rr.end(), by_gid);
        for (int c : s) lm.send_cells.push_back(c);
        for (int c : rr) lm.recv_cells.push_back(c);
        lm.send_start[k + 1] = lm.send_start[k] + static_cast<int>(s.size());
        lm.recv_start[k + 1] = lm.recv_start[k] + static_cast<int>(rr.size());
      }

      // ---- Write the rank file ----
      const std::string rfile = dir + "/rank" + std::to_string(r) + ".bin";
      std::ofstream of(rfile, std::ios::binary);
      PartHeader h{};
      std::strncpy(h.magic, "CFDPART1", sizeof(h.magic));
      h.rank = r; h.n_ranks = n_ranks; h.n_owned = lm.n_owned; h.n_ghost = lm.n_ghost;
      h.n_nodes = lm.n_nodes; h.n_faces = lm.n_faces;
      h.n_cell_node_list = lm.cell_node_start[lm.n_cells];
      h.n_cells_global = gm.n_cells;
      h.n_faces_global = gm.n_faces_internal + gm.n_faces_boundary;
      h.n_nodes_global = gm.n_nodes;
      h.edge_cut = edge_cut;
      h.n_bc_names = static_cast<int>(lm.bc_names.size());
      h.n_nbr = static_cast<int>(lm.nbr_rank.size());
      h.n_cf = ncf;
      of.write(reinterpret_cast<const char*>(&h), sizeof(h));
      for (const auto& nm : lm.bc_names) {
        int32_t len = static_cast<int32_t>(nm.size());
        of.write(reinterpret_cast<const char*>(&len), sizeof(len));
        of.write(nm.data(), len);
      }
      write_vec(of, lm.cell_global);
      write_vec(of, lm.cx); write_vec(of, lm.cy); write_vec(of, lm.vol);
      write_vec(of, lm.cell_node_start); write_vec(of, lm.cell_node_list);
      write_vec(of, lm.node_x); write_vec(of, lm.node_y); write_vec(of, lm.node_global);
      write_vec(of, lm.face_l); write_vec(of, lm.face_r); write_vec(of, lm.face_bc);
      write_vec(of, lm.face_nx); write_vec(of, lm.face_ny); write_vec(of, lm.face_area);
      write_vec(of, lm.face_cx); write_vec(of, lm.face_cy);
      write_vec(of, lm.cell_face_start); write_vec(of, lm.cell_face_list);
      write_vec(of, lm.cf_dx); write_vec(of, lm.cf_dy); write_vec(of, lm.cf_w);
      write_vec(of, lm.lsq_m00); write_vec(of, lm.lsq_m01); write_vec(of, lm.lsq_m11);
      write_vec(of, lm.nbr_rank);
      write_vec(of, lm.send_start); write_vec(of, lm.send_cells);
      write_vec(of, lm.recv_start); write_vec(of, lm.recv_cells);
      of.close();
    }

    // Per-rank summary into the global JSON.
    for (int r = 0; r < n_ranks; ++r) {
      int ng = 0;
      for (const auto& kv : ghost_of[r]) ng += static_cast<int>(kv.second.size());
      ginfo["ranks"].push_back({{"rank", r},
                                {"owned", static_cast<int>(owned_of[r].size())},
                                {"ghost", ng}});
    }
    std::ofstream gf(dir + "/global.json");
    gf << ginfo.dump(2) << std::endl;
    std::ofstream df(dir + "/DONE");
    df << "ok" << std::endl;
    return true;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
}

LocalMesh load_local_partition(const std::string& out_dir, int rank, int n_ranks,
                               PartitionInfo& info) {
  const std::string dir = part_dir(out_dir, n_ranks);
  const std::string rfile = dir + "/rank" + std::to_string(rank) + ".bin";
  std::ifstream f(rfile, std::ios::binary);
  if (!f) throw std::runtime_error("missing partition file: " + rfile);
  PartHeader h{};
  f.read(reinterpret_cast<char*>(&h), sizeof(h));
  if (std::strncmp(h.magic, "CFDPART1", 8) != 0)
    throw std::runtime_error("bad partition file magic in " + rfile);
  if (h.rank != rank || h.n_ranks != n_ranks)
    throw std::runtime_error("partition file rank mismatch in " + rfile);

  LocalMesh lm;
  lm.rank = rank; lm.n_ranks = n_ranks;
  lm.n_owned = h.n_owned; lm.n_ghost = h.n_ghost; lm.n_cells = h.n_owned + h.n_ghost;
  lm.n_nodes = h.n_nodes; lm.n_faces = h.n_faces;
  for (int i = 0; i < h.n_bc_names; ++i) {
    int32_t len = 0;
    f.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!f || len < 0 || len > 1024)
      throw std::runtime_error("truncated or corrupt partition file (bc name)");
    std::string nm(len, ' ');
    f.read(nm.data(), len);
    if (!f)
      throw std::runtime_error("truncated or corrupt partition file (bc name)");
    lm.bc_names.push_back(nm);
  }
  read_vec(f, lm.cell_global, lm.n_cells);
  read_vec(f, lm.cx, lm.n_cells); read_vec(f, lm.cy, lm.n_cells);
  read_vec(f, lm.vol, lm.n_cells);
  read_vec(f, lm.cell_node_start, lm.n_cells + 1);
  read_vec(f, lm.cell_node_list, h.n_cell_node_list);
  read_vec(f, lm.node_x, lm.n_nodes); read_vec(f, lm.node_y, lm.n_nodes);
  read_vec(f, lm.node_global, lm.n_nodes);
  read_vec(f, lm.face_l, lm.n_faces); read_vec(f, lm.face_r, lm.n_faces);
  read_vec(f, lm.face_bc, lm.n_faces);
  read_vec(f, lm.face_nx, lm.n_faces); read_vec(f, lm.face_ny, lm.n_faces);
  read_vec(f, lm.face_area, lm.n_faces);
  read_vec(f, lm.face_cx, lm.n_faces); read_vec(f, lm.face_cy, lm.n_faces);
  read_vec(f, lm.cell_face_start, lm.n_owned + 1);
  read_vec(f, lm.cell_face_list, h.n_cf);
  read_vec(f, lm.cf_dx, h.n_cf); read_vec(f, lm.cf_dy, h.n_cf);
  read_vec(f, lm.cf_w, h.n_cf);
  read_vec(f, lm.lsq_m00, lm.n_owned); read_vec(f, lm.lsq_m01, lm.n_owned);
  read_vec(f, lm.lsq_m11, lm.n_owned);
  read_vec(f, lm.nbr_rank, h.n_nbr);
  read_vec(f, lm.send_start, h.n_nbr + 1);
  read_vec(f, lm.send_cells, lm.send_start[h.n_nbr]);
  read_vec(f, lm.recv_start, h.n_nbr + 1);
  read_vec(f, lm.recv_cells, lm.recv_start[h.n_nbr]);

  info.n_cells_global = h.n_cells_global;
  info.n_faces_global = h.n_faces_global;
  info.n_nodes_global = h.n_nodes_global;
  info.edge_cut = h.edge_cut;
  info.bc_names = lm.bc_names;
  return lm;
}

int LocalMesh::numBoundaryFacesOwned() const {
  int n = 0;
  for (int f = 0; f < n_faces; ++f)
    if (face_bc[f] >= 0) ++n;
  return n;
}

std::string partition_diagnostics_csv(const LocalMesh& lm, const PartitionInfo&) {
  // rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,
  // neighbor_ranks,send_cells,recv_cells
  std::string row = std::to_string(lm.rank) + "," + std::to_string(lm.n_owned) + "," +
                    std::to_string(lm.n_ghost) + "," +
                    std::to_string(lm.numBoundaryFacesOwned()) + "," +
                    std::to_string(lm.nbr_rank.size()) + ",\"";
  for (size_t k = 0; k < lm.nbr_rank.size(); ++k) {
    row += std::to_string(lm.nbr_rank[k]);
    if (k + 1 < lm.nbr_rank.size()) row += ";";
  }
  row += "\",";
  row += std::to_string(lm.send_start.empty() ? 0 : lm.send_start.back()) + ",";
  row += std::to_string(lm.recv_start.empty() ? 0 : lm.recv_start.back());
  return row;
}

}  // namespace cfd
