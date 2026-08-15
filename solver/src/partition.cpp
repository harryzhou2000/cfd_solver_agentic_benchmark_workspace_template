#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace cfd {

namespace {

// Build the CSR cell-adjacency graph (deduplicated neighbors) for METIS.
void build_adjacency(const GlobalMesh& mesh, std::vector<idx_t>& xadj,
                     std::vector<idx_t>& adjncy) {
  const idx_t n = static_cast<idx_t>(mesh.cells.size());
  xadj.resize(static_cast<size_t>(n) + 1);
  std::vector<std::vector<idx_t>> nb(n);
  for (const Face& f : mesh.faces) {
    nb[static_cast<size_t>(f.cL)].push_back(static_cast<idx_t>(f.cR));
    nb[static_cast<size_t>(f.cR)].push_back(static_cast<idx_t>(f.cL));
  }
  adjncy.clear();
  for (idx_t i = 0; i < n; ++i) {
    xadj[static_cast<size_t>(i)] = static_cast<idx_t>(adjncy.size());
    auto& list = nb[static_cast<size_t>(i)];
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    adjncy.insert(adjncy.end(), list.begin(), list.end());
  }
  xadj[static_cast<size_t>(n)] = static_cast<idx_t>(adjncy.size());
}

// Exchange per-neighbor integer vectors (requests) using non-blocking sends
// and MPI_Probe-based receives.
std::vector<std::vector<int>> exchange_int_vectors(
    const std::vector<Partition::Neighbor>& neighbors,
    const std::vector<std::vector<int>>& send_data) {
  MPI_Comm comm = MPI_COMM_WORLD;
  std::vector<MPI_Request> reqs;
  // Fixed tag: exactly one message is in flight per (source, tag) pair per
  // exchange call, so the tag must not depend on the local neighbor index
  // (which is not symmetric across ranks).
  const int tag = 100;
  for (size_t k = 0; k < neighbors.size(); ++k) {
    MPI_Request r;
    const std::vector<int>& v = send_data[k];
    MPI_Isend(const_cast<int*>(v.data()), static_cast<int>(v.size()), MPI_INT,
              neighbors[k].rank, tag, comm, &r);
    reqs.push_back(r);
  }
  std::vector<std::vector<int>> recv(neighbors.size());
  std::vector<MPI_Request> recv_reqs;
  for (size_t k = 0; k < neighbors.size(); ++k) {
    MPI_Status st;
    MPI_Probe(neighbors[k].rank, tag, comm, &st);
    int count = 0;
    MPI_Get_count(&st, MPI_INT, &count);
    recv[k].resize(static_cast<size_t>(count));
    MPI_Request r;
    MPI_Irecv(recv[k].data(), count, MPI_INT, neighbors[k].rank,
              tag, comm, &r);
    recv_reqs.push_back(r);
  }
  if (!reqs.empty()) MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
  if (!recv_reqs.empty())
    MPI_Waitall(static_cast<int>(recv_reqs.size()), recv_reqs.data(), MPI_STATUSES_IGNORE);
  return recv;
}

}  // namespace

void Partition::build(const GlobalMesh& mesh, const CaseConfig& cfg, int r, int n,
                      double rho_inf, double u_inf, double v_inf, double p_inf) {
  rank = r;
  nranks = n;
  num_cells_global = static_cast<int>(mesh.cells.size());
  num_faces_global = static_cast<int>(mesh.faces.size() + mesh.bfaces.size());
  gamma = cfg.gamma;
  q_inf = {rho_inf, u_inf, v_inf, p_inf};

  // ---- METIS k-way partition (computed on rank 0, broadcast) ----
  std::vector<int> part;
  int edgecut = 0;
  if (nranks > 1) {
    std::vector<idx_t> xadj, adjncy, vwgt;
    if (rank == 0) {
      build_adjacency(mesh, xadj, adjncy);
      vwgt.reserve(mesh.cells.size());
      for (const Cell& c : mesh.cells)
        vwgt.push_back(std::max<idx_t>(1, static_cast<idx_t>(std::llround(c.vol * 100.0))));
    }
    idx_t nvtxs = static_cast<idx_t>(num_cells_global);
    idx_t ncon = 1;
    idx_t nparts = static_cast<idx_t>(nranks);
    idx_t objval = 0;
    std::vector<idx_t> part_idx(static_cast<size_t>(nvtxs));
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_SEED] = 0;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    options[METIS_OPTION_CTYPE] = METIS_CTYPE_SHEM;
    options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_EDGE;
    options[METIS_OPTION_RTYPE] = METIS_RTYPE_FM;
    options[METIS_OPTION_NCUTS] = 5;
    options[METIS_OPTION_NITER] = 10;
    if (rank == 0) {
      int ierr = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                     vwgt.data(), nullptr, nullptr, &nparts, nullptr,
                                     nullptr, options, &objval, part_idx.data());
      if (ierr != METIS_OK)
        throw std::runtime_error("METIS_PartGraphKway failed (error " + std::to_string(ierr) + ")");
      part.assign(part_idx.begin(), part_idx.end());
      edgecut = static_cast<int>(objval);
    }
    if (rank != 0) part.resize(static_cast<size_t>(num_cells_global));
    MPI_Bcast(part.data(), static_cast<int>(part.size()), MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&edgecut, 1, MPI_INT, 0, MPI_COMM_WORLD);
  } else {
    part.assign(static_cast<size_t>(num_cells_global), 0);
  }
  global_part = part;
  this->edgecut = edgecut;

  // ---- Build the rank-local cell list (owned + ghosts) ----
  global_to_local_map.assign(static_cast<size_t>(num_cells_global), -1);
  cells.clear();
  cell_faces.clear();
  for (int gid = 0; gid < num_cells_global; ++gid) {
    if (part[static_cast<size_t>(gid)] != rank) continue;
    const Cell& g = mesh.cells[static_cast<size_t>(gid)];
    RankCell rc;
    rc.global_id = gid;
    rc.nv = g.nv;
    rc.verts = g.v;
    rc.cx = g.cx;
    rc.cy = g.cy;
    rc.vol = g.vol;
    rc.owned = true;
    global_to_local_map[static_cast<size_t>(gid)] = static_cast<int>(cells.size());
    cells.push_back(rc);
  }
  num_cells_owned = static_cast<int>(cells.size());

  // Ghost discovery: face neighbors of owned cells with a different owner.
  std::set<int> ghost_set;
  for (const Face& f : mesh.faces) {
    const int a = f.cL, b = f.cR;
    const int pa = part[static_cast<size_t>(a)];
    const int pb = part[static_cast<size_t>(b)];
    if (pa == rank && pb != rank) ghost_set.insert(b);
    if (pb == rank && pa != rank) ghost_set.insert(a);
  }
  for (int gid : ghost_set) {
    const Cell& g = mesh.cells[static_cast<size_t>(gid)];
    RankCell rc;
    rc.global_id = gid;
    rc.nv = g.nv;
    rc.verts = g.v;
    rc.cx = g.cx;
    rc.cy = g.cy;
    rc.vol = g.vol;
    rc.owned = false;
    global_to_local_map[static_cast<size_t>(gid)] = static_cast<int>(cells.size());
    cells.push_back(rc);
  }
  num_cells_ghost = static_cast<int>(cells.size()) - num_cells_owned;

  // ---- Per-cell face lists ----
  cell_faces.resize(cells.size());
  for (const Face& f : mesh.faces) {
    const int la = global_to_local_map[static_cast<size_t>(f.cL)];
    const int lb = global_to_local_map[static_cast<size_t>(f.cR)];
    if (la >= 0 && lb >= 0) {
      cell_faces[static_cast<size_t>(la)].push_back({lb, f.nx, f.ny, f.len, f.fx, f.fy,
                                                     BcType::Farfield, -1});
      cell_faces[static_cast<size_t>(lb)].push_back({la, -f.nx, -f.ny, f.len, f.fx, f.fy,
                                                     BcType::Farfield, -1});
    } else if (la >= 0) {
      cell_faces[static_cast<size_t>(la)].push_back({lb, f.nx, f.ny, f.len, f.fx, f.fy,
                                                     BcType::Farfield, -1});
    } else if (lb >= 0) {
      cell_faces[static_cast<size_t>(lb)].push_back({la, -f.nx, -f.ny, f.len, f.fx, f.fy,
                                                     BcType::Farfield, -1});
    }
  }
  for (size_t gb = 0; gb < mesh.bfaces.size(); ++gb) {
    const BFace& bf = mesh.bfaces[gb];
    const int lc = global_to_local_map[static_cast<size_t>(bf.cell)];
    if (lc < 0) continue;
    cell_faces[static_cast<size_t>(lc)].push_back({-1, bf.nx, bf.ny, bf.len, bf.fx, bf.fy,
                                                   bf.type, static_cast<int>(gb)});
    bfaces.push_back(bf);
    bface_gbf.push_back(static_cast<int>(gb));
  }

  // ---- Neighbor exchange lists ----
  // Rank r needs, from each neighbor q: the geometry/state of ghosts owned by
  // q.  Rank r sends q the global ids of those ghosts (a request); q replies
  // by mapping them to its owned cells.
  std::map<int, std::vector<int>> ghost_by_owner;
  for (int gid = num_cells_owned; gid < static_cast<int>(cells.size()); ++gid) {
    const int g = cells[static_cast<size_t>(gid)].global_id;
    const int owner = part[static_cast<size_t>(g)];
    ghost_by_owner[owner].push_back(g);
  }
  neighbors.clear();
  std::vector<std::vector<int>> requests;
  for (const auto& [q, gids] : ghost_by_owner) {
    Neighbor nb;
    nb.rank = q;
    neighbors.push_back(nb);
    requests.push_back(gids);
  }
  std::vector<std::vector<int>> recv_requests =
      exchange_int_vectors(neighbors, requests);

  for (size_t k = 0; k < neighbors.size(); ++k) {
    // recv_requests[k] = global ids this rank must send to neighbor k
    auto& send_local = neighbors[k].send_local;
    for (int gid : recv_requests[k]) {
      const int loc = global_to_local_map[static_cast<size_t>(gid)];
      if (loc < 0)
        throw std::runtime_error("internal error: requested cell " + std::to_string(gid) +
                                 " not owned locally");
      send_local.push_back(loc);
    }
    // recv_local: ghosts for neighbor k, in the same order as we requested
    for (int gid : requests[k]) {
      const int loc = global_to_local_map[static_cast<size_t>(gid)];
      neighbors[k].recv_local.push_back(loc);
    }
  }

  // ---- Exchange ghost geometry (centroid, volume) ----
  {
    const int ncomp = 3;
    std::vector<double> data(static_cast<size_t>(cells.size()) * ncomp, 0.0);
    for (int i = 0; i < num_cells_owned; ++i) {
      data[static_cast<size_t>(i) * ncomp + 0] = cells[static_cast<size_t>(i)].cx;
      data[static_cast<size_t>(i) * ncomp + 1] = cells[static_cast<size_t>(i)].cy;
      data[static_cast<size_t>(i) * ncomp + 2] = cells[static_cast<size_t>(i)].vol;
    }
    exchange_doubles(data, ncomp);
    for (int i = num_cells_owned; i < static_cast<int>(cells.size()); ++i) {
      cells[static_cast<size_t>(i)].cx = data[static_cast<size_t>(i) * ncomp + 0];
      cells[static_cast<size_t>(i)].cy = data[static_cast<size_t>(i) * ncomp + 1];
      cells[static_cast<size_t>(i)].vol = data[static_cast<size_t>(i) * ncomp + 2];
    }
  }

  // ---- Rank-local vertex data (owned cells only) ----
  {
    v_map.assign(static_cast<size_t>(mesh.x.size()), -1);
    vx.clear();
    vy.clear();
    v_gid.clear();
    for (int i = 0; i < num_cells_owned; ++i) {
      const RankCell& c = cells[static_cast<size_t>(i)];
      for (int k = 0; k < c.nv; ++k) {
        const int gid = c.verts[k];
        if (v_map[static_cast<size_t>(gid)] < 0) {
          v_map[static_cast<size_t>(gid)] = static_cast<int>(v_gid.size());
          v_gid.push_back(gid);
          vx.push_back(mesh.x[static_cast<size_t>(gid)]);
          vy.push_back(mesh.y[static_cast<size_t>(gid)]);
        }
      }
    }
  }

  // ---- LSQ reconstruction geometry (owned cells) ----
  build_lsq();
}

void Partition::build_lsq() {
  lsq.clear();
  lsq.reserve(static_cast<size_t>(num_cells_owned));
  for (int i = 0; i < num_cells_owned; ++i) {
    const RankCell& c = cells[static_cast<size_t>(i)];
    LsqCell lc;
    double a00 = 0.0, a01 = 0.0, a11 = 0.0;
    for (const FaceRef& f : cell_faces[static_cast<size_t>(i)]) {
      LsqPoint p;
      if (f.other >= 0) {
        const RankCell& n = cells[static_cast<size_t>(f.other)];
        p.dx = n.cx - c.cx;
        p.dy = n.cy - c.cy;
        p.kind = 0;
        p.other = f.other;
      } else {
        // boundary stencil point at the face midpoint
        p.dx = f.fx - c.cx;
        p.dy = f.fy - c.cy;
        p.kind = 1;
        p.bc = f.bc;
        p.gbf = f.gbf;
        p.nx = f.nx;
        p.ny = f.ny;
      }
      if (p.kind == 1 && std::getenv("CFD_NO_BND_STENCIL")) continue;
      const double d2 = p.dx * p.dx + p.dy * p.dy;
      if (d2 < 1e-30) continue;
      p.w = 1.0 / d2;
      a00 += p.w * p.dx * p.dx;
      a01 += p.w * p.dx * p.dy;
      a11 += p.w * p.dy * p.dy;
      lc.pts.push_back(p);
    }
    const double det = a00 * a11 - a01 * a01;
    if (det < 1e-30) {
      // Degenerate stencil (e.g. single-cell partition): fall back to
      // zero gradient (first order) for this cell.
      lc.a00 = lc.a01 = lc.a11 = 0.0;
      lc.det = 0.0;
    } else {
      lc.a00 = a11 / det;
      lc.a01 = -a01 / det;
      lc.a11 = a00 / det;
      lc.det = det;
    }
    lsq.push_back(std::move(lc));
  }
}

void Partition::exchange_doubles(std::vector<double>& data, int ncomp) const {
  if (neighbors.empty()) return;
  std::vector<MPI_Request> reqs;
  std::vector<std::vector<double>> send_bufs(neighbors.size());
  for (size_t k = 0; k < neighbors.size(); ++k) {
    const Neighbor& nb = neighbors[k];
    std::vector<double>& sb = send_bufs[k];
    sb.resize(static_cast<size_t>(nb.send_local.size()) * ncomp);
    for (size_t i = 0; i < nb.send_local.size(); ++i) {
      const int loc = nb.send_local[i];
      for (int j = 0; j < ncomp; ++j)
        sb[i * ncomp + j] = data[static_cast<size_t>(loc) * ncomp + j];
    }
    MPI_Request r;
    MPI_Isend(sb.data(), static_cast<int>(sb.size()), MPI_DOUBLE, nb.rank,
              200, MPI_COMM_WORLD, &r);
    reqs.push_back(r);
  }
  std::vector<MPI_Request> recv_reqs;
  std::vector<std::vector<double>> recv_bufs(neighbors.size());
  for (size_t k = 0; k < neighbors.size(); ++k) {
    const Neighbor& nb = neighbors[k];
    recv_bufs[k].resize(static_cast<size_t>(nb.recv_local.size()) * ncomp);
    MPI_Request r;
    MPI_Irecv(recv_bufs[k].data(), static_cast<int>(recv_bufs[k].size()), MPI_DOUBLE,
              nb.rank, 200, MPI_COMM_WORLD, &r);
    recv_reqs.push_back(r);
  }
  if (!reqs.empty()) MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
  if (!recv_reqs.empty())
    MPI_Waitall(static_cast<int>(recv_reqs.size()), recv_reqs.data(), MPI_STATUSES_IGNORE);
  for (size_t k = 0; k < neighbors.size(); ++k) {
    const Neighbor& nb = neighbors[k];
    for (size_t i = 0; i < nb.recv_local.size(); ++i) {
      const int loc = nb.recv_local[i];
      for (int j = 0; j < ncomp; ++j)
        data[static_cast<size_t>(loc) * ncomp + j] = recv_bufs[k][i * ncomp + j];
    }
  }
}

int Partition::global_to_local(int gid) const {
  if (gid < 0 || gid >= static_cast<int>(global_to_local_map.size())) return -1;
  return global_to_local_map[static_cast<size_t>(gid)];
}

}  // namespace cfd
