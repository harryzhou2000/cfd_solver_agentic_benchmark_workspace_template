#include "mesh_local.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

namespace cfd {
namespace {

struct FileHeader {
  uint32_t nranks = 0;
  uint32_t rank = 0;
  uint64_t n_owned = 0;
  uint64_t n_ghost = 0;
  uint64_t n_faces = 0;
  uint64_t num_cells_global = 0;
  uint64_t num_faces_global = 0;
};

}  // namespace

LocalMesh load_local_mesh(const std::string& outdir, int rank) {
  const std::string path =
      outdir + "/partition/rank_" + std::to_string(rank) + ".bin";
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open partition file " + path);
  auto get = [&](void* data, size_t n) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
    if (!in) throw std::runtime_error("truncated partition file " + path);
  };

  FileHeader h;
  get(&h, sizeof(h));
  if (h.rank != static_cast<uint32_t>(rank) ||
      h.nranks < 1)
    throw std::runtime_error("partition file header mismatch for rank " +
                             std::to_string(rank));

  LocalMesh m;
  m.rank = rank;
  m.nranks = h.nranks;
  m.n_owned = h.n_owned;
  m.n_ghost = h.n_ghost;
  m.num_cells_global = h.num_cells_global;
  m.num_faces_global = h.num_faces_global;
  m.cells.resize(m.n_cells_local());

  for (int i = 0; i < m.n_owned; ++i) {
    int32_t id = 0;
    get(&id, 4);
    double geo[3] = {0};
    get(geo, 24);
    auto& c = m.cells[i];
    c.global_id = id;
    c.owner_rank = rank;
    c.vol = geo[0];
    c.cx = geo[1];
    c.cy = geo[2];
  }
  for (int i = 0; i < m.n_ghost; ++i) {
    int32_t data[2] = {0};
    get(data, 8);
    double geo[3] = {0};
    get(geo, 24);
    auto& c = m.cells[m.n_owned + i];
    c.global_id = data[0];
    c.owner_rank = data[1];
    c.vol = geo[0];
    c.cx = geo[1];
    c.cy = geo[2];
  }

  // Global id -> local cell index.
  std::map<int, int> local_of_global;
  for (int i = 0; i < m.n_cells_local(); ++i)
    local_of_global[m.cells[i].global_id] = i;

  m.faces.resize(h.n_faces);
  for (size_t k = 0; k < m.faces.size(); ++k) {
    int32_t data[6] = {0};
    get(data, 24);
    auto& f = m.faces[k];
    f.global_id = data[0];
    f.n0 = data[1];
    f.n1 = data[2];
    f.c0 = local_of_global.at(data[3]);
    f.c1 = (data[4] < 0) ? -1 : local_of_global.at(data[4]);
    f.bc = static_cast<BCType>(data[5]);
    uint32_t nlen = 0;
    get(&nlen, 4);
    f.bc_name.resize(nlen);
    get(&f.bc_name[0], nlen);
    double geo[5] = {0};
    get(geo, 40);
    f.area = geo[0];
    f.nx = geo[1];
    f.ny = geo[2];
    f.fx = geo[3];
    f.fy = geo[4];
    if (f.c1 < 0) {
      // Mirror ghost distance: 2 * (cell center -> face midpoint)
      const double dx = f.fx - m.cells[f.c0].cx;
      const double dy = f.fy - m.cells[f.c0].cy;
      f.dist = 2.0 * std::sqrt(dx * dx + dy * dy);
      if (f.dist < 1e-30) f.dist = 1e-30;
      m.boundary_faces.push_back(static_cast<int>(k));
    } else {
      const double dx = m.cells[f.c1].cx - m.cells[f.c0].cx;
      const double dy = m.cells[f.c1].cy - m.cells[f.c0].cy;
      f.dist = std::sqrt(dx * dx + dy * dy);
      if (f.dist < 1e-30) f.dist = 1e-30;
    }
  }

  // Per-owned-cell face lists.
  for (int i = 0; i < m.n_owned; ++i) {
    uint32_t nf = 0;
    get(&nf, 4);
    m.cells[i].faces.resize(nf);
    for (uint32_t k = 0; k < nf; ++k) {
      int32_t li = 0;
      get(&li, 4);
      m.cells[i].faces[k] = li;
    }
  }

  // Halo structure.
  uint32_t nneigh = 0;
  get(&nneigh, 4);
  m.halos.resize(nneigh);
  for (auto& hl : m.halos) {
    int32_t hrank = 0;
    get(&hrank, 4);
    hl.rank = hrank;
    uint64_t ns = 0, nr = 0;
    get(&ns, 8);
    hl.send_ids.resize(ns);
    hl.send_local.resize(ns);
    for (uint64_t k = 0; k < ns; ++k) {
      int32_t v = 0;
      get(&v, 4);
      hl.send_ids[k] = v;
      hl.send_local[k] = local_of_global.at(v);
    }
    get(&nr, 8);
    hl.recv_ids.resize(nr);
    hl.recv_local.resize(nr);
    for (uint64_t k = 0; k < nr; ++k) {
      int32_t v = 0;
      get(&v, 4);
      hl.recv_ids[k] = v;
    }
    for (uint64_t k = 0; k < nr; ++k) {
      int32_t v = 0;
      get(&v, 4);
      hl.recv_local[k] = v;
    }
  }
  return m;
}

void halo_exchange(const LocalMesh& m, MPI_Comm comm, int ncomp, double* data) {
  if (m.nranks == 1) return;
  const int nhalo = static_cast<int>(m.halos.size());
  std::vector<MPI_Request> reqs;
  reqs.reserve(2 * nhalo);
  std::vector<std::vector<double>> sendbuf(nhalo), recvbuf(nhalo);

  for (int h = 0; h < nhalo; ++h) {
    const auto& hl = m.halos[h];
    sendbuf[h].resize(hl.send_local.size() * ncomp);
    recvbuf[h].resize(hl.recv_local.size() * ncomp);
    for (size_t i = 0; i < hl.send_local.size(); ++i) {
      const double* src = data + hl.send_local[i] * ncomp;
      std::copy(src, src + ncomp, sendbuf[h].data() + i * ncomp);
    }
    MPI_Request r1, r2;
    MPI_Isend(sendbuf[h].data(), static_cast<int>(sendbuf[h].size()), MPI_DOUBLE,
              hl.rank, 101, comm, &r1);
    MPI_Irecv(recvbuf[h].data(), static_cast<int>(recvbuf[h].size()), MPI_DOUBLE,
              hl.rank, 101, comm, &r2);
    reqs.push_back(r1);
    reqs.push_back(r2);
  }
  if (!reqs.empty())
    MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);

  for (int h = 0; h < nhalo; ++h) {
    const auto& hl = m.halos[h];
    for (size_t i = 0; i < hl.recv_local.size(); ++i) {
      const double* src = recvbuf[h].data() + i * ncomp;
      std::copy(src, src + ncomp, data + hl.recv_local[i] * ncomp);
    }
  }
}

}  // namespace cfd
