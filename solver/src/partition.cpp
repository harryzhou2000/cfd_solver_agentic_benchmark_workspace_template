#include "partition.h"

#include <metis.h>

namespace cfd {
namespace {

int metis_partition(const Mesh& mesh, int nparts, std::vector<int32_t>& part, int& edgecut) {
  int ncells = mesh.num_cells_global;
  // Build CSR adjacency from interior faces.
  std::vector<int32_t> xadj(ncells + 1, 0);
  for (const Face& f : mesh.faces) {
    if (f.cb >= 0) {
      xadj[f.ca + 1]++;
      xadj[f.cb + 1]++;
    }
  }
  for (int i = 0; i < ncells; ++i) xadj[i + 1] += xadj[i];
  std::vector<int32_t> adjncy(xadj[ncells]);
  std::vector<int32_t> cursor(ncells);
  for (int i = 0; i < ncells; ++i) cursor[i] = xadj[i];
  for (const Face& f : mesh.faces) {
    if (f.cb >= 0) {
      adjncy[cursor[f.ca]++] = f.cb;
      adjncy[cursor[f.cb]++] = f.ca;
    }
  }
  part.resize(ncells);
  int32_t ncon = 1;
  int32_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 0;   // 0-based
  options[METIS_OPTION_SEED] = 42;
  options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
  int32_t objval = 0;
  int ret = METIS_PartGraphKway(&ncells, &ncon, xadj.data(), adjncy.data(),
                                nullptr, nullptr, nullptr, &nparts, nullptr,
                                nullptr, options, &objval, part.data());
  edgecut = (int)objval;
  return ret;
}

}  // namespace

bool build_local_mesh(const Mesh& mesh, int nparts, int rank, LocalMesh& lm,
                      std::string& err) {
  int ncells = mesh.num_cells_global;
  std::vector<int32_t> part;
  int edgecut = 0;
  int ret = METIS_OK;
  if (nparts == 1) {
    if (rank == 0) {
      part.assign(ncells, 0);
      edgecut = 0;
    }
  } else if (rank == 0) {
    ret = metis_partition(mesh, nparts, part, edgecut);
    if (ret != METIS_OK) {
      err = "METIS_PartGraphKway failed with code " + std::to_string(ret);
      return false;
    }
  }
  MPI_Bcast(&edgecut, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (rank != 0) part.resize(ncells);
  MPI_Bcast(part.data(), ncells, MPI_INT, 0, MPI_COMM_WORLD);

  lm.rank = rank;
  lm.nranks = nparts;
  lm.num_cells_global = ncells;
  lm.num_faces_global = mesh.num_faces_global;
  lm.edgecut = edgecut;
  lm.global_to_local.assign(ncells, -1);
  lm.global_part.assign(part.begin(), part.end());

  // Owned cells: part == rank, in ascending global order.
  std::vector<int> owned;
  for (int g = 0; g < ncells; ++g) {
    if (part[g] == rank) owned.push_back(g);
  }
  // Ghost cells: any cell adjacent (via interior faces) to an owned cell with a
  // different owner. Sorted by global id.
  std::set<int> ghost_set;
  for (int g : owned) {
    // gather face neighbors of cell g
    for (const auto& cf : mesh.cell_faces[g]) {
      const Face& f = mesh.faces[cf];
      int nb = (f.ca == g) ? f.cb : f.ca;
      if (nb >= 0 && part[nb] != rank) ghost_set.insert(nb);
    }
  }
  std::vector<int> ghosts(ghost_set.begin(), ghost_set.end());

  lm.nowned = (int)owned.size();
  lm.nghost = (int)ghosts.size();
  lm.local_to_global.reserve(lm.nowned + lm.nghost);
  lm.owner_rank.reserve(lm.nowned + lm.nghost);
  for (int g : owned) {
    int li = (int)lm.local_to_global.size();
    lm.local_to_global.push_back(g);
    lm.owner_rank.push_back(rank);
    lm.global_to_local[g] = li;
  }
  for (int g : ghosts) {
    int li = (int)lm.local_to_global.size();
    lm.local_to_global.push_back(g);
    lm.owner_rank.push_back(part[g]);
    lm.global_to_local[g] = li;
  }

  // Local cell geometry.
  int nlocal = lm.nowned + lm.nghost;
  lm.vol.resize(nlocal);
  lm.cx.resize(nlocal);
  lm.cy.resize(nlocal);
  lm.cell_faces.resize(nlocal);
  lm.cell_face_sign.resize(nlocal);
  for (int li = 0; li < nlocal; ++li) {
    const Cell& c = mesh.cells[lm.local_to_global[li]];
    lm.vol[li] = c.vol;
    lm.cx[li] = c.cx;
    lm.cy[li] = c.cy;
  }

  // Local faces: any global face with at least one owned endpoint.
  std::vector<int> face_map(mesh.faces.size(), -1);  // global face -> local face
  for (int gf = 0; gf < (int)mesh.faces.size(); ++gf) {
    const Face& f = mesh.faces[gf];
    int lca = lm.global_to_local[f.ca];
    int lcb = (f.cb >= 0) ? lm.global_to_local[f.cb] : -1;
    bool include = (lca >= 0 && lm.is_owned(lca));
    if (!include && lcb >= 0) include = lm.is_owned(lcb);
    if (!include) continue;
    int lf = (int)lm.face_ca.size();
    face_map[gf] = lf;
    lm.face_ca.push_back(lca);
    lm.face_cb.push_back(lcb);
    lm.face_nx.push_back(f.nx);
    lm.face_ny.push_back(f.ny);
    lm.face_len.push_back(f.len);
    lm.face_bc.push_back(f.bc);
    lm.face_bface.push_back(f.bface);
    if (lca >= 0) {
      lm.cell_faces[lca].push_back(lf);
      lm.cell_face_sign[lca].push_back(+1);
    }
    if (lcb >= 0) {
      lm.cell_faces[lcb].push_back(lf);
      lm.cell_face_sign[lcb].push_back(-1);
    }
  }

  // Local boundary faces (owned cells only).
  for (const BoundaryFace& bf : mesh.boundary_faces) {
    int li = lm.global_to_local[bf.cell];
    if (li < 0 || !lm.is_owned(li)) continue;
    int lf = face_map[bf.face];
    if (lf < 0) continue;
    BoundaryFace lb = bf;
    lb.face = lf;
    lb.cell = li;
    int idx = (int)lm.boundary_faces.size();
    lm.boundary_faces.push_back(lb);
    lm.face_bface[lf] = idx;
  }

  // Halo plan: recv lists per owner rank, then exchange to build send lists.
  std::map<int, std::vector<int>> recv_by_owner;
  for (int li = lm.nowned; li < nlocal; ++li) {
    recv_by_owner[lm.owner_rank[li]].push_back(lm.local_to_global[li]);
  }
  // Alltoall counts of recv lists.
  std::vector<int> send_counts(nparts, 0), recv_counts(nparts, 0);
  for (auto& kv : recv_by_owner) send_counts[kv.first] = (int)kv.second.size();
  MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);
  // Build send buffer (concatenation of recv lists, in rank order).
  std::vector<int> send_disp(nparts, 0), recv_disp(nparts, 0);
  int total_send = 0, total_recv = 0;
  for (int q = 0; q < nparts; ++q) {
    send_disp[q] = total_send;
    total_send += send_counts[q];
    recv_disp[q] = total_recv;
    total_recv += recv_counts[q];
  }
  std::vector<int> send_buf(total_send), recv_buf(total_recv);
  for (auto& kv : recv_by_owner) {
    std::copy(kv.second.begin(), kv.second.end(), send_buf.begin() + send_disp[kv.first]);
  }
  MPI_Alltoallv(send_buf.data(), send_counts.data(), send_disp.data(), MPI_INT,
                recv_buf.data(), recv_counts.data(), recv_disp.data(), MPI_INT,
                MPI_COMM_WORLD);
  // recv_buf[q] = global ids of cells owned by me that rank q needs.
  std::map<int, std::vector<int>> send_by_rank;
  for (int q = 0; q < nparts; ++q) {
    for (int k = 0; k < recv_counts[q]; ++k) {
      int g = recv_buf[recv_disp[q] + k];
      int li = lm.global_to_local[g];
      if (li < 0 || !lm.is_owned(li)) {
        err = "halo construction error: received request for non-owned cell";
        return false;
      }
      send_by_rank[q].push_back(li);
    }
  }
  std::set<int> nbr_set;
  for (auto& kv : recv_by_owner) nbr_set.insert(kv.first);
  for (auto& kv : send_by_rank) nbr_set.insert(kv.first);
  lm.neighbors.assign(nbr_set.begin(), nbr_set.end());
  for (int q : lm.neighbors) {
    // Convert the received global ids to local ghost ids.
    std::vector<int> recv_local;
    recv_local.reserve(recv_by_owner[q].size());
    for (int g : recv_by_owner[q]) {
      int li = lm.global_to_local[g];
      if (li < 0 || lm.is_owned(li)) {
        err = "halo construction error: invalid ghost local id";
        return false;
      }
      recv_local.push_back(li);
    }
    lm.recv_cells.push_back(std::move(recv_local));
    lm.send_cells.push_back(send_by_rank[q]);
  }

  return true;
}

}  // namespace cfd
