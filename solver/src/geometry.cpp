/// @file geometry.cpp
/// Implementation of cell/face geometry computation.

#include "geometry.hpp"

#include "logging.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cfd {

namespace {

/// Sorted node-pair key identifying an undirected edge.
struct EdgeKey {
    std::size_t lo;
    std::size_t hi;

    bool operator==(const EdgeKey& other) const {
        return lo == other.lo && hi == other.hi;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const {
        // Splitmix64-style mixing of the two node ids.
        std::size_t h = k.lo + 0x9e3779b97f4a7c15ULL;
        h ^= (k.hi + 0x9e3779b97f4a7c15ULL) + (h << 6) + (h >> 2);
        return h;
    }
};

struct EdgeEntry {
    std::array<std::size_t, 2> nodes;      ///< node pair as encountered
    std::vector<std::size_t> cells;        ///< cells containing this edge
};

using EdgeMap = std::unordered_map<EdgeKey, EdgeEntry, EdgeKeyHash>;

/// Add the edge (a, b) of cell `ci` to the edge map.
void add_edge(EdgeMap& edges, std::size_t a, std::size_t b, std::size_t ci) {
    EdgeKey key{std::min(a, b), std::max(a, b)};
    auto& entry = edges[key];
    if (entry.cells.empty()) {
        entry.nodes = {a, b};
    }
    entry.cells.push_back(ci);
}

/// Unit normal of the directed edge p0 -> p1 rotated by +90 degrees:
/// n = (dy, -dx) / |edge|.
Vec2 edge_normal(const Vec2& p0, const Vec2& p1) {
    Vec2 d = p1 - p0;
    const Real len = d.norm();
    if (len < SMALL) {
        throw std::runtime_error("Degenerate face (zero length edge) in mesh");
    }
    Vec2 n;
    n << d.y(), -d.x();
    return n / len;
}

} // namespace

void compute_geometry(Mesh& mesh) {
    const std::size_t n_cells = mesh.cells.size();

    // --- 1. Cell centroids and volumes (shoelace formula) ----------------------
    for (auto& cell : mesh.cells) {
        if (cell.nodes.size() < 3) {
            throw std::runtime_error("Cell with fewer than 3 nodes in mesh");
        }
        const std::size_t n = cell.nodes.size();

        // Centroid: average of node positions.
        Vec2 c = Vec2::Zero();
        for (std::size_t i = 0; i < n; ++i) {
            c += mesh.nodes[cell.nodes[i]].coord;
        }
        cell.centroid = c / static_cast<Real>(n);

        // Volume (area): shoelace formula, |sum(x_i * y_{i+1} - x_{i+1} * y_i)| / 2.
        Real twice = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec2& p = mesh.nodes[cell.nodes[i]].coord;
            const Vec2& q = mesh.nodes[cell.nodes[(i + 1) % n]].coord;
            twice += p.x() * q.y() - q.x() * p.y();
        }
        cell.volume = std::abs(twice) * 0.5;
        if (cell.volume < SMALL) {
            throw std::runtime_error("Cell with (near-)zero area in mesh");
        }
    }

    // --- 2. Global bounding box -------------------------------------------------
    if (mesh.nodes.empty()) {
        throw std::runtime_error("Mesh contains no nodes");
    }
    mesh.min_coord = mesh.nodes[0].coord;
    mesh.max_coord = mesh.nodes[0].coord;
    for (const auto& node : mesh.nodes) {
        mesh.min_coord = mesh.min_coord.cwiseMin(node.coord);
        mesh.max_coord = mesh.max_coord.cwiseMax(node.coord);
    }

    // --- 3. Edge map: every cell edge -> owning cells ---------------------------
    EdgeMap edges;
    edges.reserve(mesh.n_faces() + n_cells * 2);
    for (std::size_t ci = 0; ci < n_cells; ++ci) {
        const auto& nodes = mesh.cells[ci].nodes;
        const std::size_t n = nodes.size();
        for (std::size_t i = 0; i < n; ++i) {
            add_edge(edges, nodes[i], nodes[(i + 1) % n], ci);
        }
    }

    // --- 4. Internal faces from edges shared by two cells -------------------------
    // Boundary faces were created by the mesh reader and already live at the
    // front of mesh.faces; internal faces are appended afterwards.
    const std::size_t n_boundary_faces = mesh.faces.size();
    std::size_t n_skipped = 0;

    for (const auto& [key, entry] : edges) {
        if (entry.cells.size() == 1) {
            // Edge with a single cell: boundary edge or zone-interface edge.
            continue;  // handled below (boundary) or skipped (interface)
        }
        if (entry.cells.size() > 2) {
            LOG_WARN("Non-manifold edge ({}, {}) shared by {} cells; skipping", key.lo, key.hi,
                     entry.cells.size());
            ++n_skipped;
            continue;
        }

        const std::size_t c0 = entry.cells[0];
        const std::size_t c1 = entry.cells[1];
        const Vec2& p0 = mesh.nodes[key.lo].coord;
        const Vec2& p1 = mesh.nodes[key.hi].coord;

        Face face;
        face.nodes = {key.lo, key.hi};
        face.centroid = (p0 + p1) * 0.5;
        face.area = (p1 - p0).norm();
        face.is_boundary = false;
        face.bc_tag = 0;
        face.left_cell = Face::INVALID;
        face.right_cell = Face::INVALID;

        // Orient so the (dy, -dx) normal points from the left cell to the
        // right cell. The cell whose centroid lies on the +n side is right.
        Vec2 n = edge_normal(p0, p1);
        const Vec2 d0 = mesh.cells[c0].centroid - p0;
        if (d0.dot(n) > 0.0) {
            // c0 is on the positive side of the canonical normal:
            // c0 is the RIGHT cell, c1 (on the negative side) is LEFT.
            face.left_cell = c1;
            face.right_cell = c0;
            face.normal = n;
        } else {
            // c0 is on the negative side of the canonical normal:
            // c0 is the LEFT cell and the normal still points from the
            // negative side (c0) toward the positive side (c1).
            face.left_cell = c0;
            face.right_cell = c1;
            face.normal = n;
        }
        mesh.faces.push_back(std::move(face));
    }

    // --- 5. Assign cells to boundary faces and orient outward ---------------------
    for (std::size_t fi = 0; fi < n_boundary_faces; ++fi) {
        Face& face = mesh.faces[fi];
        const std::size_t a = face.nodes[0];
        const std::size_t b = face.nodes[1];
        EdgeKey key{std::min(a, b), std::max(a, b)};

        auto it = edges.find(key);
        if (it == edges.end() || it->second.cells.empty()) {
            throw std::runtime_error("Boundary face references an edge with no owning cell");
        }
        if (it->second.cells.size() > 1) {
            LOG_WARN("Boundary face ({}, {}) is shared by {} cells; treating as internal", a, b,
                     it->second.cells.size());
        }
        const std::size_t cell = it->second.cells[0];
        const Vec2& p0 = mesh.nodes[a].coord;
        const Vec2& p1 = mesh.nodes[b].coord;
        const Vec2 n = edge_normal(p0, p1);

        // Keep the cell as the LEFT cell; if the canonical normal points into
        // the cell, flip the stored node order so the normal points outward.
        const Vec2 d = mesh.cells[cell].centroid - p0;
        if (d.dot(n) > 0.0) {
            face.nodes = {b, a};
            face.normal = -n;
        } else {
            face.normal = n;
        }
        face.centroid = (p0 + p1) * 0.5;
        face.area = (p1 - p0).norm();
        face.left_cell = cell;
        face.right_cell = Face::INVALID;
        face.is_boundary = true;
    }

    if (n_skipped > 0) {
        LOG_WARN("{} non-manifold edges skipped during geometry construction", n_skipped);
    }

    // --- 6. Completeness: every 1-cell edge must be claimed by a boundary face ---
    // After internal-face creation, before cell->face adjacency: an edge used
    // by a single cell is either a boundary edge (must have a boundary face)
    // or a dangling/interface edge (missing boundary condition or a zone
    // interface that was not merged).
    {
        std::unordered_set<EdgeKey, EdgeKeyHash> boco_keys;
        boco_keys.reserve(n_boundary_faces);
        for (std::size_t fi = 0; fi < n_boundary_faces; ++fi) {
            const Face& bf = mesh.faces[fi];
            boco_keys.insert(
                EdgeKey{std::min(bf.nodes[0], bf.nodes[1]), std::max(bf.nodes[0], bf.nodes[1])});
        }
        for (const auto& [key, entry] : edges) {
            if (entry.cells.size() == 1 && boco_keys.find(key) == boco_keys.end()) {
                throw std::runtime_error("Edge (" + std::to_string(key.lo) + ", " +
                                         std::to_string(key.hi) +
                                         ") has only 1 cell but is not a boundary face — mesh may "
                                         "have missing boundary conditions");
            }
        }
    }

    // --- 7. Cell -> face / neighbor adjacency --------------------------------------
    for (auto& cell : mesh.cells) {
        cell.faces.clear();
        cell.neighbors.clear();
    }
    for (std::size_t fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& face = mesh.faces[fi];
        if (face.left_cell != Face::INVALID) {
            mesh.cells[face.left_cell].faces.push_back(fi);
        }
        if (face.right_cell != Face::INVALID) {
            mesh.cells[face.right_cell].faces.push_back(fi);
            mesh.cells[face.right_cell].neighbors.push_back(face.left_cell);
            mesh.cells[face.left_cell].neighbors.push_back(face.right_cell);
        }
    }
}

} // namespace cfd
