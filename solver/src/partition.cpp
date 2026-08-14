#include "partition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <utility>
#include <vector>

#include <metis.h>

namespace cfd {

std::string partition_dir(const std::string& base, int np) {
  return base + "/partition_np" + std::to_string(np);
}

namespace {

struct GFace {
  int32_t c0 = -1, c1 = -1;
  int32_t n0 = -1, n1 = -1;
};

void mkdir_p(const std::string& d) {
  std::string cur;
  for (char ch : d) {
    cur += ch;
    if (ch == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
  }
  mkdir(d.c_str(), 0755);
}

template <typename T>
void wr(std::ofstream& o, const T& v) {
  o.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
void wrv(std::ofstream& o, const std::vector<T>& v) {
  int64_t n = static_cast<int64_t>(v.size());
  wr(o, n);
  if (n > 0) o.write(reinterpret_cast<const char*>(v.data()), n * sizeof(T));
}
void wrs(std::ofstream& o, const std::string& s) {
  int64_t n = static_cast<int64_t>(s.size());
  wr(o, n);
  o.write(s.data(), n);
}
template <typename T>
void rd(std::ifstream& i, T& v) {
  i.read(reinterpret_cast<char*>(&v), sizeof(T));
}
template <typename T>
void rdv(std::ifstream& i, std::vector<T>& v) {
  int64_t n = 0;
  rd(i, n);
  v.resize(n);
  if (n > 0) i.read(reinterpret_cast<char*>(v.data()), n * sizeof(T));
}
void rds(std::ifstream& i, std::string& s) {
  int64_t n = 0;
  rd(i, n);
  s.resize(n);
  i.read(s.data(), n);
}

constexpr int64_t kMagic = 0x4346445041525431LL;

}  // namespace

void build_partitions(const GlobalMesh& gm, int np, const std::string& out_dir,
                      std::vector<int64_t>* global_stats) {
  const int nc = gm.num_cells;

  std::map<std::pair<int32_t, int32_t>, GFace> emap;
  for (int c = 0; c < nc; ++c) {
    int nv = gm.cell_nverts[c];
    for (int v = 0; v < nv; ++v) {
      int32_t a = gm.cell_nodes[c][v];
      int32_t b = gm.cell_nodes[c][(v + 1) % nv];
      auto key = std::minmax(a, b);
      auto it = emap.find(key);
      if (it == emap.end()) {
        GFace f;
        f.c0 = c;
        f.n0 = a;
        f.n1 = b;
        emap.emplace(key, f);
      } else {
        if (it->second.c1 != -1)
          throw std::runtime_error("non-manifold edge in mesh");
        it->second.c1 = c;
      }
    }
  }

  std::set<std::pair<int32_t, int32_t>> bnd_keys;
  std::map<std::pair<int32_t, int32_t>, int32_t> bnd_fam;
  std::map<int32_t, int32_t> fam_remap;
  std::vector<std::string> used_family_names;
  std::map<std::string, int> dbg_fam_total, dbg_fam_skipped;
  for (const auto& bf : gm.bnd_faces) {
    auto key = std::minmax(bf.n0, bf.n1);
    auto it = emap.find(key);
    if (it == emap.end())
      throw std::runtime_error("boundary face does not match any cell edge");
    dbg_fam_total[gm.family_names[bf.family_id]]++;
    if (it->second.c1 != -1) {
      dbg_fam_skipped[gm.family_names[bf.family_id]]++;
      continue;
    }
    auto rit = fam_remap.find(bf.family_id);
    if (rit == fam_remap.end()) {
      int nid = (int)used_family_names.size();
      rit = fam_remap.emplace(bf.family_id, nid).first;
      used_family_names.push_back(gm.family_names[bf.family_id]);
    }
    bnd_keys.insert(key);
    bnd_fam[key] = rit->second;
  }
  for (const auto& [nm, tot] : dbg_fam_total)
    fprintf(stderr,
            "partition: boundary section %-12s faces=%d skipped_interior=%d\n",
            nm.c_str(), tot, dbg_fam_skipped[nm]);

  std::vector<GFace> gfaces;
  std::vector<int32_t> gface_fam;
  int unmatched = 0;
  for (auto& [key, f] : emap) {
    if (f.c1 == -1) {
      auto bit = bnd_fam.find(key);
      if (bit == bnd_fam.end()) {
        ++unmatched;
        continue;
      }
      gfaces.push_back(f);
      gface_fam.push_back(bit->second);
    } else {
      gfaces.push_back(f);
      gface_fam.push_back(-1);
    }
  }
  if (unmatched > 0)
    throw std::runtime_error("found " + std::to_string(unmatched) +
                             " single-cell edges not covered by boundary "
                             "sections (zone stitching failure?)");

  std::vector<std::vector<int32_t>> cell_adj(nc);
  for (const auto& f : gfaces) {
    if (f.c1 >= 0) {
      cell_adj[f.c0].push_back(f.c1);
      cell_adj[f.c1].push_back(f.c0);
    }
  }

  std::vector<int32_t> part(nc, 0);
  int64_t edge_cut = 0;
  if (np > 1) {
    idx_t nvtxs = nc;
    std::vector<idx_t> xadj(nc + 1, 0);
    for (int i = 0; i < nc; ++i) xadj[i + 1] = xadj[i] + (idx_t)cell_adj[i].size();
    std::vector<idx_t> adjncy(xadj[nc]);
    idx_t pos = 0;
    for (int i = 0; i < nc; ++i)
      for (int32_t j : cell_adj[i]) adjncy[pos++] = j;
    idx_t ncon = 1, nparts = np, objval = 0;
    std::vector<idx_t> pidx(nc);
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;
    int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                 nullptr, nullptr, nullptr, &nparts, nullptr,
                                 nullptr, options, &objval, pidx.data());
    if (rc != METIS_OK)
      throw std::runtime_error("METIS_PartGraphKway failed");
    for (int i = 0; i < nc; ++i) part[i] = pidx[i];
    edge_cut = objval;
  }
  int64_t num_faces_global = static_cast<int64_t>(gfaces.size());

  std::string dir = out_dir;
  mkdir_p(dir);

  for (int r = 0; r < np; ++r) {
    std::vector<int32_t> owned;
    for (int c = 0; c < nc; ++c)
      if (part[c] == r) owned.push_back(c);
    std::sort(owned.begin(), owned.end());

    std::map<int32_t, int32_t> local_of;
    LocalMesh lm;
    lm.rank = r;
    lm.np = np;
    lm.num_cells_global = nc;
    lm.num_faces_global = num_faces_global;
    lm.edge_cut = edge_cut;
    lm.family_names = used_family_names;
    lm.n_owned = static_cast<int>(owned.size());

    for (int i = 0; i < lm.n_owned; ++i) {
      int32_t g = owned[i];
      local_of[g] = i;
      lm.cell_gid.push_back(g);
      lm.cell_owner.push_back(r);
      lm.cell_nodes.push_back(gm.cell_nodes[g]);
      lm.cell_nverts.push_back(gm.cell_nverts[g]);
      lm.cell_cx.push_back(gm.cell_cx[g]);
      lm.cell_cy.push_back(gm.cell_cy[g]);
      lm.cell_vol.push_back(gm.cell_vol[g]);
    }

    std::map<int32_t, std::vector<int32_t>> ghosts_by_owner;
    for (int32_t g : owned)
      for (int32_t nb : cell_adj[g])
        if (part[nb] != r) ghosts_by_owner[part[nb]].push_back(nb);

    std::vector<int32_t> ghost_owner_order;
    for (auto& [o, lst] : ghosts_by_owner) {
      std::sort(lst.begin(), lst.end());
      lst.erase(std::unique(lst.begin(), lst.end()), lst.end());
      ghost_owner_order.push_back(o);
    }

    for (int32_t o : ghost_owner_order) {
      auto& lst = ghosts_by_owner[o];
      std::vector<int32_t> recv;
      std::vector<int32_t> send;
      for (int32_t g : lst) {
        int32_t lid = static_cast<int>(lm.cell_gid.size());
        local_of[g] = lid;
        recv.push_back(lid);
        lm.cell_gid.push_back(g);
        lm.cell_owner.push_back(o);
        lm.cell_nodes.push_back({0, 0, 0, 0});
        lm.cell_nverts.push_back(0);
        lm.cell_cx.push_back(gm.cell_cx[g]);
        lm.cell_cy.push_back(gm.cell_cy[g]);
        lm.cell_vol.push_back(gm.cell_vol[g]);
      }
      lm.neighbor_ranks.push_back(o);
      lm.recv_cells.push_back(std::move(recv));
      (void)send;
    }
    lm.n_ghost = static_cast<int>(lm.cell_gid.size()) - lm.n_owned;

    for (size_t k = 0; k < lm.neighbor_ranks.size(); ++k) {
      int32_t o = lm.neighbor_ranks[k];
      std::vector<int32_t> send;
      for (int32_t g : owned) {
        for (int32_t nb : cell_adj[g])
          if (part[nb] == o) {
            send.push_back(local_of[g]);
            break;
          }
      }
      lm.send_cells.push_back(std::move(send));
    }

    std::map<int32_t, int32_t> node_local;
    auto get_node = [&](int32_t g) -> int32_t {
      auto it = node_local.find(g);
      if (it != node_local.end()) return it->second;
      int32_t lid = static_cast<int32_t>(lm.node_x.size());
      node_local[g] = lid;
      lm.node_x.push_back(gm.node_x[g]);
      lm.node_y.push_back(gm.node_y[g]);
      return lid;
    };

    for (size_t fi = 0; fi < gfaces.size(); ++fi) {
      const GFace& f = gfaces[fi];
      int32_t fam = gface_fam[fi];
      bool l_owned = part[f.c0] == r;
      bool r_owned = f.c1 >= 0 && part[f.c1] == r;
      if (fam < 0 && !l_owned && !r_owned) continue;
      if (fam >= 0 && !l_owned) continue;

      Face lf;
      lf.n0 = get_node(f.n0);
      lf.n1 = get_node(f.n1);
      double x0 = gm.node_x[f.n0], y0 = gm.node_y[f.n0];
      double x1 = gm.node_x[f.n1], y1 = gm.node_y[f.n1];
      double dx = x1 - x0, dy = y1 - y0;
      double len = std::sqrt(dx * dx + dy * dy);
      if (len <= 0.0) throw std::runtime_error("zero-length face");
      double nx = dy / len, ny = -dx / len;

      int32_t gc0 = f.c0, gc1 = f.c1;
      int32_t lc0 = local_of[gc0];
      if (fam >= 0) {
        lf.left = lc0;
        lf.right = -1;
        double fx = 0.5 * (x0 + x1), fy = 0.5 * (y0 + y1);
        double vx = fx - gm.cell_cx[gc0], vy = fy - gm.cell_cy[gc0];
        if (nx * vx + ny * vy < 0.0) { nx = -nx; ny = -ny; }
        lf.bc_family = fam;
      } else {
        int32_t lc1 = local_of[gc1];
        if (!l_owned) {
          std::swap(lc0, lc1);
          std::swap(gc0, gc1);
        }
        lf.left = lc0;
        lf.right = lc1;
        double vx = gm.cell_cx[gc1] - gm.cell_cx[gc0];
        double vy = gm.cell_cy[gc1] - gm.cell_cy[gc0];
        if (nx * vx + ny * vy < 0.0) { nx = -nx; ny = -ny; }
        lf.bc_family = -1;
      }
      lf.nx = nx;
      lf.ny = ny;
      lf.area = len;
      lf.cx = 0.5 * (x0 + x1);
      lf.cy = 0.5 * (y0 + y1);
      double tx, ty;
      if (lf.right >= 0) {
        tx = lm.cell_cx[lf.right] - lm.cell_cx[lf.left];
        ty = lm.cell_cy[lf.right] - lm.cell_cy[lf.left];
      } else {
        tx = lf.cx - lm.cell_cx[lf.left];
        ty = lf.cy - lm.cell_cy[lf.left];
      }
      double dd = std::sqrt(tx * tx + ty * ty);
      lf.ex = tx / dd;
      lf.ey = ty / dd;
      lf.dist = dd;

      if (lf.bc_family >= 0)
        lm.faces_bnd.push_back(lf);
      else
        lm.faces_int.push_back(lf);
    }

    for (auto& cn : lm.cell_nodes) {
      for (auto& v : cn) v = -1;
    }
    for (int i = 0; i < lm.n_owned; ++i) {
      int32_t g = lm.cell_gid[i];
      for (int v = 0; v < lm.cell_nverts[i]; ++v)
        lm.cell_nodes[i][v] = node_local.at(gm.cell_nodes[g][v]);
    }

    std::string path = dir + "/rank_" + std::to_string(r) + ".bin";
    std::ofstream o(path, std::ios::binary);
    if (!o) throw std::runtime_error("cannot write partition file " + path);
    wr(o, kMagic);
    wr(o, lm.np);
    wr(o, lm.rank);
    wr(o, lm.n_owned);
    wr(o, lm.n_ghost);
    wr(o, lm.num_cells_global);
    wr(o, lm.num_faces_global);
    wr(o, lm.edge_cut);
    int64_t nfam = lm.family_names.size();
    wr(o, nfam);
    for (auto& s : lm.family_names) wrs(o, s);
    wrv(o, lm.node_x);
    wrv(o, lm.node_y);
    wrv(o, lm.cell_nverts);
    wrv(o, lm.cell_cx);
    wrv(o, lm.cell_cy);
    wrv(o, lm.cell_vol);
    wrv(o, lm.cell_gid);
    wrv(o, lm.cell_owner);
    std::vector<int32_t> flat_conn(lm.cell_nodes.size() * 4);
    for (size_t i = 0; i < lm.cell_nodes.size(); ++i)
      for (int v = 0; v < 4; ++v) flat_conn[i * 4 + v] = lm.cell_nodes[i][v];
    wrv(o, flat_conn);
    int64_t nfi = lm.faces_int.size();
    wr(o, nfi);
    for (const Face& f : lm.faces_int) {
      wr(o, f.left); wr(o, f.right); wr(o, f.n0); wr(o, f.n1);
      wr(o, f.nx); wr(o, f.ny); wr(o, f.area); wr(o, f.cx); wr(o, f.cy);
      wr(o, f.ex); wr(o, f.ey); wr(o, f.dist);
    }
    int64_t nfb = lm.faces_bnd.size();
    wr(o, nfb);
    for (const Face& f : lm.faces_bnd) {
      wr(o, f.left); wr(o, f.right); wr(o, f.n0); wr(o, f.n1);
      wr(o, f.nx); wr(o, f.ny); wr(o, f.area); wr(o, f.cx); wr(o, f.cy);
      wr(o, f.ex); wr(o, f.ey); wr(o, f.dist); wr(o, f.bc_family);
    }
    int64_t nnb = lm.neighbor_ranks.size();
    wr(o, nnb);
    for (size_t k = 0; k < lm.neighbor_ranks.size(); ++k) {
      wr(o, lm.neighbor_ranks[k]);
      wrv(o, lm.send_cells[k]);
      wrv(o, lm.recv_cells[k]);
    }
  }

  if (global_stats) {
    global_stats->clear();
    global_stats->push_back(nc);
    global_stats->push_back(num_faces_global);
    global_stats->push_back(edge_cut);
  }
}

LocalMesh load_partition(const std::string& dir, int np, int rank) {
  std::string path = dir + "/rank_" + std::to_string(rank) + ".bin";
  std::ifstream i(path, std::ios::binary);
  if (!i)
    throw std::runtime_error("cannot open partition file " + path +
                             " (run the 'partition' step first)");
  LocalMesh lm;
  int64_t magic = 0;
  rd(i, magic);
  if (magic != kMagic)
    throw std::runtime_error("bad magic in partition file " + path);
  rd(i, lm.np);
  if (lm.np != np)
    throw std::runtime_error("partition np mismatch in " + path);
  rd(i, lm.rank);
  if (lm.rank != rank)
    throw std::runtime_error("partition rank mismatch in " + path);
  rd(i, lm.n_owned);
  rd(i, lm.n_ghost);
  rd(i, lm.num_cells_global);
  rd(i, lm.num_faces_global);
  rd(i, lm.edge_cut);
  int64_t nfam = 0;
  rd(i, nfam);
  lm.family_names.resize(nfam);
  for (auto& s : lm.family_names) rds(i, s);
  rdv(i, lm.node_x);
  rdv(i, lm.node_y);
  rdv(i, lm.cell_nverts);
  rdv(i, lm.cell_cx);
  rdv(i, lm.cell_cy);
  rdv(i, lm.cell_vol);
  rdv(i, lm.cell_gid);
  rdv(i, lm.cell_owner);
  std::vector<int32_t> flat_conn;
  rdv(i, flat_conn);
  lm.cell_nodes.resize(flat_conn.size() / 4);
  for (size_t k = 0; k < lm.cell_nodes.size(); ++k)
    for (int v = 0; v < 4; ++v) lm.cell_nodes[k][v] = flat_conn[k * 4 + v];
  int64_t nfi = 0;
  rd(i, nfi);
  lm.faces_int.resize(nfi);
  for (auto& f : lm.faces_int) {
    rd(i, f.left); rd(i, f.right); rd(i, f.n0); rd(i, f.n1);
    rd(i, f.nx); rd(i, f.ny); rd(i, f.area); rd(i, f.cx); rd(i, f.cy);
    rd(i, f.ex); rd(i, f.ey); rd(i, f.dist);
    f.bc_family = -1;
  }
  int64_t nfb = 0;
  rd(i, nfb);
  lm.faces_bnd.resize(nfb);
  for (auto& f : lm.faces_bnd) {
    rd(i, f.left); rd(i, f.right); rd(i, f.n0); rd(i, f.n1);
    rd(i, f.nx); rd(i, f.ny); rd(i, f.area); rd(i, f.cx); rd(i, f.cy);
    rd(i, f.ex); rd(i, f.ey); rd(i, f.dist); rd(i, f.bc_family);
  }
  int64_t nnb = 0;
  rd(i, nnb);
  lm.neighbor_ranks.resize(nnb);
  lm.send_cells.resize(nnb);
  lm.recv_cells.resize(nnb);
  for (int64_t k = 0; k < nnb; ++k) {
    rd(i, lm.neighbor_ranks[k]);
    rdv(i, lm.send_cells[k]);
    rdv(i, lm.recv_cells[k]);
  }
  return lm;
}

}  // namespace cfd
