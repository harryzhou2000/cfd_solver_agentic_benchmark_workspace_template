// MPI distribution: METIS cell-graph partitioning and rank-local mesh
// construction. During setup, rank 0 loads the full mesh, partitions the cell
// adjacency graph with METIS, builds a LocalMesh (owned + ghost cells and the
// faces touching owned cells) for every rank, and scatters one LocalMesh per
// rank via MPI_Scatterv. During iterations each rank holds ONLY its LocalMesh
// -- no full-mesh / full-state replication (see halo.hpp for ghost exchange).
//
// Ghost layer is one cell deep, which is sufficient for cell-centered linear
// reconstruction + Barth-Jespersen limiting (both use only face-adjacent
// neighbors). Ghost states are filled by neighbor-scoped halo exchange before
// each reconstruction; they are never solved.
#pragma once
#include "mesh.hpp"
#include "types.hpp"
#include <string>
#include <vector>

namespace cfd {

struct LocalFace {
  Vec2 p1, p2, center;
  double Sx = 0, Sy = 0, len = 0;  // area-weighted normal (lc -> rc)
  int lc = -1;   // local index of left/owner cell
  int rc = -1;   // local index of right cell, -1 if real boundary
  BCType bctype = BCType::Internal;
  std::string family;
  int global_face = -1;
  // neighbor-rank bookkeeping for halo: if rc is a ghost owned by another
  // rank, that rank id is stored in rc_owner (and symmetric for lc).
  int lc_owner = -1;
  int rc_owner = -1;
};

struct LocalMesh {
  int rank = 0, nranks = 1;
  int n_owned = 0, n_ghost = 0;
  // per local cell (owned then ghost)
  std::vector<Vec2> center;
  std::vector<double> area;
  std::vector<int> global_id;     // local -> global cell id
  std::vector<int> owner;         // local -> owning rank
  std::vector<std::vector<Vec2>> verts;  // owned cells' vertices (ghost entries empty)
  std::vector<std::vector<int>> cell_faces;  // owned cells -> local face ids (ghost: empty)
  std::vector<LocalFace> faces;
  std::vector<int> wall_face_ids;     // local boundary faces on walls
  std::vector<int> farfield_face_ids;
  // halo communication plan
  std::vector<int> neighbor_ranks;
  std::vector<std::vector<int>> send_local;  // per neighbor: owned local cells whose state we send
  std::vector<std::vector<int>> recv_local;  // per neighbor: ghost local cells we receive into
  // partition diagnostics (global, computed on rank 0 and broadcast)
  int num_cells_global = 0;
  int num_faces_global = 0;
  int edge_cut = 0;
  std::string partitioner = "metis_kway";
  std::string halo_exchange = "neighbor_isend_irecv";
  Vec2 bbox_min{0,0}, bbox_max{0,0};
  double diag = 1.0;
  std::string mesh_file;
};

// Rank 0 builds all rank-local meshes from the global mesh + METIS partition
// and scatters one per rank. Returns this rank's LocalMesh. The global mesh is
// freed by the caller after this returns.
LocalMesh distribute_mesh(const Mesh& global, int rank, int nranks);

// Serialize/deserialize a LocalMesh to a flat byte buffer (also used for the
// partition files written for reproducibility / examiner inspection).
std::vector<char> serialize_local_mesh(const LocalMesh& m);
LocalMesh deserialize_local_mesh(const char* data, size_t n);

}  // namespace cfd
