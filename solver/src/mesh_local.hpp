#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "case_io.hpp"

namespace cfd {

// Rank-local mesh: owned cells + ghost cells + local faces.
// Built by loading only this rank's partition file.
struct LocalMesh {
  struct Cell {
    int global_id = -1;
    int owner_rank = -1;
    double vol = 0.0, cx = 0.0, cy = 0.0;
    std::vector<int> faces;  // local face indices
    bool is_owned() const { return owner_rank >= 0; }
  };
  struct Face {
    int global_id = -1;
    int n0 = -1, n1 = -1;      // global node ids (for surface output)
    int c0 = -1, c1 = -1;      // local cell ids; c1 < 0 for boundary faces
    BCType bc = BCType::Interior;
    std::string bc_name;
    double area = 0.0, nx = 0.0, ny = 0.0, fx = 0.0, fy = 0.0;
    double dist = 0.0;         // |x(c1)-x(c0)| (mirror distance at boundary)
    bool boundary() const { return c1 < 0; }
  };
  struct Halo {
    int rank = -1;
    std::vector<int> send_ids;    // global ids of my owned cells to send
    std::vector<int> send_local;  // local cell ids to pack from
    std::vector<int> recv_ids;    // global ids of ghost cells to receive
    std::vector<int> recv_local;  // local cell ids to unpack into
  };

  int rank = 0, nranks = 1;
  int n_owned = 0, n_ghost = 0;
  int num_cells_global = 0, num_faces_global = 0;
  std::vector<Cell> cells;   // owned first (0..n_owned-1), then ghosts
  std::vector<Face> faces;   // only faces with at least one owned cell
  std::vector<Halo> halos;
  std::vector<int> boundary_faces;  // local indices of boundary faces

  int n_cells_local() const { return n_owned + n_ghost; }
};

// Loads this rank's partition file (rank-local mesh only).
LocalMesh load_local_mesh(const std::string& outdir, int rank);

// Neighbor-scoped halo exchange of ncomp-component cell data.
//   data: values for all local cells (owned + ghost); only owned values are
//         sent; received values overwrite the ghost entries in `data`.
void halo_exchange(const LocalMesh& m, MPI_Comm comm, int ncomp, double* data);

}  // namespace cfd
