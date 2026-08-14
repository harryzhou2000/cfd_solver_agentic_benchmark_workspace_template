#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "mesh.hpp"

namespace cfd {

struct Face {
  int32_t left = -1;
  int32_t right = -1;
  int32_t n0 = -1, n1 = -1;
  double nx = 0.0, ny = 0.0;
  double area = 0.0;
  double cx = 0.0, cy = 0.0;
  double ex = 0.0, ey = 0.0, dist = 0.0;
  int32_t bc_family = -1;
};

struct LocalMesh {
  int rank = 0, np = 1;
  int n_owned = 0, n_ghost = 0;
  int64_t num_cells_global = 0, num_faces_global = 0, edge_cut = 0;

  std::vector<double> node_x, node_y;

  std::vector<std::array<int32_t, 4>> cell_nodes;
  std::vector<int8_t> cell_nverts;
  std::vector<double> cell_cx, cell_cy, cell_vol;
  std::vector<int64_t> cell_gid;
  std::vector<int32_t> cell_owner;

  std::vector<Face> faces_int;
  std::vector<Face> faces_bnd;

  std::vector<std::string> family_names;

  std::vector<int32_t> neighbor_ranks;
  std::vector<std::vector<int32_t>> send_cells;
  std::vector<std::vector<int32_t>> recv_cells;

  int num_cells_local() const { return n_owned + n_ghost; }
};

void build_partitions(const GlobalMesh& gm, int np, const std::string& out_dir,
                      std::vector<int64_t>* global_stats);
LocalMesh load_partition(const std::string& dir, int np, int rank);
std::string partition_dir(const std::string& base, int np);

}  // namespace cfd
