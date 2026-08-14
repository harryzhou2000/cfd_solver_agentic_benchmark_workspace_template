#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

struct GlobalMesh {
  int num_nodes = 0;
  std::vector<double> node_x, node_y;

  int num_cells = 0;
  std::vector<std::array<int32_t, 4>> cell_nodes;
  std::vector<int8_t> cell_nverts;

  struct BndFace {
    int32_t n0, n1;
    int32_t family_id;
  };
  std::vector<BndFace> bnd_faces;
  std::vector<std::string> family_names;

  std::vector<double> cell_cx, cell_cy, cell_vol;
};

GlobalMesh read_cgns_mesh(const std::string& path);
void compute_cell_geometry(GlobalMesh& m);

}  // namespace cfd
