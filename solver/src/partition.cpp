// METIS cell-graph partitioning, rank-local mesh construction, and scatter.
#include "partition.hpp"
#include <metis.h>
#include <algorithm>
#include <map>
#include <set>
#include <cstring>
#include <mpi.h>
#include <stdexcept>
#include <unordered_map>

#include <vector>

namespace cfd {

namespace {
// ---- tiny binary serializer (also reused for restart files) ----
struct Writer {
  std::vector<char> b;
  void put(const void* p, size_t n) {
    b.insert(b.end(), (const char*)p, (const char*)p + n);
  }
  template <class T> void t(const T& x) { put(&x, sizeof(T)); }
  void u32(uint32_t x) { t(x); }
  void s(const std::string& x) { u32((uint32_t)x.size()); if (!x.empty()) put(x.data(), x.size()); }
  void v2(const Vec2& x) { t(x.x); t(x.y); }
  void iv(const std::vector<int>& x) { u32((uint32_t)x.size()); if (!x.empty()) put(x.data(), x.size() * sizeof(int)); }
  void dv(const std::vector<double>& x) { u32((uint32_t)x.size()); if (!x.empty()) put(x.data(), x.size() * sizeof(double)); }
  void v2v(const std::vector<Vec2>& x) { u32((uint32_t)x.size()); if (!x.empty()) put(x.data(), x.size() * sizeof(Vec2)); }
};
struct Reader {
  const char* p; const char* e;
  void get(void* dst, size_t n) {
    if (p + n > e) throw std::runtime_error("deserialize: out of bounds");
    std::memcpy(dst, p, n); p += n;
  }
  template <class T> T t() { T x; get(&x, sizeof(T)); return x; }
  uint32_t u32() { return t<uint32_t>(); }
  int i32() { return t<int>(); }
  double d() { return t<double>(); }
  std::string s() { uint32_t n = u32(); std::string x; if (n) { x.resize(n); get(&x[0], n); } return x; }
  Vec2 v2() { Vec2 x; x.x = d(); x.y = d(); return x; }
  std::vector<int> iv() { uint32_t n = u32(); std::vector<int> x; if (n) { x.resize(n); get(x.data(), n*sizeof(int)); } return x; }
  std::vector<double> dv() { uint32_t n = u32(); std::vector<double> x; if (n) { x.resize(n); get(x.data(), n*sizeof(double)); } return x; }
  std::vector<Vec2> v2v() { uint32_t n = u32(); std::vector<Vec2> x; if (n) { x.resize(n); get(x.data(), n*sizeof(Vec2)); } return x; }
};

void serialize_face(Writer& w, const LocalFace& f) {
  w.v2(f.p1); w.v2(f.p2); w.v2(f.center);
  w.t(f.Sx); w.t(f.Sy); w.t(f.len);
  w.t(f.lc); w.t(f.rc); w.t((int)f.bctype);
  w.s(f.family); w.t(f.global_face); w.t(f.lc_owner); w.t(f.rc_owner);
}
LocalFace deserialize_face(Reader& r) {
  LocalFace f;
  f.p1 = r.v2(); f.p2 = r.v2(); f.center = r.v2();
  f.Sx = r.d(); f.Sy = r.d(); f.len = r.d();
  f.lc = r.i32(); f.rc = r.i32(); f.bctype = (BCType)r.i32();
  f.family = r.s(); f.global_face = r.i32(); f.lc_owner = r.i32(); f.rc_owner = r.i32();
  return f;
}
}  // namespace

std::vector<char> serialize_local_mesh(const LocalMesh& m) {
  Writer w;
  w.t(m.rank); w.t(m.nranks); w.t(m.n_owned); w.t(m.n_ghost);
  uint32_t n = (uint32_t)m.center.size();
  w.u32(n);
  w.dv(std::vector<double>(m.area.begin(), m.area.end()));
  // center
  for (const auto& c : m.center) w.v2(c);
  w.iv(m.global_id);
  w.iv(m.owner);
  w.u32((uint32_t)m.verts.size());
  for (const auto& v : m.verts) w.v2v(v);
  w.u32((uint32_t)m.cell_faces.size());
  for (const auto& cf : m.cell_faces) w.iv(cf);
  w.u32((uint32_t)m.faces.size());
  for (const auto& f : m.faces) serialize_face(w, f);
  w.iv(m.wall_face_ids);
  w.iv(m.farfield_face_ids);
  w.iv(m.neighbor_ranks);
  w.u32((uint32_t)m.send_local.size());
  for (const auto& s : m.send_local) w.iv(s);
  w.u32((uint32_t)m.recv_local.size());
  for (const auto& r : m.recv_local) w.iv(r);
  w.t(m.num_cells_global); w.t(m.num_faces_global); w.t(m.edge_cut);
  w.s(m.partitioner); w.s(m.halo_exchange);
  w.v2(m.bbox_min); w.v2(m.bbox_max); w.t(m.diag); w.s(m.mesh_file);
  return w.b;
}
LocalMesh deserialize_local_mesh(const char* data, size_t n) {
  Reader r{data, data + n};
  LocalMesh m;
  m.rank = r.i32(); m.nranks = r.i32(); m.n_owned = r.i32(); m.n_ghost = r.i32();
  uint32_t nc = r.u32();
  m.area = r.dv();
  m.center.resize(nc);
  for (uint32_t i = 0; i < nc; ++i) m.center[i] = r.v2();
  m.global_id = r.iv();
  m.owner = r.iv();
  uint32_t nv = r.u32();
  m.verts.resize(nv);
  for (uint32_t i = 0; i < nv; ++i) m.verts[i] = r.v2v();
  uint32_t ncf = r.u32();
  m.cell_faces.resize(ncf);
  for (uint32_t i = 0; i < ncf; ++i) m.cell_faces[i] = r.iv();
  uint32_t nf = r.u32();
  m.faces.resize(nf);
  for (uint32_t i = 0; i < nf; ++i) m.faces[i] = deserialize_face(r);
  m.wall_face_ids = r.iv();
  m.farfield_face_ids = r.iv();
  m.neighbor_ranks = r.iv();
  uint32_t ns = r.u32();
  m.send_local.resize(ns);
  for (uint32_t i = 0; i < ns; ++i) m.send_local[i] = r.iv();
  uint32_t nr = r.u32();
  m.recv_local.resize(nr);
  for (uint32_t i = 0; i < nr; ++i) m.recv_local[i] = r.iv();
  m.num_cells_global = r.i32(); m.num_faces_global = r.i32(); m.edge_cut = r.i32();
  m.partitioner = r.s(); m.halo_exchange = r.s();
  m.bbox_min = r.v2(); m.bbox_max = r.v2(); m.diag = r.d(); m.mesh_file = r.s();
  return m;
}

namespace {
// Build CSR cell adjacency from internal faces (numflag=0, 0-based).
void build_graph(const Mesh& g, std::vector<idx_t>& xadj, std::vector<idx_t>& adjncy) {
  int n = (int)g.cells.size();
  std::vector<std::vector<int>> adj(n);
  for (const auto& f : g.faces) {
    if (f.rc < 0) continue;  // boundary
    adj[f.lc].push_back(f.rc);
    adj[f.rc].push_back(f.lc);
  }
  xadj.assign(n + 1, 0);
  for (int i = 0; i < n; ++i) xadj[i + 1] = xadj[i] + (idx_t)adj[i].size();
  adjncy.clear();
  for (int i = 0; i < n; ++i) adjncy.insert(adjncy.end(), adj[i].begin(), adj[i].end());
}

// Build the rank-local mesh for `target` from the global mesh + partition.
LocalMesh build_local(const Mesh& g, const std::vector<int>& part, int target, int nranks,
                      int edge_cut) {
  LocalMesh m;
  m.rank = target; m.nranks = nranks;
  int n = (int)g.cells.size();
  // owned local indices
  std::vector<int> g2l(n, -1);
  int n_owned = 0;
  for (int c = 0; c < n; ++c) if (part[c] == target) g2l[c] = n_owned++;
  m.n_owned = n_owned;
  // ghosts: neighbors of owned cells owned by other ranks
  std::vector<int> ghost_global;
  for (const auto& f : g.faces) {
    if (f.rc < 0) continue;
    int a = f.lc, b = f.rc;
    if (part[a] == target && part[b] != target) ghost_global.push_back(b);
    if (part[b] == target && part[a] != target) ghost_global.push_back(a);
  }
  std::sort(ghost_global.begin(), ghost_global.end());
  ghost_global.erase(std::unique(ghost_global.begin(), ghost_global.end()), ghost_global.end());
  int n_ghost = (int)ghost_global.size();
  m.n_ghost = n_ghost;
  for (int i = 0; i < n_ghost; ++i) g2l[ghost_global[i]] = n_owned + i;
  int nloc = n_owned + n_ghost;
  m.center.resize(nloc); m.area.resize(nloc); m.global_id.resize(nloc);
  m.owner.resize(nloc); m.verts.resize(nloc); m.cell_faces.resize(nloc);
  for (int c = 0; c < n; ++c) if (g2l[c] >= 0) {
    int li = g2l[c];
    m.center[li] = g.cells[c].center;
    m.area[li] = g.cells[c].area;
    m.global_id[li] = c;
    m.owner[li] = part[c];
    if (li < n_owned) m.verts[li] = g.cells[c].verts;
  }
  // local faces (touching owned cells)
  for (int fi = 0; fi < (int)g.faces.size(); ++fi) {
    const Face& gf = g.faces[fi];
    int a = gf.lc, b = gf.rc;
    bool aOwn = (a >= 0 && part[a] == target);
    bool bOwn = (b >= 0 && part[b] == target);
    if (!aOwn && !bOwn) continue;
    LocalFace lf;
    lf.p1 = gf.p1; lf.p2 = gf.p2; lf.center = gf.center;
    lf.Sx = gf.Sx; lf.Sy = gf.Sy; lf.len = gf.len;
    lf.global_face = fi;
    lf.lc = (a >= 0) ? g2l[a] : -1;
    lf.rc = (b >= 0) ? g2l[b] : -1;
    lf.bctype = gf.bctype;
    lf.family = gf.family;
    lf.lc_owner = (a >= 0) ? part[a] : -1;
    lf.rc_owner = (b >= 0) ? part[b] : -1;
    int lfi = (int)m.faces.size();
    m.faces.push_back(lf);
    if (lf.lc >= 0 && lf.lc < n_owned) m.cell_faces[lf.lc].push_back(lfi);
    if (lf.rc >= 0 && lf.rc < n_owned) m.cell_faces[lf.rc].push_back(lfi);
    if (lf.rc < 0) {
      if (lf.bctype == BCType::SlipWall || lf.bctype == BCType::NoSlipAdiabaticWall)
        m.wall_face_ids.push_back(lfi);
      else if (lf.bctype == BCType::Farfield)
        m.farfield_face_ids.push_back(lfi);
    }
  }
  // halo communication plan (per neighbor rank)
  std::map<int, std::set<int>> send, recv;
  for (const auto& lf : m.faces) {
    if (lf.rc >= 0) {
      // face between two local cells
      int lo = lf.lc_owner, ro = lf.rc_owner;
      if (lo == target && ro != target && ro >= 0) { send[ro].insert(lf.lc); recv[ro].insert(lf.rc); }
      if (ro == target && lo != target && lo >= 0) { send[lo].insert(lf.rc); recv[lo].insert(lf.lc); }
    }
  }
  for (const auto& kv : send) {
    m.neighbor_ranks.push_back(kv.first);
    m.send_local.emplace_back(kv.second.begin(), kv.second.end());
    m.recv_local.emplace_back(recv[kv.first].begin(), recv[kv.first].end());
  }
  m.num_cells_global = (int)g.cells.size();
  m.num_faces_global = (int)g.faces.size();
  m.edge_cut = edge_cut;
  m.bbox_min = g.bbox_min; m.bbox_max = g.bbox_max; m.diag = g.diag;
  m.mesh_file = g.mesh_file;
  return m;
}
}  // namespace

LocalMesh distribute_mesh(const Mesh& global, int rank, int nranks) {
  std::vector<int> part;
  int edge_cut = 0;
  if (rank == 0) {
    std::vector<idx_t> xadj, adjncy;
    build_graph(global, xadj, adjncy);
    idx_t nvtxs = (idx_t)global.cells.size();
    idx_t ncon = 1;
    idx_t nparts = nranks;
    part.assign(nvtxs, 0);
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_CONTIG] = 1;   // contiguous partitions
    options[METIS_OPTION_SEED] = 42;
    options[METIS_OPTION_NUMBERING] = 0;
    idx_t ec = 0;
    // METIS divides by (nparts-1) internally for tpwgts/ubvec normalization;
    // nparts==1 therefore crashes with a divide-by-zero. A single rank
    // trivially owns everything, so skip the METIS call in that case.
    if (nparts > 1 && nvtxs > 0) {
      int r = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                  nullptr, nullptr, nullptr, &nparts,
                                  nullptr, nullptr, options, &ec, part.data());
      if (r != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
      edge_cut = (int)ec;
    } else {
      edge_cut = 0;
    }
  }
  // broadcast partition + edge cut to all ranks (small int array; this is
  // preprocessing metadata, not solution state)
  int ncells = (rank == 0) ? (int)global.cells.size() : 0;
  MPI_Bcast(&ncells, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if ((int)part.size() != ncells) part.assign(ncells, 0);
  MPI_Bcast(part.data(), ncells, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&edge_cut, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // rank 0 builds every rank's LocalMesh and scatters one per rank.
  std::vector<char> sendbuf, rankbuf;
  std::vector<int> counts, displs;
  if (rank == 0) {
    displs.assign(nranks, 0);
    counts.assign(nranks, 0);
    for (int r = 0; r < nranks; ++r) {
      LocalMesh lm = build_local(global, part, r, nranks, edge_cut);
      std::vector<char> blob = serialize_local_mesh(lm);
      displs[r] = (int)sendbuf.size();
      counts[r] = (int)blob.size();
      sendbuf.insert(sendbuf.end(), blob.begin(), blob.end());
    }
  }
  // scatter the blob sizes first so each rank knows its recv count
  int mycount = 0;
  MPI_Scatter(counts.data(), 1, MPI_INT, &mycount, 1, MPI_INT, 0, MPI_COMM_WORLD);
  rankbuf.assign(mycount, 0);
  MPI_Scatterv(sendbuf.data(), counts.data(), displs.data(), MPI_BYTE,
               rankbuf.data(), mycount, MPI_BYTE, 0, MPI_COMM_WORLD);
  if (rankbuf.empty()) {
    // rank received nothing (e.g. more ranks than cells) -> empty local mesh
    LocalMesh m; m.rank = rank; m.nranks = nranks;
    m.num_cells_global = ncells; m.num_faces_global = 0; m.edge_cut = edge_cut;
    m.bbox_min = global.bbox_min; m.bbox_max = global.bbox_max; m.diag = global.diag;
    m.mesh_file = global.mesh_file;
    return m;
  }
  return deserialize_local_mesh(rankbuf.data(), rankbuf.size());
}

}  // namespace cfd
