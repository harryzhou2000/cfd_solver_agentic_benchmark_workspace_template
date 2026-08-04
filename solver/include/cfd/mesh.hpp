#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

namespace cfd {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

enum class CellType : std::uint8_t { triangle = 3, quadrilateral = 4 };
enum class BoundaryType : std::uint8_t {
    interior,
    farfield,
    slip_wall,
    no_slip_adiabatic_wall,
    unspecified
};

/// Maps CGNS BC/family names to the case-file boundary condition names.
using BoundaryMap = std::map<std::string, BoundaryType>;

BoundaryType boundary_type_from_string(const std::string& name);
BoundaryMap boundary_map_from_strings(const std::unordered_map<std::string, std::string>& values);
const char* boundary_type_name(BoundaryType type) noexcept;

struct Cell {
    std::vector<int> nodes;       // global node ids, counter-clockwise
    std::vector<int> faces;       // global face ids in matching loop order
    std::vector<int> neighbors;   // global neighboring cell ids, -1 at boundaries
    Vec2 centroid;
    double area = 0.0;
    CellType type = CellType::triangle;
};

struct Face {
    std::array<int, 2> nodes{{-1, -1}}; // global node ids
    int left_cell = -1;                 // owner of the stored outward normal
    int right_cell = -1;                // -1 for a physical boundary
    Vec2 centroid;
    Vec2 normal;                        // length-weighted outward normal of left_cell
    Vec2 unit_normal;
    double length = 0.0;
    BoundaryType boundary_type = BoundaryType::interior;
    std::string boundary_name;
};

struct Mesh {
    std::vector<Vec2> nodes;
    std::vector<Cell> cells;
    std::vector<Face> faces;
};

/// Read a 2-D unstructured CGNS file, merge coincident zone nodes, and build
/// cells/faces/geometry.  Only TRI_3 and QUAD_4 volume elements are accepted.
/// Boundary names are taken from BC nodes (prefer FamilyName when present) and
/// translated through boundary_map; unmapped physical faces remain unspecified.
Mesh read_cgns_mesh(const std::string& path, const BoundaryMap& boundary_map,
                    double merge_tolerance = 1.0e-10);

std::vector<std::vector<int>> cell_adjacency(const Mesh& mesh);

} // namespace cfd
