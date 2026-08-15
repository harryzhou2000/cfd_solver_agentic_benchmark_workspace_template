#pragma once

#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

// Global (preprocessing) mesh: nodes, cells, interior faces, boundary faces.
// Built once by every rank from the CGNS file, then partitioned with METIS;
// solver iterations only ever use the rank-local partition (see partition.hpp).
struct Cell {
  int nv = 0;
  std::array<int, 4> v{-1, -1, -1, -1};  // global node ids
  double cx = 0.0, cy = 0.0;             // centroid
  double vol = 0.0;                      // area
  int bf_index = -1;                     // boundary face index if the cell touches a boundary
};

// Broadcast a GlobalMesh from rank 0 to all other ranks.


struct Face {
  int cL = -1, cR = -1;   // global cell ids; normal points from cL to cR
  double nx = 0.0, ny = 0.0;
  double len = 0.0;
  double fx = 0.0, fy = 0.0;
};

// Broadcast a GlobalMesh from rank 0 to all other ranks.


struct BFace {
  int cell = -1;          // global cell id
  double nx = 0.0, ny = 0.0;   // outward unit normal
  double len = 0.0;
  double fx = 0.0, fy = 0.0;
  BcType type = BcType::Farfield;
  std::string family;     // mesh family / section name (for tags)
};

// Broadcast a GlobalMesh from rank 0 to all other ranks.


struct GlobalMesh {
  std::vector<double> x, y;           // nodes (size nnodes)
  std::vector<Cell> cells;            // size num_cells_global
  std::vector<Face> faces;            // interior faces
  std::vector<BFace> bfaces;          // boundary faces
  std::vector<int64_t> bface_edges;   // node-pair key for each boundary face
  std::string mesh_file;
  std::vector<std::string> families;  // all base-level family names
};

// Broadcast a GlobalMesh from rank 0 to all other ranks.


// Read a CGNS 2-D unstructured mesh and build geometry + face adjacency.
// Resolves boundary sections to BC types through the case mapping (generic
// resolution, documented in the report).
GlobalMesh read_mesh(const CaseConfig& cfg);
void broadcast_mesh(GlobalMesh& mesh, int rank);

// Serialize a GlobalMesh into a byte buffer for MPI broadcast.
// Returns the buffer and the size in bytes.
void mesh_to_buffer(const GlobalMesh& mesh, std::vector<char>& buf);

// Reconstruct a GlobalMesh from a byte buffer (broadcast from rank 0).
void mesh_from_buffer(const std::vector<char>& buf, GlobalMesh& mesh);


// Global cell id of the neighbor across the boundary face index (for
// verification / diagnostics).
int64_t face_key(int a, int b);

}  // namespace cfd

// Broadcast a GlobalMesh from rank 0 to all other ranks.
// CGNS is not MPI-safe for concurrent reads, so the mesh must be read
// on rank 0 and distributed via MPI_Bcast.
