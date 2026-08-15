#pragma once

#include "cfd/Mesh.hpp"

#include <mpi.h>

#include <map>
#include <string>
#include <vector>

namespace cfd {

struct LocalCell {
  int global_id = -1;
  int owner_rank = -1;
  Vec2 center;
  double volume = 0.0;
  std::vector<Vec2> polygon;
};

struct LocalFace {
  int left = -1;
  int right = -1;
  Vec2 node0;
  Vec2 node1;
  Vec2 center;
  Vec2 normal;  // points out of left
  double area = 0.0;
  std::string boundary_tag;
};

struct HaloPeer {
  int rank = -1;
  std::vector<int> send_cells;
  std::vector<int> receive_ghosts;
};

struct LocalMesh {
  int rank = 0;
  int ranks = 1;
  int global_cells = 0;
  int global_faces = 0;
  int partition_edge_cut = 0;
  int owned_count = 0;
  std::vector<LocalCell> cells;  // owned cells first, then ghosts
  std::vector<LocalFace> faces;
  std::vector<HaloPeer> halo;

  int ghost_count() const { return static_cast<int>(cells.size()) - owned_count; }
  void validate() const;
};

class MeshDistributor {
 public:
  // Rank zero performs CGNS import and METIS preprocessing. Each rank receives only
  // its owned cells, one-ring ghosts, incident faces, and neighbor exchange maps.
  static LocalMesh load_partition(const std::string& mesh_path, MPI_Comm communicator);
};

}  // namespace cfd
