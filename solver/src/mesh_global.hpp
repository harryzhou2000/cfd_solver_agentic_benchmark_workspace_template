#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "case_io.hpp"

namespace cfd {

// Global mesh representation used only during preprocessing (rank 0) and
// for final field-file I/O. Solver iterations use the rank-local mesh.
struct GlobalMesh {
  struct Cell {
    std::vector<int> nodes;  // global node ids, ordered around the cell
    double vol = 0.0;
    double cx = 0.0, cy = 0.0;
    int zone = 0;
  };
  struct Face {
    int n0 = -1, n1 = -1;   // global node ids
    int c0 = -1, c1 = -1;   // adjacent cells; c1 == -1 for boundary faces
    BCType bc = BCType::Interior;
    std::string bc_name;
    double area = 0.0;      // face length (2-D)
    double nx = 0.0, ny = 0.0;  // unit normal c0 -> c1 (outward at boundary)
    double fx = 0.0, fy = 0.0;  // face midpoint
    int zone = 0;
    int64_t global_element_index = -1;  // CGNS element index (for debugging)
  };

  std::vector<double> node_x, node_y;
  std::vector<Cell> cells;
  std::vector<Face> faces;

  int num_cells_global() const { return static_cast<int>(cells.size()); }
  int num_faces_global() const { return static_cast<int>(faces.size()); }
};

// Reads a CGNS mesh (possibly multi-zone) and builds the global cell/face
// topology. Throws std::runtime_error with a descriptive message on failure.
GlobalMesh read_cgns_mesh(const std::string& path, const Case& c);

void write_global_mesh_bin(const GlobalMesh& m, const std::string& path);
GlobalMesh read_global_mesh_bin(const std::string& path);

}  // namespace cfd
