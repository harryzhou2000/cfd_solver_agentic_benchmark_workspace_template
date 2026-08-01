#pragma once

#include "cfd/types.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace cfd {

struct Node {
    GlobalIndex global_id{-1};
    Vec2 xy{};
};

// Cell vertex order is counter-clockwise.  The unused entries of vertices are -1.
struct Cell {
    GlobalIndex global_id{-1};
    std::array<GlobalIndex, 4> vertices{{-1, -1, -1, -1}};
    std::uint8_t vertex_count{}; // 3 or 4
    Vec2 centroid{};
    double area{};
};

// The normal is outward from owner (the first cell that supplied this edge).
// neighbor == -1 identifies a physical boundary face, whose tag is nonempty.
struct Face {
    GlobalIndex global_id{-1};
    std::array<GlobalIndex, 2> vertices{{-1, -1}};
    GlobalIndex owner{-1};
    GlobalIndex neighbor{-1};
    Vec2 center{};
    Vec2 normal{};
    double length{};
    std::string tag{};
};

struct Mesh {
    int cell_dimension{};
    int physical_dimension{};
    std::vector<Node> nodes;
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<std::vector<GlobalIndex>> cell_faces;
    std::vector<std::vector<GlobalIndex>> adjacency;

    [[nodiscard]] bool empty() const noexcept { return cells.empty(); }
    void clear() noexcept;
};

// Imports 2-D unstructured CGNS meshes made of TRI_3, QUAD_4, or MIXED sections.
// Abutting zone PointList/PointListDonor mappings are used to identify duplicate
// vertices; coordinate coincidence is only checked, never used as the primary key.
Mesh read_cgns_mesh(const std::filesystem::path& filename);

// Throws std::runtime_error when geometry, face incidence, or boundary tagging is
// inconsistent.  It is intentionally inexpensive enough for startup diagnostics.
void validate_mesh(const Mesh& mesh);

} // namespace cfd
