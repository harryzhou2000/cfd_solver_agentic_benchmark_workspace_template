#include "geometry.hpp"
#include <iostream>
#include <algorithm>
#include <set>
#include <string>

Real compute_cell_volume(const Cell& cell, const std::vector<Face>& faces) {
    // Divergence theorem: V = 1/2 * sum_f (x_f · n_f) * A_f
    // where x_f is face centroid, n_f is unit normal, A_f is face area
    Real vol = 0.0;
    for (FaceID fid : cell.faces) {
        const Face& f = faces[fid];
        vol += f.centroid.dot(f.normal) * f.area;
    }
    return 0.5 * std::abs(vol);
}

void compute_geometry(Mesh& mesh) {
    // Compute cell centroids from face centroids (area-weighted)
    for (Index ic = 0; ic < mesh.num_cells; ic++) {
        Cell& cell = mesh.cells[ic];
        Vec2 sum(0,0);
        Real total_area = 0.0;
        for (FaceID fid : cell.faces) {
            const Face& f = mesh.faces[fid];
            sum += f.centroid * f.area;
            total_area += f.area;
        }
        if (total_area > 0) {
            cell.centroid = sum / total_area;
        }
        cell.volume = compute_cell_volume(cell, mesh.faces);
    }

    // Recompute face normals with proper cell-centroid direction
    for (FaceID fid = 0; fid < mesh.num_faces; fid++) {
        Face& f = mesh.faces[fid];
        if (f.right >= 0) {
            // Internal face: normal points left -> right
            Vec2 dir = mesh.cells[f.right].centroid - mesh.cells[f.left].centroid;
            if (dir.dot(f.normal) < 0) {
                f.normal = -f.normal;
            }
        } else if (f.left >= 0) {
            // Boundary face: normal points outward (away from cell)
            Vec2 dir = f.centroid - mesh.cells[f.left].centroid;
            if (dir.dot(f.normal) < 0) {
                f.normal = -f.normal;
            }
        }
    }

    std::cout << "Geometry computed: cells=" << mesh.num_cells
              << " faces=" << mesh.num_faces
              << " internal=" << mesh.num_internal_faces
              << " boundary=" << mesh.boundary_faces.size() << std::endl;
}

void classify_boundary_faces(Mesh& mesh, const CaseConfig& cfg) {
    mesh.wall_faces.clear();

    // Debug: print what we have
    std::cout << "  Boundary face families found: ";
    std::set<std::string> families;
    for (auto& [fid, bf] : mesh.boundary_faces) {
        families.insert(bf.family_name);
    }
    for (const auto& f : families) {
        std::cout << "'" << f << "' ";
    }
    std::cout << std::endl;
    std::cout << "  Case BC mapping: ";
    for (const auto& [fam, bc] : cfg.bc_map) {
        std::cout << "'" << fam << "'->" << bc_type_to_string(bc) << " ";
    }
    std::cout << std::endl;

    for (auto& [fid, bf] : mesh.boundary_faces) {
        // Match family name against case BC mapping
        for (const auto& [fam, bc] : cfg.bc_map) {
            if (bf.family_name == fam) {
                bf.bc_type = bc;
                break;
            }
        }

        // Propagate bc_type to the actual Face object
        if (fid >= 0 && fid < mesh.num_faces) {
            mesh.faces[fid].bc_type = bf.bc_type;
        }

        // Collect wall faces
        if (bf.bc_type == BCType::SlipWall ||
            bf.bc_type == BCType::NoSlipAdiabaticWall) {
            mesh.wall_faces.push_back(fid);
        }
    }

    std::cout << "Boundary classification: " << mesh.boundary_faces.size()
              << " boundary faces, " << mesh.wall_faces.size() << " wall faces" << std::endl;
}
