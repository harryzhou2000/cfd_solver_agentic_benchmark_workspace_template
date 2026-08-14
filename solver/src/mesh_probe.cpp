#include "mesh.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mesh_probe <mesh.cgns>\n";
        return 1;
    }
    cfd::Mesh mesh = cfd::read_cgns_mesh(argv[1]);
    std::cout << "cells: " << mesh.n_cells << "\n";
    std::cout << "faces: " << mesh.n_faces << "\n";
    std::cout << "boundary faces: " << mesh.n_boundary_faces << "\n";
    std::cout << "nodes: " << mesh.nodes.size() << "\n";
    for (const auto& [fam, fids] : mesh.boundary_families) {
        std::cout << "family " << fam << ": " << fids.size() << " faces\n";
    }
    // orientation sanity: all interior faces must have consistent normals
    long bad = 0;
    for (const auto& f : mesh.faces) {
        if (f.right_cell < 0) continue;
        cfd::Vec2 d = mesh.cells[f.right_cell].centroid - mesh.cells[f.left_cell].centroid;
        if (f.normal.dot(d) < 0) bad++;
    }
    std::cout << "bad interior normals: " << bad << "\n";

    // volume / face stats
    std::vector<double> vols;
    vols.reserve(mesh.n_cells);
    double vmin = 1e300, vmax = 0;
    for (const auto& c : mesh.cells) {
        vols.push_back(c.volume);
        vmin = std::min(vmin, c.volume);
        vmax = std::max(vmax, c.volume);
    }
    std::sort(vols.begin(), vols.end());
    std::cout << "cell volumes min=" << vmin << " median=" << vols[vols.size()/2]
              << " max=" << vmax << "\n";

    // boundary faces: S/vol ratio of the adjacent cell
    double svmax = 0;
    cfd::idx_t svcell = -1;
    long bad_wall_normals = 0;
    long n_wall = 0;
    for (const auto& f : mesh.faces) {
        if (f.right_cell >= 0) continue;
        double S = std::sqrt(f.normal[0]*f.normal[0] + f.normal[1]*f.normal[1]);
        double ratio = S / mesh.cells[f.left_cell].volume;
        if (ratio > svmax) { svmax = ratio; svcell = f.left_cell; }
        // wall faces belong to the 'bc-4' family; check outward orientation
        // against the airfoil interior (center ~ (0.5, 0))
        cfd::idx_t fam = mesh.boundary_families.begin()->first.empty() ? 0 : 1;
        (void)fam;
        if (mesh.bc_tag_map.count("bc-4") &&
            f.bc_tag == mesh.bc_tag_map.at("bc-4")) {
            n_wall++;
            double nx = f.normal[0]/S, ny = f.normal[1]/S;
            double rx = f.centroid[0] - 0.5, ry = f.centroid[1];
            if (nx*rx + ny*ry < 0) bad_wall_normals++;
        }
    }
    std::cout << "max boundary-face S/vol = " << svmax << " cell=" << svcell << "\n";
    std::cout << "wall faces: " << n_wall << " with inward-pointing normal: " << bad_wall_normals << "\n";

    // wall face x distribution
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (const auto& f : mesh.faces) {
        if (f.right_cell >= 0) continue;
        if (!mesh.bc_tag_map.count("bc-4") || f.bc_tag != mesh.bc_tag_map.at("bc-4")) continue;
        xmin = std::min(xmin, f.centroid[0]); xmax = std::max(xmax, f.centroid[0]);
        ymin = std::min(ymin, f.centroid[1]); ymax = std::max(ymax, f.centroid[1]);
    }
    std::cout << "wall face x range [" << xmin << "," << xmax << "] y range ["
              << ymin << "," << ymax << "]\n";

    // farfield extent
    xmin = 1e300; xmax = -1e300; ymin = 1e300; ymax = -1e300;
    for (const auto& f : mesh.faces) {
        if (f.right_cell >= 0) continue;
        if (!mesh.bc_tag_map.count("bc-2") || f.bc_tag != mesh.bc_tag_map.at("bc-2")) continue;
        xmin = std::min(xmin, f.centroid[0]); xmax = std::max(xmax, f.centroid[0]);
        ymin = std::min(ymin, f.centroid[1]); ymax = std::max(ymax, f.centroid[1]);
    }
    std::cout << "farfield x range [" << xmin << "," << xmax << "] y range ["
              << ymin << "," << ymax << "]\n";

    // Find a mid-chord wall face and check its orientation manually
    for (const auto& f : mesh.faces) {
        if (f.right_cell >= 0) continue;
        if (!mesh.bc_tag_map.count("bc-4") || f.bc_tag != mesh.bc_tag_map.at("bc-4")) continue;
        if (f.centroid[0] > 0.4 && f.centroid[0] < 0.6) {
            double S = std::sqrt(f.normal[0]*f.normal[0] + f.normal[1]*f.normal[1]);
            double nx = f.normal[0]/S, ny = f.normal[1]/S;
            const auto& cc = mesh.cells[f.left_cell].centroid;
            double to_face_mag = std::sqrt((f.centroid[0]-cc[0])*(f.centroid[0]-cc[0]) +
                                           (f.centroid[1]-cc[1])*(f.centroid[1]-cc[1]));
            std::cout << "mid-chord wall face " << f.id
                      << " L=" << f.left_cell
                      << " fc=(" << f.centroid[0] << "," << f.centroid[1] << ")"
                      << " n̂=(" << nx << "," << ny << ")"
                      << " cell_cc=(" << cc[0] << "," << cc[1] << ")"
                      << " |to_face|=" << to_face_mag << "\n";
            break;
        }
    }
    // Print bc_tag_map
    std::cout << "bc_tag_map:";
    for (const auto& [k, v] : mesh.bc_tag_map) std::cout << " " << k << "->" << v;
    std::cout << "\n";
    if (svcell >= 0) {
        const auto& c = mesh.cells[svcell];
        std::cout << "  cell centroid=(" << c.centroid[0] << "," << c.centroid[1]
                  << ") volume=" << c.volume << "\n";
    }

    // dump a known near-wall sliver cell (id 12430) node coordinates
    for (cfd::idx_t want : {12430, 14327}) {
        const auto& c = mesh.cells[want];
        std::cout << "cell " << want << " nodes:";
        for (cfd::idx_t nid : c.node_ids) {
            std::cout << " (" << mesh.nodes[nid][0] << "," << mesh.nodes[nid][1] << ")";
        }
        std::cout << " centroid=(" << c.centroid[0] << "," << c.centroid[1]
                  << ") vol=" << c.volume << " faces:";
        for (cfd::idx_t fid : c.face_ids) {
            const auto& f = mesh.faces[fid];
            std::cout << " [" << fid << " L" << f.left_cell << " R" << f.right_cell
                      << " n=(" << f.normal[0] << "," << f.normal[1]
                      << ") fc=(" << f.centroid[0] << "," << f.centroid[1] << ")]";
        }
        std::cout << "\n";
    }
    return 0;
}
