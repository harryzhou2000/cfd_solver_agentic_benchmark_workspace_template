#include "case.h"
#include "mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace cfd;

struct EdgeKey {
  int lo, hi;
  bool operator<(const EdgeKey& o) const {
    return lo != o.lo ? lo < o.lo : hi < o.hi;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: mesh_diag <case.json>\n");
    return 1;
  }
  CaseConfig cfg;
  std::string err;
  if (!load_case(argv[1], cfg, err)) {
    fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  Mesh mesh;
  if (!read_mesh(cfg.mesh_file, cfg.bc_map, mesh, err)) {
    fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }

  printf("mesh: %s\n", cfg.mesh_file.c_str());
  printf("  cells=%d faces=%d nodes=%d boundary_faces=%d\n",
         mesh.num_cells_global, mesh.num_faces_global, mesh.num_nodes_global,
         (int)mesh.boundary_faces.size());

  // Exact-edge map from cell connectivity (single and double records).
  std::map<EdgeKey, std::vector<int>> cell_edges;  // edge -> cells
  for (int ci = 0; ci < (int)mesh.cells.size(); ++ci) {
    const Cell& c = mesh.cells[ci];
    for (int k = 0; k < c.nverts; ++k) {
      int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
      cell_edges[EdgeKey{std::min(a, b), std::max(a, b)}].push_back(ci);
    }
  }

  int nonmanifold = 0;
  int single_count = 0;
  for (auto& kv : cell_edges) {
    if (kv.second.size() == 1) single_count++;
    if (kv.second.size() > 2) nonmanifold++;
  }
  printf("  distinct cell edges=%zu (single=%d, nonmanifold=%d)\n",
         cell_edges.size(), single_count, nonmanifold);

  // For each wall boundary face, find interior faces whose two endpoints are
  // within tol of the wall edge endpoints.
  const double tol = 1.0e-4;
  auto close = [&](int na, int nb, int ma, int mb) {
    const Vec2& a = mesh.nodes[na];
    const Vec2& b = mesh.nodes[nb];
    const Vec2& c = mesh.nodes[ma];
    const Vec2& d = mesh.nodes[mb];
    double d1 = std::hypot(a.x - c.x, a.y - c.y);
    double d2 = std::hypot(b.x - d.x, b.y - d.y);
    double e1 = std::hypot(a.x - d.x, a.y - d.y);
    double e2 = std::hypot(b.x - c.x, b.y - c.y);
    return std::min(d1 + d2, e1 + e2);
  };

  int wall_faces = 0, wall_with_mirror = 0, wall_with_mirror_multiple = 0;
  int sliver_cells = 0, bad_wall_cells = 0;
  double max_sep = 0.0, min_area = 1e30;
  int min_area_cell = -1;
  for (const BoundaryFace& bf : mesh.boundary_faces) {
    if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
    wall_faces++;
    const Face& f = mesh.faces[bf.face];
    int a = -1, b = -1;
    for (int k = 0; k < mesh.cells[bf.cell].nverts; ++k) {
      int n0 = mesh.cells[bf.cell].nodes[k];
      int n1 = mesh.cells[bf.cell].nodes[(k + 1) % mesh.cells[bf.cell].nverts];
      EdgeKey key{std::min(n0, n1), std::max(n0, n1)};
      auto it = cell_edges.find(key);
      if (it != cell_edges.end() && it->second.size() == 1 &&
          it->second[0] == bf.cell) {
        // boundary edge of this cell; need the node ids.
        // Find the edge key that contains this wall face midpoint: use face len.
        (void)key;
        a = n0; b = n1;
        // Prefer the shorter edge (the wall edge is 0.0005; sliver cross edges
        // are ~5e-6). The wall edge is a cell edge by construction, so the
        // first one found is fine unless the cell is a sliver with a tiny edge.
        if (std::hypot(mesh.nodes[n0].x - mesh.nodes[n1].x,
                       mesh.nodes[n0].y - mesh.nodes[n1].y) < 1e-4) {
          // tiny cross edge; try the next one
          continue;
        }
        break;
      }
    }
    if (a < 0) {
      bad_wall_cells++;
      continue;
    }
    std::vector<int> mirrors;
    for (auto& kv : cell_edges) {
      if (kv.second.size() != 2) continue;
      const std::vector<int>& ca = kv.second;
      int n0a = mesh.cells[ca[0]].nodes[0], n0b = mesh.cells[ca[0]].nodes[1];
      for (int k = 0; k < mesh.cells[ca[0]].nverts; ++k) {
        int x0 = mesh.cells[ca[0]].nodes[k];
        int x1 = mesh.cells[ca[0]].nodes[(k + 1) % mesh.cells[ca[0]].nverts];
        if (std::min(x0, x1) == kv.first.lo && std::max(x0, x1) == kv.first.hi) {
          n0a = x0; n0b = x1; break;
        }
      }
      double sep = close(a, b, n0a, n0b);
      if (sep < tol) {
        mirrors.push_back((int)kv.second.size());
        max_sep = std::max(max_sep, sep);
        if (kv.second[0] == bf.cell || kv.second[1] == bf.cell) {
          // mirror includes the wall cell: sliver
          if (mesh.cells[bf.cell].vol < min_area) {
            min_area = mesh.cells[bf.cell].vol;
            min_area_cell = bf.cell;
          }
        }
      }
    }
    if (!mirrors.empty()) {
      wall_with_mirror++;
      if (mirrors.size() > 1) wall_with_mirror_multiple++;
    }
    // Check whether the wall edge itself is shared (interior) - should not be.
    EdgeKey wk{std::min(a, b), std::max(a, b)};
    if (cell_edges[wk].size() == 2) bad_wall_cells++;
  }
  printf("  wall faces=%d, wall with mirror interior edge=%d (multiple=%d)\n",
         wall_faces, wall_with_mirror, wall_with_mirror_multiple);
  printf("  max wall/interior separation=%.3e, min wall-cell area=%.3e (cell %d)\n",
         max_sep, min_area, min_area_cell);
  printf("  bad wall cells=%d\n", bad_wall_cells);

  // Sliver-cell detection directly: cells whose area is below a threshold.
  int tiny = 0;
  int64_t min_area_big = 0;
  for (int ci = 0; ci < (int)mesh.cells.size(); ++ci) {
    if (mesh.cells[ci].vol < 1e-6) tiny++;
  }
  printf("  cells with vol<1e-6: %d\n", tiny);

  // Cells containing a pair of nearly coincident vertices (duplicate-curve
  // artifact). Tally for a few tolerances and report the spatial range.
  {
    const double tols[] = {2e-5, 3e-5, 5e-5, 1e-4, 2e-4};
    for (double tol : tols) {
      int cnt = 0, cnt_adj = 0;
      double xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
      for (const Cell& c : mesh.cells) {
        bool any = false, adj = false;
        for (int i = 0; i < c.nverts; ++i) {
          const Vec2& a = mesh.nodes[c.nodes[i]];
          for (int j = i + 1; j < c.nverts; ++j) {
            const Vec2& b = mesh.nodes[c.nodes[j]];
            double d = std::hypot(a.x - b.x, a.y - b.y);
            if (d < tol) any = true;
            if (j == (i + 1) % c.nverts || (i == 0 && j == c.nverts - 1)) {
              if (d < tol) adj = true;
            }
          }
        }
        if (any) {
          cnt++;
          xmin = std::min(xmin, c.cx); xmax = std::max(xmax, c.cx);
          ymin = std::min(ymin, c.cy); ymax = std::max(ymax, c.cy);
        }
        if (adj) cnt_adj++;
      }
      printf("  cells with close vertex pair < %.0e: any=%d adj=%d "
             "bbox x[%.4f,%.4f] y[%.4f,%.4f]\n",
             tol, cnt, cnt_adj, xmin, xmax, ymin, ymax);
    }
  }

  // Cell aspect-ratio histogram: area / Lmax^2. Sheath slivers should sit at
  // ~0.01 while ordinary cells are near-isotropic (>=~0.1).
  {
    std::vector<double> ratio;
    for (const Cell& c : mesh.cells) {
      double lmax = 0.0;
      for (int k = 0; k < c.nverts; ++k) {
        int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
        lmax = std::max(lmax, std::hypot(mesh.nodes[a].x - mesh.nodes[b].x,
                                         mesh.nodes[a].y - mesh.nodes[b].y));
      }
      ratio.push_back(c.vol / (lmax * lmax));
    }
    const double bins[] = {0, 0.005, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64};
    for (int b = 0; b + 1 < (int)(sizeof(bins)/sizeof(bins[0])); ++b) {
      int cnt = 0;
      for (double r : ratio) if (r >= bins[b] && r < bins[b+1]) cnt++;
      if (cnt) printf("    aspect [%.3f,%.3f): %d\n", bins[b], bins[b+1], cnt);
    }
  }

  // BFS cluster of cells connected to wall-adjacent cells through shared
  // faces: how big is the degenerate wall sheath?
  {
    std::map<EdgeKey, std::vector<int>> edge_cells;
    for (int ci = 0; ci < (int)mesh.cells.size(); ++ci) {
      const Cell& c = mesh.cells[ci];
      for (int k = 0; k < c.nverts; ++k) {
        int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
        edge_cells[EdgeKey{std::min(a,b), std::max(a,b)}].push_back(ci);
      }
    }
    std::vector<char> seen(mesh.cells.size(), 0);
    std::vector<int> q;
    for (const BoundaryFace& bf : mesh.boundary_faces) {
      if (bf.bc == BCType::SlipWall || bf.bc == BCType::NoSlipAdiabaticWall) {
        if (!seen[bf.cell]) { seen[bf.cell] = 1; q.push_back(bf.cell); }
      }
    }
    double min_area_in_cluster = 1e30, max_area_in_cluster = 0.0;
    int tiny_in_cluster = 0;
    for (size_t head = 0; head < q.size(); ++head) {
      const Cell& c = mesh.cells[q[head]];
      min_area_in_cluster = std::min(min_area_in_cluster, c.vol);
      max_area_in_cluster = std::max(max_area_in_cluster, c.vol);
      if (c.vol < 1e-6) tiny_in_cluster++;
      for (int k = 0; k < c.nverts; ++k) {
        int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
        EdgeKey key{std::min(a,b), std::max(a,b)};
        for (int nb : edge_cells[key]) {
          if (!seen[nb]) { seen[nb] = 1; q.push_back(nb); }
        }
      }
    }
    printf("  wall-connected cluster: %zu cells (tiny<1e-6: %d), "
           "area range [%.3e, %.3e]\n",
           q.size(), tiny_in_cluster, min_area_in_cluster, max_area_in_cluster);
  }

  // Nearest-neighbor distance histogram for nodes: find the gap between
  // duplicate-coordinate pairs (~5e-6) and genuine mesh spacing.
  {
    const int nsamp = std::min(20000, (int)mesh.nodes.size());
    std::vector<double> nn(nsamp, 1e30);
    for (int i = 0; i < nsamp; ++i) {
      for (int j = 0; j < nsamp; ++j) {
        if (i == j) continue;
        double d = std::hypot(mesh.nodes[i].x - mesh.nodes[j].x,
                              mesh.nodes[i].y - mesh.nodes[j].y);
        if (d < nn[i]) nn[i] = d;
      }
    }
    std::vector<int> hist_bins = {1,2,3,4,5,6,8,10,12,15,20,30,50,100,200,500,1000};
    printf("  node NN distance histogram (first %d nodes):\n", nsamp);
    for (size_t b = 0; b + 1 < hist_bins.size(); ++b) {
      int lo = hist_bins[b], hi = hist_bins[b+1];
      int cnt = 0;
      for (double d : nn) if (d >= lo*1e-6 && d < hi*1e-6) cnt++;
      if (cnt) printf("    [%d,%d)e-6: %d\n", lo, hi, cnt);
    }
    int below = 0;
    for (double d : nn) if (d < 2e-5) below++;
    printf("  nodes with NN distance < 2e-5: %d\n", below);
  }

  // Symmetry check: does every node have a y-mirror partner?
  {
    int matched = 0;
    const double tol = 1e-6;
    for (const Vec2& n : mesh.nodes) {
      bool ok = false;
      for (const Vec2& m : mesh.nodes) {
        if (std::hypot(n.x - m.x, n.y + m.y) < tol) { ok = true; break; }
      }
      if (ok) matched++;
    }
    printf("  nodes with y-mirror partner (tol %.0e): %d / %zu\n",
           tol, matched, mesh.nodes.size());
  }

  // Interface pairing quality: for each interior face, the unit normal must
  // point from ca toward cb (centroid-to-centroid dot with normal > 0).
  {
    int bad = 0, checked = 0;
    double worst = -1.0;
    for (const Face& f : mesh.faces) {
      if (f.cb < 0) continue;
      checked++;
      double nxu = f.nx / f.len, nyu = f.ny / f.len;
      const Cell& a = mesh.cells[f.ca];
      const Cell& b = mesh.cells[f.cb];
      double dot = nxu * (b.cx - a.cx) + nyu * (b.cy - a.cy);
      if (dot <= 0.0) {
        bad++;
        worst = std::max(worst, dot);
        if (bad <= 3) {
          printf("  BAD-FACE %d ca=%d cb=%d dot=%.3e len=%.4e n=(%.4f,%.4f)\n",
                 (int)(&f - mesh.faces.data()), f.ca, f.cb, dot, f.len, nxu, nyu);
        }
      }
    }
    printf("  interior faces checked=%d bad orientation=%d worst dot=%.3e\n",
           checked, bad, worst);
  }

  // Reproduce the read_mesh sliver-detection logic in detail.
  {
    const double tol = 1.0e-4;
    std::map<EdgeKey, std::vector<int>> single_owner;
    std::map<EdgeKey, std::vector<std::pair<int,int>>> interior_cells;
    for (auto& kv : cell_edges) {
      if (kv.second.size() == 1) single_owner[kv.first] = kv.second;
      if (kv.second.size() == 2) {
        interior_cells[kv.first] = {{kv.second[0], -1}, {kv.second[1], -1}};
      }
    }
    int checked = 0, found = 0, mismatch = 0;
    for (const BoundaryFace& bf : mesh.boundary_faces) {
      if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
      // Wall edge endpoints from the boundary face's owning cell traversal.
      int wa = -1, wb = -1;
      const Cell& c = mesh.cells[bf.cell];
      for (int k = 0; k < c.nverts; ++k) {
        int n0 = c.nodes[k], n1 = c.nodes[(k + 1) % c.nverts];
        EdgeKey key{std::min(n0, n1), std::max(n0, n1)};
        auto it = single_owner.find(key);
        if (it != single_owner.end() && it->second[0] == bf.cell) {
          wa = n0; wb = n1;
          break;
        }
      }
      if (wa < 0) continue;
      checked++;
      const Vec2& pwa = mesh.nodes[wa];
      const Vec2& pwb = mesh.nodes[wb];
      double wdx = pwb.x - pwa.x, wdy = pwb.y - pwa.y;
      double wlen = std::hypot(wdx, wdy);
      int best_parallel = -1;
      double best_sep = 1e30;
      for (auto& kv : interior_cells) {
        int n0 = kv.second[0].first;
        int n1 = -1;
        const Cell& cc = mesh.cells[n0];
        for (int k = 0; k < cc.nverts; ++k) {
          int x0 = cc.nodes[k], x1 = cc.nodes[(k + 1) % cc.nverts];
          if (std::min(x0, x1) == kv.first.lo && std::max(x0, x1) == kv.first.hi) {
            n0 = x0; n1 = x1; break;
          }
        }
        if (n1 < 0) continue;
        const Vec2& ma = mesh.nodes[n0];
        const Vec2& mb = mesh.nodes[n1];
        double d1 = std::hypot(ma.x-pwa.x, ma.y-pwa.y) + std::hypot(mb.x-pwb.x, mb.y-pwb.y);
        double d2 = std::hypot(ma.x-pwb.x, ma.y-pwb.y) + std::hypot(mb.x-pwa.x, mb.y-pwa.y);
        double sep = std::min(d1, d2);
        double mdx = mb.x-ma.x, mdy = mb.y-ma.y;
        double mlen = std::hypot(mdx, mdy);
        if (mlen <= 0) continue;
        double dot = (wdx*mdx + wdy*mdy)/(wlen*mlen);
        if (dot > 0.99 && sep < tol && sep < best_sep) {
          best_sep = sep;
          best_parallel = (int)kv.second[0].first;
        }
      }
      if (best_parallel < 0) {
        printf("  SLIVER: wall cell %d no parallel mirror (edge %d-%d)\n",
               bf.cell, wa, wb);
        continue;
      }
      // Determine whether the mirror contains the wall cell.
      found++;
      bool contains = false;
      for (auto& kv : interior_cells) {
        for (auto& pr : kv.second) {
          if (pr.first == bf.cell) contains = true;
        }
      }
      if (!contains) {
        mismatch++;
        if (mismatch <= 3) {
          printf("  MISMATCH wall cell %d edge %d-%d mirror cell %d sep %.3e\n",
                 bf.cell, wa, wb, best_parallel, best_sep);
        }
      }
    }
  printf("  sliver detail: checked=%d found_mirror=%d not_containing_wall_cell=%d\n",
           checked, found, mismatch);
  }

  // Detailed dump of a few wall cells: are the wall edge and the parallel
  // interior edge the two long sides of a thin but genuine first-layer cell?
  {
    std::map<EdgeKey, std::vector<int>> edge_cells;
    for (int ci = 0; ci < (int)mesh.cells.size(); ++ci) {
      const Cell& c = mesh.cells[ci];
      for (int k = 0; k < c.nverts; ++k) {
        int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
        edge_cells[EdgeKey{std::min(a,b), std::max(a,b)}].push_back(ci);
      }
    }
    int shown = 0;
    for (const BoundaryFace& bf : mesh.boundary_faces) {
      if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
      const Cell& c = mesh.cells[bf.cell];
      // Find the wall edge of this cell: the long edge whose key appears once.
      for (int k = 0; k < c.nverts; ++k) {
        int a = c.nodes[k], b = c.nodes[(k + 1) % c.nverts];
        EdgeKey key{std::min(a,b), std::max(a,b)};
        if (edge_cells[key].size() == 1 && std::hypot(mesh.nodes[a].x-mesh.nodes[b].x,
                                                      mesh.nodes[a].y-mesh.nodes[b].y) > 1e-4) {
          if (shown < 2) {
            printf("  WALLCELL %d: edge %d-%d (%.6f,%.6f)->(%.6f,%.6f) len %.3e\n",
                   bf.cell, a, b, mesh.nodes[a].x, mesh.nodes[a].y,
                   mesh.nodes[b].x, mesh.nodes[b].y,
                   std::hypot(mesh.nodes[a].x-mesh.nodes[b].x,
                              mesh.nodes[a].y-mesh.nodes[b].y));
            printf("    cell nodes:");
            for (int j = 0; j < c.nverts; ++j)
              printf(" %d(%.7f,%.7f)", c.nodes[j], mesh.nodes[c.nodes[j]].x,
                     mesh.nodes[c.nodes[j]].y);
            printf("  centroid (%.7f,%.7f) vol %.3e\n", c.cx, c.cy, c.vol);
            // Find the long edge parallel to the wall edge and its shared cells.
            double wdx = mesh.nodes[b].x-mesh.nodes[a].x;
            double wdy = mesh.nodes[b].y-mesh.nodes[a].y;
            double wl = std::hypot(wdx, wdy);
            for (int k2 = 0; k2 < c.nverts; ++k2) {
              int x0 = c.nodes[k2], x1 = c.nodes[(k2+1)%c.nverts];
              if (std::min(x0,x1) == std::min(a,b) && std::max(x0,x1) == std::max(a,b)) continue;
              double dx = mesh.nodes[x1].x-mesh.nodes[x0].x;
              double dy = mesh.nodes[x1].y-mesh.nodes[x0].y;
              double l = std::hypot(dx, dy);
              if (l > 1e-4) {
                double dot = (wdx*dx + wdy*dy)/(wl*l);
                if (dot > 0.9) {
                  EdgeKey ek{std::min(x0,x1), std::max(x0,x1)};
                  printf("    parallel edge %d-%d len %.3e shared with cells:",
                         x0, x1, l);
                  for (int nb : edge_cells[ek]) printf(" %d", nb);
                  printf("\n");
                }
              }
            }
            shown++;
          }
          break;
        }
      }
      if (shown >= 2) break;
    }
  }

  // Dump wall cells right at the LE tip (centroid |x| < 0.002).
  {
    int shown = 0;
    for (const BoundaryFace& bf : mesh.boundary_faces) {
      if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
      const Cell& c = mesh.cells[bf.cell];
      if (std::abs(c.cx) > 0.002 || std::abs(c.cy) > 0.03) continue;
      printf("  LE-TIP-CELL %d: nv %d nodes", bf.cell, c.nverts);
      for (int k = 0; k < c.nverts; ++k) {
        printf(" %d(%.7f,%.7f)", c.nodes[k], mesh.nodes[c.nodes[k]].x,
               mesh.nodes[c.nodes[k]].y);
      }
      printf(" cen(%.7f,%.7f) vol %.3e\n", c.cx, c.cy, c.vol);
      for (int fid : mesh.cell_faces[bf.cell]) {
        const Face& f = mesh.faces[fid];
        const char* kind = f.cb >= 0 ? "int" : "bnd";
        printf("    face %d %s ca=%d cb=%d len %.4e n %.6f %.6f", fid, kind,
               f.ca, f.cb, f.len,
               f.len > 0 ? f.nx / f.len : 0.0, f.len > 0 ? f.ny / f.len : 0.0);
        if (f.cb >= 0) {
          printf(" neigh g=%d", mesh.cells[f.cb].cx >= 0 ? f.cb : -1);
        } else {
          printf(" bc=%s", bc_type_name(f.bc));
        }
        printf("\n");
      }
      if (++shown >= 12) break;
    }
    printf("  (LE tip wall cells shown: %d)\n", shown);
  }

  // Zone / family summary
  std::map<std::string, int> famcount;
  for (const BoundaryFace& bf : mesh.boundary_faces) famcount[bf.family]++;
  for (auto& kv : famcount) {
    printf("  family %-12s faces=%d\n", kv.first.c_str(), kv.second);
  }
  return 0;
}
