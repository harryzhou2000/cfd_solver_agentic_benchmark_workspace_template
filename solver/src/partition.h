#pragma once

#include "common.h"
#include "mesh.h"

namespace cfd {

struct LocalMesh {
  int rank = 0, nranks = 1;
  int num_cells_global = 0;
  int num_faces_global = 0;
  int nowned = 0, nghost = 0;
  int edgecut = 0;

  std::vector<int> global_part;  // global cell -> owning rank (all ranks)

  std::vector<int> local_to_global;   // owned first (sorted), then ghosts (sorted)
  std::vector<int> owner_rank;        // per local cell
  std::vector<int> global_to_local;   // size num_cells_global, -1 if not local

  std::vector<double> vol, cx, cy;
  std::vector<std::vector<int>> cell_faces;      // local face ids per local cell
  std::vector<std::vector<int>> cell_face_sign;  // +1 if face_ca == cell

  std::vector<int> face_ca, face_cb;             // local cell ids; cb == -1 boundary
  std::vector<double> face_nx, face_ny, face_len;
  std::vector<BCType> face_bc;
  std::vector<int> face_bface;                   // index into boundary_faces or -1

  std::vector<BoundaryFace> boundary_faces;      // local boundary faces (owned cells)

  // Halo plan (index-aligned with `neighbors`).
  std::vector<int> neighbors;
  std::vector<std::vector<int>> send_cells;  // local owned ids to send
  std::vector<std::vector<int>> recv_cells;  // local ghost ids to receive

  bool is_owned(int li) const { return li < nowned; }
};

// Partitions the cell graph with METIS on rank 0, broadcasts the partition,
// and builds this rank's owned+ghost local mesh. Every rank calls this; only
// rank 0 computes the partition. Returns false + err on failure.
bool build_local_mesh(const Mesh& mesh, int nparts, int rank, LocalMesh& lm,
                      std::string& err);

}  // namespace cfd
