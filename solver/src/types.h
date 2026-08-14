#pragma once

#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <set>
#include <mpi.h>

namespace cfd2d {

// Conservative state: [rho, rhou, rhov, rhoE]
constexpr int NEQ = 4;
using ConsState = std::array<double, NEQ>;
using PrimState = std::array<double, NEQ>; // [rho, u, v, p]

// Element type codes (CGNS)
enum class ElemType : int {
  Tri = 5,
  Quad = 7,
  Bar = 3, // 2-node line (boundary)
};

// A raw element from CGNS reading
struct RawElement {
  ElemType type;
  std::vector<int> nodes; // 1-based global vertex ids
};

// A boundary face segment
struct BoundaryFace {
  std::string family;
  std::vector<int> nodes; // 2 nodes, 1-based global
};

// Mesh loaded from CGNS (full, before partitioning)
struct Mesh {
  // Vertices
  std::vector<double> x, y; // global vertex coords
  // Volume cells (tri/quad) as node lists
  std::vector<std::vector<int>> cellNodes; // each: 3 (tri) or 4 (quad) nodes, 1-based
  // Boundary faces (bars) grouped by family
  std::vector<BoundaryFace> bfaces;
  // Family -> BC type string from case file
  std::map<std::string, std::string> familyBC;
  // Set of interior cell indices to exclude (e.g., cells inside a body)
  std::set<int> excludedCells;
};

// Cell-centered geometry
struct CellGeo {
  double cx, cy;     // centroid
  double area;      // area (signed positive)
  std::vector<int> nodeIds; // cell vertices (1-based global)
};

// Face (interior or boundary)
struct Face {
  int l, r;          // left cell, right cell (global cell id, -1 if boundary)
  int n0, n1;        // face vertices (1-based global)
  double nx, ny;     // unit normal (pointing from l to r)
  double len;        // face length
  double mx, my;     // midpoint
  int bcType;        // -1 interior, else index into bcTypes
  std::string family;
};

// BC type enum
enum class BCType : int {
  Interior = -1,
  Farfield = 0,
  SlipWall = 1,
  NoSlipAdiabaticWall = 2,
};

inline const char* bcTypeName(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
    default: return "interior";
  }
}

} // namespace cfd2d
