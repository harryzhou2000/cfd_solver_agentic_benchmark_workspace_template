// Cell/face geometry construction from raw CGNS import data.
//
//  1. cell volumes (polygon areas) and centroids,
//  2. internal faces: shared edges between two cells (edge-key map),
//  3. face geometry: normals (magnitude = edge length), centers; internal
//     face normals are oriented from left cell to right cell,
//  4. boundary faces: mapped to their adjacent cell, normals oriented
//     outward from the domain, BC type resolved via the family map.

#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include "boundary.hpp"

namespace cfd {

namespace {

// Packed 64-bit key for an undirected edge (node ids < 2^31).
std::uint64_t edge_key(cgsize_t a, cgsize_t b) {
    const cgsize_t lo = std::min(a, b);
    const cgsize_t hi = std::max(a, b);
    return (static_cast<std::uint64_t>(lo) << 32) |
           static_cast<std::uint64_t>(hi);
}

// Resolves the BC type for a boundary family tag: first the case-file map,
// then bc_type_from_string on the tag itself, else Unknown.
BCType resolve_bc_type(
    const std::string& tag,
    const std::map<std::string, std::string>& family_bc_map) {
    auto it = family_bc_map.find(tag);
    if (it != family_bc_map.end()) {
        const BCType mapped = bc_type_from_string(it->second);
        if (mapped != BCType::Unknown) return mapped;
    }
    const BCType direct = bc_type_from_string(tag);
    if (direct != BCType::Unknown) return direct;
    return BCType::Unknown;
}

}  // namespace

Mesh build_geometry(const MeshData& data,
                    const std::map<std::string, std::string>& family_bc_map) {
    Mesh mesh;

    mesh.x = data.x;
    mesh.y = data.y;
    mesh.n_nodes = data.n_nodes;
    mesh.n_cells = data.n_cells;

    // --- 1. Cell volumes and centers ------------------------------------------
    mesh.cell_vol.resize(static_cast<size_t>(mesh.n_cells));
    mesh.cell_center_x.resize(static_cast<size_t>(mesh.n_cells));
    mesh.cell_center_y.resize(static_cast<size_t>(mesh.n_cells));

    // Edge registration: key -> first (owner) cell and the edge direction as
    // stored in that cell's node loop; `consumed` marks edges already turned
    // into an internal face (a third occurrence is a mesh degeneracy).
    struct EdgeInfo {
        cgsize_t cell = -1;
        cgsize_t a = -1, b = -1;
        bool consumed = false;
    };
    std::unordered_map<std::uint64_t, EdgeInfo> edge_info;
    edge_info.reserve(static_cast<size_t>(mesh.n_cells) * 4);

    // Internal faces (left = first cell to register the edge, right = second).
    std::vector<cgsize_t> face_left, face_right;
    std::vector<double> face_nx, face_ny, face_area;
    std::vector<double> face_cx, face_cy;
    face_left.reserve(static_cast<size_t>(mesh.n_cells) * 2);
    face_right.reserve(static_cast<size_t>(mesh.n_cells) * 2);

    auto add_face = [&](cgsize_t cell1, cgsize_t a, cgsize_t b,
                        cgsize_t cell2) {
        const double xa = data.x[static_cast<size_t>(a)];
        const double ya = data.y[static_cast<size_t>(a)];
        const double xb = data.x[static_cast<size_t>(b)];
        const double yb = data.y[static_cast<size_t>(b)];

        // Face geometry from the edge direction a -> b (as stored in cell1).
        double nx = yb - ya;
        double ny = -(xb - xa);
        const double len = std::hypot(nx, ny);

        // Orient the normal from cell1 (left) toward cell2 (right).
        const double lx = mesh.cell_center_x[static_cast<size_t>(cell1)];
        const double ly = mesh.cell_center_y[static_cast<size_t>(cell1)];
        const double rx = mesh.cell_center_x[static_cast<size_t>(cell2)];
        const double ry = mesh.cell_center_y[static_cast<size_t>(cell2)];
        if (nx * (rx - lx) + ny * (ry - ly) < 0.0) {
            nx = -nx;
            ny = -ny;
        }

        face_left.push_back(cell1);
        face_right.push_back(cell2);
        face_nx.push_back(nx);
        face_ny.push_back(ny);
        face_area.push_back(len);
        face_cx.push_back(0.5 * (xa + xb));
        face_cy.push_back(0.5 * (ya + yb));
    };

    for (cgsize_t c = 0; c < mesh.n_cells; ++c) {
        const auto& nodes = data.cell_nodes[static_cast<size_t>(c)];
        const size_t n = nodes.size();
        if (n < 3) {
            throw std::runtime_error("cell " + std::to_string(c) +
                                     " has fewer than 3 nodes");
        }

        // Centroid: average of node coordinates.
        double cx = 0.0, cy = 0.0;
        for (cgsize_t node : nodes) {
            cx += data.x[static_cast<size_t>(node)];
            cy += data.y[static_cast<size_t>(node)];
        }
        mesh.cell_center_x[static_cast<size_t>(c)] = cx / static_cast<double>(n);
        mesh.cell_center_y[static_cast<size_t>(c)] = cy / static_cast<double>(n);

        // Signed polygon area: A = 0.5 * sum (x_i*y_{i+1} - x_{i+1}*y_i).
        double sum = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const size_t k1 = (k + 1) % n;
            const double x0 = data.x[static_cast<size_t>(nodes[k])];
            const double y0 = data.y[static_cast<size_t>(nodes[k])];
            const double x1 = data.x[static_cast<size_t>(nodes[k1])];
            const double y1 = data.y[static_cast<size_t>(nodes[k1])];
            sum += x0 * y1 - x1 * y0;
        }
        mesh.cell_vol[static_cast<size_t>(c)] = std::fabs(sum) * 0.5;

        // Register edges (including wrap-around).
        for (size_t k = 0; k < n; ++k) {
            const cgsize_t a = nodes[k];
            const cgsize_t b = nodes[(k + 1) % n];
            const std::uint64_t key = edge_key(a, b);
            auto it = edge_info.find(key);
            if (it == edge_info.end()) {
                edge_info.emplace(key, EdgeInfo{c, a, b, false});
            } else if (!it->second.consumed) {
                // Shared edge: complete the internal face, left = owner cell.
                it->second.consumed = true;
                add_face(it->second.cell, it->second.a, it->second.b, c);
            }
            // Third+ occurrence of the same edge: degenerate mesh, ignore.
        }
    }

    // --- Zonal interface faces ------------------------------------------------
    // Multi-zone meshes declare the inter-zone boundary as BAR_2 element
    // sections no boco references; the reader matched the coincident edge
    // copies (one per zone) and recorded them here. Each pair becomes one
    // internal face between the two adjacent cells, oriented left -> right
    // by add_face (the same orientation rule as for shared edges).
    for (const auto& zf : data.zonal_faces) {
        if (zf.cell_l < 0 || zf.cell_r < 0 || zf.node_a < 0 || zf.node_b < 0 ||
            zf.cell_l >= mesh.n_cells || zf.cell_r >= mesh.n_cells) {
            continue;
        }
        add_face(zf.cell_l, zf.node_a, zf.node_b, zf.cell_r);
    }

    // --- 2/3. Internal faces and face geometry ----------------------------------
    mesh.face_left = std::move(face_left);
    mesh.face_right = std::move(face_right);
    mesh.face_nx = std::move(face_nx);
    mesh.face_ny = std::move(face_ny);
    mesh.face_area = std::move(face_area);
    mesh.face_center_x = std::move(face_cx);
    mesh.face_center_y = std::move(face_cy);
    mesh.n_faces = static_cast<cgsize_t>(mesh.face_left.size());

    // --- 4. Boundary faces -------------------------------------------------------
    mesh.bface_cell.reserve(data.boundary_faces.size());
    mesh.bface_bc_type.reserve(data.boundary_faces.size());
    mesh.bface_tag.reserve(data.boundary_faces.size());
    mesh.bface_nx.reserve(data.boundary_faces.size());
    mesh.bface_ny.reserve(data.boundary_faces.size());
    mesh.bface_area.reserve(data.boundary_faces.size());
    mesh.bface_center_x.reserve(data.boundary_faces.size());
    mesh.bface_center_y.reserve(data.boundary_faces.size());

    for (const auto& bf : data.boundary_faces) {
        if (bf.node_ids.size() < 2 || bf.cell_id < 0 ||
            bf.cell_id >= mesh.n_cells) {
            // Unresolved boundary face: skip with a note (the solver prints
            // a summary, so no exception here).
            continue;
        }
        const cgsize_t a = bf.node_ids[0];
        const cgsize_t b = bf.node_ids[1];
        const double xa = data.x[static_cast<size_t>(a)];
        const double ya = data.y[static_cast<size_t>(a)];
        const double xb = data.x[static_cast<size_t>(b)];
        const double yb = data.y[static_cast<size_t>(b)];

        double nx = yb - ya;
        double ny = -(xb - xa);

        // Orient the normal outward from the adjacent cell.
        const size_t c = static_cast<size_t>(bf.cell_id);
        const double fx = 0.5 * (xa + xb);
        const double fy = 0.5 * (ya + yb);
        const double out_dot =
            nx * (mesh.cell_center_x[c] - fx) +
            ny * (mesh.cell_center_y[c] - fy);
        if (out_dot > 0.0) {
            nx = -nx;
            ny = -ny;
        }

        mesh.bface_cell.push_back(bf.cell_id);
        mesh.bface_bc_type.push_back(
            static_cast<int>(resolve_bc_type(bf.family_name, family_bc_map)));
        mesh.bface_tag.push_back(bf.family_name);
        mesh.bface_nx.push_back(nx);
        mesh.bface_ny.push_back(ny);
        mesh.bface_area.push_back(std::hypot(nx, ny));
        mesh.bface_center_x.push_back(0.5 * (xa + xb));
        mesh.bface_center_y.push_back(0.5 * (ya + yb));
    }
    mesh.n_boundary_faces =
        static_cast<cgsize_t>(mesh.bface_cell.size());

    return mesh;
}

Mesh build_geometry(const MeshData& data) {
    return build_geometry(data, {});
}

}  // namespace cfd
