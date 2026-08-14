#include "mesh/GlobalMesh.hpp"

#include <algorithm>
#include <cstdio>

using namespace cfds;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: mesh_info <mesh.cgns>\n");
    return 1;
  }
  try {
    GlobalMesh mesh = load_cgns_mesh(argv[1]);
    std::printf("nodes:      %zu\n", mesh.nodes.size());
    std::printf("cells:      %zu\n", mesh.cells.size());
    std::printf("faces:      %zu\n", mesh.faces.size());
    int interior = 0, boundary = 0;
    double min_vol = 1e300, max_vol = 0.0;
    for (const auto& f : mesh.faces) {
      if (f.cellR >= 0) ++interior; else ++boundary;
    }
    for (const auto& c : mesh.cells) {
      min_vol = std::min(min_vol, c.volume);
      max_vol = std::max(max_vol, c.volume);
    }
    std::printf("interior faces: %d, boundary faces: %d\n", interior, boundary);
    std::printf("cell volume range: [%g, %g]\n", min_vol, max_vol);
    std::printf("boundary families:\n");
    for (const auto& [fam, n] : mesh.family_bc_face_count)
      std::printf("  %-12s %d faces\n", fam.c_str(), n);
    // Sum of outward normals x length over boundary faces: must be ~0 for a
    // closed domain (a constant vector field has zero net boundary flux).
    double sx = 0.0, sy = 0.0;
    std::map<std::string, std::pair<double, double>> fam_sum;
    for (const auto& f : mesh.faces) {
      if (f.cellR == -1) {
        sx += f.normal[0] * f.length;
        sy += f.normal[1] * f.length;
        fam_sum[f.family].first += f.normal[0] * f.length;
        fam_sum[f.family].second += f.normal[1] * f.length;
      }
    }
    std::printf("boundary normal-length sum: [%g, %g] (should be ~0)\n", sx, sy);
    for (const auto& [fam, s] : fam_sum)
      std::printf("  family %-10s sum [%g, %g]\n", fam.c_str(), s.first, s.second);
    // For the farfield, check the normal points away from the body center.
    double cx = 0.25, cy = 0.0;
    if (mesh.family_bc_face_count.count("FAR")) { cx = 0.0; cy = 0.0; }
    int inward = 0, outward = 0;
    double rmin = 1e300, rmax = 0.0;
    for (const auto& f : mesh.faces) {
      if (f.cellR != -1) continue;
      if (f.family != "bc-2" && f.family != "FAR") continue;
      const double dx = f.centroid[0] - cx;
      const double dy = f.centroid[1] - cy;
      const double r = std::sqrt(dx * dx + dy * dy);
      rmin = std::min(rmin, r);
      rmax = std::max(rmax, r);
      const double dot = dx * f.normal[0] + dy * f.normal[1];
      if (dot > 0) ++outward;
      else {
        ++inward;
        std::printf("  inward face: nodes %d %d at (%g, %g) normal (%g, %g) len %g cell %d\n",
                    f.n0, f.n1, f.centroid[0], f.centroid[1],
                    f.normal[0], f.normal[1], f.length, f.cellL);
        const Cell& c = mesh.cells[f.cellL];
        std::printf("    cell %d nodes:", f.cellL);
        for (int nd : c.nodes) std::printf(" %d", nd);
        std::printf("\n    coords:");
        for (int nd : c.nodes)
          std::printf(" (%g,%g)", mesh.nodes[nd][0], mesh.nodes[nd][1]);
        std::printf("\n    volume %g\n", c.volume);
      }
      (void)r;
    }
    std::printf("farfield faces outward=%d inward=%d radius range [%g, %g]\n",
                outward, inward, rmin, rmax);
    // Wall faces near the leading edge: check the normal points away from
    // the airfoil interior (dot with (face_centroid - cell_centroid) > 0).
    int wall_near_le = 0, wall_bad = 0;
    for (const auto& f : mesh.faces) {
      if (f.cellR != -1) continue;
      if (f.family != "bc-4" && f.family != "WALL") continue;
      if (f.centroid[0] > 0.05) continue;
      ++wall_near_le;
      const Cell& c = mesh.cells[f.cellL];
      const double dx = f.centroid[0] - c.centroid[0];
      const double dy = f.centroid[1] - c.centroid[1];
      const double dot = dx * f.normal[0] + dy * f.normal[1];
      if (dot <= 0.0) {
        ++wall_bad;
        std::printf("  LE wall face (%g,%g) n (%g,%g) cell %d vol %g cellc (%g,%g) dot %g\n",
                    f.centroid[0], f.centroid[1], f.normal[0], f.normal[1],
                    f.cellL, c.volume, c.centroid[0], c.centroid[1], dot);
      }
    }
    std::printf("LE wall faces (x<0.05): %d, bad normal count %d\n",
                wall_near_le, wall_bad);
    // Smallest wall-adjacent cells near the LE.
    std::vector<std::pair<double, int>> wall_cells;
    for (const auto& f : mesh.faces) {
      if (f.cellR != -1) continue;
      if (f.family != "bc-4" && f.family != "WALL") continue;
      if (f.centroid[0] > 0.05) continue;
      wall_cells.emplace_back(mesh.cells[f.cellL].volume, f.cellL);
    }
    std::sort(wall_cells.begin(), wall_cells.end());
    for (int k = 0; k < (int)std::min<size_t>(8, wall_cells.size()); ++k) {
      const Cell& c = mesh.cells[wall_cells[k].second];
      std::printf("  small wall cell %d vol %.3e center (%g,%g)\n",
                  wall_cells[k].second, wall_cells[k].first,
                  c.centroid[0], c.centroid[1]);
    }
    // Check the cell adjacency graph is symmetric.
    int asym = 0;
    for (int c = 0; c < static_cast<int>(mesh.cells.size()); ++c) {
      for (int k = mesh.cell_neighbor_offsets[c]; k < mesh.cell_neighbor_offsets[c+1]; ++k) {
        int nb = mesh.cell_neighbors[k];
        if (nb >= 0) {
          bool found = false;
          for (int j = mesh.cell_neighbor_offsets[nb]; j < mesh.cell_neighbor_offsets[nb+1]; ++j)
            if (mesh.cell_neighbors[j] == c) { found = true; break; }
          if (!found) ++asym;
        }
      }
    }
    std::printf("asymmetric neighbor entries: %d\n", asym);
    // Interior faces: the stored normal must point from cellL toward cellR.
    int bad_n = 0, tot = 0;
    double worst = 0.0;
    for (const auto& f : mesh.faces) {
      if (f.cellR < 0) continue;
      ++tot;
      const double dx = mesh.cells[f.cellR].centroid[0] - mesh.cells[f.cellL].centroid[0];
      const double dy = mesh.cells[f.cellR].centroid[1] - mesh.cells[f.cellL].centroid[1];
      const double dot = dx * f.normal[0] + dy * f.normal[1];
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dot < 0) {
        ++bad_n;
        if (dist > worst) worst = dist;
      }
    }
    std::printf("interior faces with L->R normal pointing away: %d / %d (worst dist %g)\n",
                bad_n, tot, worst);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
