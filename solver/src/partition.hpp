#pragma once

#include "config.hpp"
#include "mesh.hpp"

#include <mpi.h>

#include <array>
#include <string>
#include <vector>

namespace cfd {

struct RankCell {
  int global_id = -1;
  int nv = 0;
  std::array<int, 4> verts{-1, -1, -1, -1};  // global node ids
  double cx = 0.0, cy = 0.0;
  double vol = 0.0;
  bool owned = true;
};

// One face reference of a local cell.
struct FaceRef {
  int other = -1;       // local cell index for interior faces; -1 for boundary
  double nx = 0.0, ny = 0.0;   // outward unit normal from this cell
  double len = 0.0;
  double fx = 0.0, fy = 0.0;
  BcType bc = BcType::Farfield;   // boundary type for boundary faces
  int gbf = -1;                   // global boundary face index or -1
};

// Rank-local mesh: owned cells + one layer of ghost cells.  Solver iterations
// only use this data; the global mesh is discarded after partitioning.
struct Partition {
  int rank = 0;
  int nranks = 1;
  int num_cells_global = 0;
  int num_faces_global = 0;    // interior + boundary
  int num_cells_owned = 0;
  int num_cells_ghost = 0;
  int edgecut = 0;

  std::vector<RankCell> cells;      // [0, num_cells_owned) owned, then ghosts
  std::vector<std::vector<FaceRef>> cell_faces;  // per local cell
  std::vector<BFace> bfaces;        // local boundary faces (owned cells only)
  std::vector<int> bface_gbf;       // global boundary face index per local bface

  // Rank-local vertex data: coordinates of the vertices used by owned cells
  // (used for field output; solver iterations never touch this).
  std::vector<double> vx, vy;
  std::vector<int> v_gid;           // global node id per local vertex
  std::vector<int> v_map;           // global node id -> local vertex index (-1)

  // Halo exchange: per neighbor rank, the owned local indices to send and the
  // ghost local indices to receive (aligned with the neighbor's send list).
  struct Neighbor {
    int rank = -1;
    std::vector<int> send_local;   // owned local indices
    std::vector<int> recv_local;   // ghost local indices
  };
  std::vector<Neighbor> neighbors;

  // METIS partition of every global cell (preprocessing; not used in solver
  // iterations).  Kept for diagnostics.
  std::vector<int> global_part;

  // LSQ reconstruction geometry (precomputed per owned cell).
  struct LsqPoint {
    double dx = 0.0, dy = 0.0, w = 0.0;
    int kind = 0;      // 0 = interior neighbor (local index in `other`),
                       // 1 = boundary face (bc type in `bc`, gbf in `gbf`)
    int other = -1;
    BcType bc = BcType::Farfield;
    int gbf = -1;
    double nx = 0.0, ny = 0.0;   // boundary face outward normal (kind==1)
  };
  struct LsqCell {
    double a00 = 0.0, a01 = 0.0, a11 = 0.0, det = 0.0;   // inverse moment matrix
    std::vector<LsqPoint> pts;
  };
  std::vector<LsqCell> lsq;   // per owned cell

  // Freestream primitive state (rho, u, v, p) for farfield BCs.
  std::array<double, 4> q_inf{1.0, 1.0, 0.0, 1.0};
  double gamma = 1.4;

  // Partition the global mesh with METIS (k-way), build the rank-local mesh
  // and halo exchange lists.  Called by every rank.
  void build(const GlobalMesh& mesh, const CaseConfig& cfg, int rank, int nranks,
             double rho_inf, double u_inf, double v_inf, double p_inf);

  // Precompute the per-owned-cell least-squares stencil geometry.
  void build_lsq();

  // Generic halo exchange for ncomp doubles per cell.
  void exchange_doubles(std::vector<double>& data, int ncomp) const;

  // Map a global cell id to its local index on this rank, or -1.
  int global_to_local(int gid) const;
  std::vector<int> global_to_local_map;   // sized num_cells_global, -1 if not local
};

}  // namespace cfd
